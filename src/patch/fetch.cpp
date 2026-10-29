// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/fetch.py.
#include "fetch.h"
#include "dotnet.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace sf3::fetch {

const char* const SKIP[] = {"SF3Translation_PatchWizard_WinScalingFix.zip", nullptr};

static bool skipped(const std::string& n) {
    for (int i = 0; SKIP[i]; i++) if (n == SKIP[i]) return true;
    return false;
}

static std::string unescape_html(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { o += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { o += s[i]; continue; }
        std::string e = s.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        if (e == "amp") cp = '&'; else if (e == "lt") cp = '<'; else if (e == "gt") cp = '>';
        else if (e == "quot") cp = '"'; else if (e == "apos") cp = '\'';
        else if (e.size() > 1 && e[0] == '#') cp = uint32_t(e[1] == 'x' || e[1] == 'X' ? strtoul(e.c_str() + 2, nullptr, 16) : strtoul(e.c_str() + 1, nullptr, 10));
        if (!cp) { o += s[i]; continue; }
        o += dotnet::utf8_encode(dotnet::U32(1, char32_t(cp)));
        i = semi;
    }
    return o;
}

std::vector<std::string> html_links(const std::string& page) {
    std::vector<std::string> out;
    size_t i = 0;
    while ((i = page.find('<', i)) != std::string::npos) {
        i++;
        if (i >= page.size() || tolower((unsigned char)page[i]) != 'a' || i + 1 >= page.size() ||
            !(isspace((unsigned char)page[i + 1]) || page[i + 1] == '>'))
            continue;
        size_t p = i + 1;
        while (p < page.size() && page[p] != '>') {
            while (p < page.size() && (isspace((unsigned char)page[p]) || page[p] == '/')) p++;
            size_t ks = p;
            while (p < page.size() && !isspace((unsigned char)page[p]) && page[p] != '=' && page[p] != '>') p++;
            std::string key = page.substr(ks, p - ks);
            for (auto& c : key) c = char(tolower((unsigned char)c));
            while (p < page.size() && isspace((unsigned char)page[p])) p++;
            std::string val;
            if (p < page.size() && page[p] == '=') {
                p++;
                while (p < page.size() && isspace((unsigned char)page[p])) p++;
                if (p < page.size() && (page[p] == '"' || page[p] == '\'')) {
                    char q = page[p++];
                    size_t e = page.find(q, p);
                    if (e == std::string::npos) e = page.size();
                    val = page.substr(p, e - p);
                    p = e + 1;
                } else {
                    size_t vs = p;
                    while (p < page.size() && !isspace((unsigned char)page[p]) && page[p] != '>') p++;
                    val = page.substr(vs, p - vs);
                }
            }
            if (key == "href" && !val.empty()) { out.push_back(unescape_html(val)); break; }
            if (key.empty() && p < page.size() && page[p] != '>') p++;
        }
    }
    return out;
}

std::string unquote(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += char(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else o += s[i];
    }
    return dotnet::utf8_encode(dotnet::utf8_decode(o));
}

namespace {
struct Url { std::string scheme, auth, path, query, frag; bool has_auth = false, has_query = false, has_frag = false; };
Url split(const std::string& s) {
    Url u;
    size_t p = 0;
    size_t c = s.find(':');
    if (c != std::string::npos && c > 0 && isalpha((unsigned char)s[0]) &&
        s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-.") == c) {
        u.scheme = s.substr(0, c);
        p = c + 1;
    }
    if (s.compare(p, 2, "//") == 0) {
        size_t e = s.find_first_of("/?#", p + 2);
        if (e == std::string::npos) e = s.size();
        u.auth = s.substr(p + 2, e - p - 2);
        u.has_auth = true;
        p = e;
    }
    size_t f = s.find('#', p);
    if (f != std::string::npos) { u.frag = s.substr(f + 1); u.has_frag = true; }
    std::string rest = s.substr(p, f == std::string::npos ? std::string::npos : f - p);
    size_t q = rest.find('?');
    if (q != std::string::npos) { u.query = rest.substr(q + 1); u.has_query = true; rest = rest.substr(0, q); }
    u.path = rest;
    return u;
}
std::string remove_dots(const std::string& in) {
    std::vector<std::string> segs;
    size_t st = 0;
    bool abs = !in.empty() && in[0] == '/';
    std::vector<std::string> parts;
    for (;;) {
        size_t sl = in.find('/', st);
        parts.push_back(in.substr(st, sl == std::string::npos ? std::string::npos : sl - st));
        if (sl == std::string::npos) break;
        st = sl + 1;
    }
    if (abs) parts.erase(parts.begin());
    for (size_t i = 0; i < parts.size(); i++) {
        const std::string& s = parts[i];
        bool last = i + 1 == parts.size();
        if (s == ".") { if (last) segs.push_back(""); continue; }
        if (s == "..") { if (!segs.empty()) segs.pop_back(); if (last) segs.push_back(""); continue; }
        segs.push_back(s);
    }
    std::string o = abs ? "/" : "";
    for (size_t i = 0; i < segs.size(); i++) o += (i ? "/" : "") + segs[i];
    return o;
}
std::string join(const Url& u) {
    std::string o;
    if (!u.scheme.empty()) o += u.scheme + ":";
    if (u.has_auth) o += "//" + u.auth;
    o += u.path;
    if (u.has_query) o += "?" + u.query;
    if (u.has_frag) o += "#" + u.frag;
    return o;
}
}  // namespace

