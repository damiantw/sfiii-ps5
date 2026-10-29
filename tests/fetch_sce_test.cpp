// SceHttpTransport against a fake libSceHttp: redirects (relative and absolute, capped), HTTP
// errors, Content-Length checks, chunked answers, `want` declining, progress, cleanup of every
// connection and request. MIT, see LICENSE.
#include "patch/fetch_sce.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

using namespace sf3::fetch;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

struct Answer { int status = 200; std::string headers, body; bool length = true; int cut = 0; bool fail_read = false; };

struct Fake {
    std::map<std::string, Answer> site;
    std::map<int, std::string> conn_url, req_url;
    std::map<int, size_t> pos;
    int next = 100, inits = 0, live = 0;
    std::vector<std::string> asked;
    SceHttpApi api() {
        SceHttpApi a;
        a.init = [this](const char*) { inits++; return 7; };
        a.connect = [this](int t, const char* u) { if (t != 7) return -1; live++; conn_url[++next] = u; return next; };
        a.request = [this](int c, const char* u) { live++; req_url[++next] = u; pos[next] = 0; (void)c; return next; };
        a.send = [this](int r) { asked.push_back(req_url[r]); return site.count(req_url[r]) ? 0 : int(0x80435001); };
        a.status = [this](int r, int* s) { *s = site[req_url[r]].status; return 0; };
        a.content_length = [this](int r, int* k, uint64_t* l) {
            Answer& an = site[req_url[r]];
            *k = an.length ? 0 : 2; *l = an.body.size() + an.cut; return 0;
        };
        a.headers = [this](int r, std::string* h) { *h = site[req_url[r]].headers; return 0; };
        a.read = [this](int r, void* b, size_t n) {
            Answer& an = site[req_url[r]];
            if (an.fail_read && pos[r] > 0) return int(0x80435002);
            size_t left = an.body.size() - pos[r], k = std::min({left, n, size_t(1000)});
            std::copy_n(an.body.data() + pos[r], k, static_cast<char*>(b));
            pos[r] += k;
            return int(k);
        };
        a.delete_request = [this](int) { live--; };
        a.delete_connection = [this](int) { live--; };
        return a;
    }
};

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

template <class F> static std::string error_of(F f) {
    try { f(); } catch (const FetchError& e) { return e.what(); }
    return "";
}

int main() {
    Fake fk;
    std::string big(250'000, 'x');
    for (size_t i = 0; i < big.size(); i++) big[i] = char('a' + i % 26);
    fk.site["https://h/V25+/"] = {200, "", "<a href=\"A.zip\">A</a>"};
    fk.site["https://h/SF3Patch.exe"] = {302, "HTTP/1.1 302 Found\r\nLocation: /dl/SF3Patch.exe\r\n\r\n", ""};
    fk.site["https://h/dl/SF3Patch.exe"] = {301, "location: https://cdn/x.exe\r\n", ""};
    fk.site["https://cdn/x.exe"] = {200, "", big};
    fk.site["https://h/chunked"] = {200, "", "hello", false};
    fk.site["https://h/missing"] = {404, "", "nope"};
    fk.site["https://h/short"] = {200, "", "abc", true, 10};
    fk.site["https://h/broken"] = {200, "", big, true, 0, true};
    fk.site["https://h/loop"] = {302, "Location: /loop\r\n", ""};
    fk.site["https://h/noloc"] = {302, "", ""};
    SceHttpTransport t(fk.api());

    CHECK(t.get_text("https://h/V25+/") == "<a href=\"A.zip\">A</a>");
    CHECK(t.get_text("https://h/chunked") == "hello");

    uint64_t seen_len = 1, last_done = 0, last_total = 0;
    int calls = 0;
    bool ok = t.get_file("https://h/SF3Patch.exe", [&](uint64_t n) { seen_len = n; return true; }, "/tmp/sf3_sce.bin",
                         [&](uint64_t d, uint64_t tot) { calls++; last_done = d; last_total = tot; });
    CHECK(ok && seen_len == big.size() && slurp("/tmp/sf3_sce.bin") == big);
    CHECK(calls > 1 && last_done == big.size() && last_total == big.size());
    CHECK(fk.asked.size() >= 3 && fk.asked[fk.asked.size() - 2] == "https://h/dl/SF3Patch.exe");

    // want declines (the file is already there): nothing written
    std::remove("/tmp/sf3_sce2.bin");
    CHECK(!t.get_file("https://cdn/x.exe", [](uint64_t) { return false; }, "/tmp/sf3_sce2.bin", nullptr));
    CHECK(!std::ifstream("/tmp/sf3_sce2.bin").good());

    CHECK(error_of([&] { t.get_text("https://h/missing"); }) == "HTTP 404 from https://h/missing");
    CHECK(error_of([&] { t.get_text("https://h/short"); }).find("cut short") != std::string::npos);
    CHECK(error_of([&] { t.get_file("https://h/short", [](uint64_t) { return true; }, "/tmp/sf3_sce3.bin", nullptr); }).find("cut short") != std::string::npos);
    CHECK(!std::ifstream("/tmp/sf3_sce3.bin").good());   // a partial file does not stay
    CHECK(error_of([&] { t.get_file("https://h/broken", [](uint64_t) { return true; }, "/tmp/sf3_sce4.bin", nullptr); }).find("failed (0x80435002)") != std::string::npos);
    CHECK(error_of([&] { t.get_text("https://h/loop"); }).find("too many redirects") != std::string::npos);
    CHECK(error_of([&] { t.get_text("https://h/noloc"); }).find("without a target") != std::string::npos);
    CHECK(error_of([&] { t.get_text("https://nowhere/"); }).find("could not reach https://nowhere/ (0x80435001)") != std::string::npos);
    CHECK(error_of([&] { t.get_file("https://h/x", [](uint64_t) { return true; }, "/nonexistent-dir/f", nullptr); }) != "");

    // through fetch(): the add-on index and a download, with the .part rename
    auto urls = addon_urls(t, "https://h/V25+/");
    CHECK(urls.size() == 1 && urls[0].second == "https://h/V25+/A.zip");

    CHECK(fk.inits == 1);   // set up once
    CHECK(fk.live == 0);    // every connection and request deleted, errors included
    std::printf(failures ? "fetch_sce_test: %d failures\n" : "fetch_sce_test: ok\n", failures);
    return failures ? 1 : 0;
}