std::string urljoin(const std::string& base, const std::string& ref) {
    if (ref.empty()) return base;
    Url b = split(base), r = split(ref), t;
    if (!r.scheme.empty()) { t = r; t.path = remove_dots(r.path); return join(t); }
    t.scheme = b.scheme;
    if (r.has_auth) { t.auth = r.auth; t.has_auth = true; t.path = remove_dots(r.path); t.query = r.query; t.has_query = r.has_query; }
    else {
        t.auth = b.auth; t.has_auth = b.has_auth;
        if (r.path.empty()) { t.path = b.path; t.query = r.has_query ? r.query : b.query; t.has_query = r.has_query || b.has_query; }
        else {
            if (r.path[0] == '/') t.path = remove_dots(r.path);
            else {
                std::string merged = b.has_auth && b.path.empty() ? "/" + r.path : b.path.substr(0, b.path.rfind('/') + 1) + r.path;
                t.path = remove_dots(merged);
            }
            t.query = r.query; t.has_query = r.has_query;
        }
    }
    t.frag = r.frag; t.has_frag = r.has_frag;
    return join(t);
}

std::vector<std::pair<std::string, std::string>> addon_urls(Transport& t, const std::string& index_url) {
    std::string page = dotnet::utf8_encode(dotnet::utf8_decode(t.get_text(index_url)));
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& href : html_links(page)) {
        std::string url = urljoin(index_url, href);
        std::string name = unquote(url.substr(url.rfind('/') + 1));
        std::string low = name;
        for (auto& c : low) c = char(tolower((unsigned char)c));
        if (low.size() >= 4 && low.compare(low.size() - 4, 4, ".zip") == 0 && !skipped(name) && url.rfind(index_url, 0) == 0)
            out.emplace_back(name, url);
    }
    return out;
}

static uint64_t file_size(const std::string& p, bool& exists) {
    struct stat st;
    exists = stat(p.c_str(), &st) == 0;
    return exists ? uint64_t(st.st_size) : 0;
}

static std::string download(Transport& t, const std::string& url, const std::string& dest, const Log& log,
                            const std::function<void(const std::string&, uint64_t, uint64_t)>& progress) {
    std::string name = dest.substr(dest.rfind('/') + 1);
    std::string tmp = dest + ".part";
    bool have = false;
    bool got = t.get_file(url, [&](uint64_t size) {
        bool exists;
        uint64_t cur = file_size(dest, exists);
        have = exists && size && cur == size;
        return !have;
    }, tmp, [&](uint64_t done, uint64_t total) { if (progress) progress(name, done, total); });
    if (!got && have) { log("have " + name); return dest; }
    if (!got) throw FetchError("download of " + url + " stopped");
    if (rename(tmp.c_str(), dest.c_str()) != 0) throw FetchError("cannot rename " + tmp);
    bool e;
    log("got  " + name + " (" + std::to_string(file_size(dest, e)) + " bytes)");
    return dest;
}

static void mkdirs(const std::string& p) {
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755);
}

std::vector<std::string> fetch(Transport& t, const std::string& dest, bool wizard, bool addons, const Log& log,
                               const std::function<void(const std::string&, uint64_t, uint64_t)>& progress) {
    mkdirs(dest + "/V25+");
    std::vector<std::string> got;
    if (wizard) got.push_back(download(t, WIZARD, dest + "/SF3Patch.exe", log, progress));
    if (addons)
        for (auto& [name, url] : addon_urls(t)) got.push_back(download(t, url, dest + "/V25+/" + name, log, progress));
    return got;
}

// ------------------------------------------------------------------ PC transport
namespace {
std::string shq(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}
class CurlCli : public Transport {
public:
    std::string get_text(const std::string& url) override {
        std::string cmd = "curl -sSfL --max-time 60 -A " + shq(USER_AGENT) + " " + shq(url);
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) throw FetchError("cannot run curl");
        std::string out;
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        if (pclose(p) != 0) throw FetchError("could not read " + url);
        return out;
    }
    bool get_file(const std::string& url, const std::function<bool(uint64_t)>& want, const std::string& path,
                  const std::function<void(uint64_t, uint64_t)>&) override {
        std::string cmd = "curl -sSfIL --max-time 60 -A " + shq(USER_AGENT) + " " + shq(url);
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) throw FetchError("cannot run curl");
        uint64_t len = 0;
        char line[1024];
        while (fgets(line, sizeof line, p))
            if (strncasecmp(line, "content-length:", 15) == 0) len = strtoull(line + 15, nullptr, 10);   // the last response's
        if (pclose(p) != 0) throw FetchError("could not reach " + url);
        if (!want(len)) return false;
        std::string get = "curl -sSfL --max-time 600 -A " + shq(USER_AGENT) + " -o " + shq(path) + " " + shq(url);
        if (system(get.c_str()) != 0) throw FetchError("download of " + url + " failed");
        return true;
    }
};
}  // namespace

std::unique_ptr<Transport> curl_transport() { return std::make_unique<CurlCli>(); }

}  // namespace sf3::fetch
