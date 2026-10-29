// MIT, see LICENSE.
#include "fetch_sce.h"
#include <cstdio>
#include <strings.h>

namespace sf3::fetch {

static std::string hex(int code) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", unsigned(code));
    return b;
}

struct SceHttpTransport::Open {
    SceHttpApi* api = nullptr;
    int conn = -1, req = -1;
    std::string url;
    bool known = false;
    uint64_t len = 0;
    Open() = default;
    Open(const Open&) = delete;
    Open(Open&& o) noexcept : api(o.api), conn(o.conn), req(o.req), url(std::move(o.url)), known(o.known), len(o.len) { o.req = o.conn = -1; }
    ~Open() { close(); }
    void close() {
        if (req >= 0 && api->delete_request) api->delete_request(req);
        if (conn >= 0 && api->delete_connection) api->delete_connection(conn);
        req = conn = -1;
    }
};

int SceHttpTransport::tmpl() {
    if (tmpl_ > 0) return tmpl_;
    int t = api_.init(USER_AGENT);
    if (t <= 0) throw FetchError("the console's network library did not start (" + hex(t) + ")");
    return tmpl_ = t;
}

static std::string location(const std::string& headers) {
    size_t pos = 0;
    while (pos < headers.size()) {
        size_t end = headers.find('\n', pos);
        if (end == std::string::npos) end = headers.size();
        std::string line = headers.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 9 && strncasecmp(line.c_str(), "location:", 9) == 0) {
            size_t v = 9;
            while (v < line.size() && line[v] == ' ') v++;
            return line.substr(v);
        }
        pos = end + 1;
    }
    return {};
}

SceHttpTransport::Open SceHttpTransport::open(const std::string& first) {
    std::string url = first;
    for (int hop = 0; hop < 6; hop++) {
        Open o;
        o.api = &api_, o.url = url;
        o.conn = api_.connect(tmpl(), url.c_str());
        if (o.conn < 0) throw FetchError("could not reach " + url + " (" + hex(o.conn) + ")");
        o.req = api_.request(o.conn, url.c_str());
        if (o.req < 0) throw FetchError("could not ask for " + url + " (" + hex(o.req) + ")");
        if (int e = api_.send(o.req); e != 0) throw FetchError("could not reach " + url + " (" + hex(e) + ")");
        int st = 0;
        if (int e = api_.status(o.req, &st); e != 0) throw FetchError("no answer from " + url + " (" + hex(e) + ")");
        if (st == 301 || st == 302 || st == 303 || st == 307 || st == 308) {
            std::string h;
            if (api_.headers) api_.headers(o.req, &h);
            std::string to = location(h);
            if (to.empty()) throw FetchError("a redirect without a target from " + url);
            url = urljoin(url, to);
            continue;
        }
        if (st != 200) throw FetchError("HTTP " + std::to_string(st) + " from " + url);
        int known = 1;
        uint64_t len = 0;
        if (api_.content_length && api_.content_length(o.req, &known, &len) == 0 && known == 0) o.known = true, o.len = len;
        return o;
    }
    throw FetchError("too many redirects from " + first);
}

std::string SceHttpTransport::get_text(const std::string& url) {
    Open o = open(url);
    std::string out;
    char buf[16384];
    for (;;) {
        int n = api_.read(o.req, buf, sizeof buf);
        if (n < 0) throw FetchError("could not read " + url + " (" + hex(n) + ")");
        if (n == 0) break;
        out.append(buf, size_t(n));
    }
    if (o.known && out.size() != o.len) throw FetchError("the answer from " + url + " was cut short");
    return out;
}

bool SceHttpTransport::get_file(const std::string& url, const std::function<bool(uint64_t)>& want, const std::string& path,
                                const std::function<void(uint64_t, uint64_t)>& progress) {
    Open o = open(url);
    if (!want(o.known ? o.len : 0)) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) throw FetchError("cannot write " + path);
    uint64_t done = 0;
    char buf[65536];
    for (;;) {
        int n = api_.read(o.req, buf, sizeof buf);
        if (n < 0) { std::fclose(f); std::remove(path.c_str()); throw FetchError("download of " + url + " failed (" + hex(n) + ")"); }
        if (n == 0) break;
        if (std::fwrite(buf, 1, size_t(n), f) != size_t(n)) { std::fclose(f); std::remove(path.c_str()); throw FetchError("cannot write " + path + " (disk full?)"); }
        done += uint64_t(n);
        if (progress) progress(done, o.known ? o.len : 0);
    }
    if (std::fclose(f) != 0) { std::remove(path.c_str()); throw FetchError("cannot write " + path); }
    if (o.known && done != o.len) { std::remove(path.c_str()); throw FetchError("download of " + url + " was cut short"); }
    return true;
}

#if defined(SF3_PS5_CONSOLE)
// libSceNet / libSceSsl / libSceHttp, declared from the public API shape.
extern "C" {
int sceNetInit(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceSslInit(size_t pool);
int sceHttpInit(int net_pool, int ssl_ctx, size_t pool);
int sceHttpCreateTemplate(int http_ctx, const char* agent, int http_ver, int auto_proxy);
int sceHttpSetAutoRedirect(int id, int enable);
int sceHttpSetConnectTimeOut(int id, uint32_t usec);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);
int sceHttpSetResponseHeaderMaxSize(int id, size_t size);
int sceHttpCreateConnectionWithURL(int tmpl, const char* url, int keep_alive);
int sceHttpCreateRequestWithURL(int conn, int method, const char* url, uint64_t content_length);
int sceHttpSendRequest(int req, const void* data, size_t size);
int sceHttpGetStatusCode(int req, int* status);
int sceHttpGetResponseContentLength(int req, int* known, uint64_t* len);
int sceHttpGetAllResponseHeaders(int req, char** header, size_t* size);
int sceHttpReadData(int req, void* data, size_t size);
int sceHttpDeleteRequest(int req);
int sceHttpDeleteConnection(int conn);
}

SceHttpApi sce_http_api() {
    SceHttpApi a;
    a.init = [](const char* agent) -> int {
        static int tmpl = 0;
        if (tmpl > 0) return tmpl;
        int e = sceNetInit();
        if (e < 0) return e;
        int pool = sceNetPoolCreate("sf3fetch", 64 * 1024, 0);
        if (pool < 0) return pool;
        int ssl = sceSslInit(256 * 1024);
        if (ssl < 0) return ssl;
        int http = sceHttpInit(pool, ssl, 256 * 1024);
        if (http < 0) return http;
        int t = sceHttpCreateTemplate(http, agent, 2 /* HTTP/1.1 */, 1);
        if (t < 0) return t;
        sceHttpSetAutoRedirect(t, 0);   // redirects are followed (and capped) here
        sceHttpSetConnectTimeOut(t, 30u * 1000 * 1000);
        sceHttpSetRecvTimeOut(t, 60u * 1000 * 1000);
        sceHttpSetResponseHeaderMaxSize(t, 16 * 1024);
        return tmpl = t;
    };
    a.connect = [](int t, const char* url) { return sceHttpCreateConnectionWithURL(t, url, 1); };
    a.request = [](int c, const char* url) { return sceHttpCreateRequestWithURL(c, 0 /* GET */, url, 0); };
    a.send = [](int r) { return sceHttpSendRequest(r, nullptr, 0); };
    a.status = [](int r, int* s) { return sceHttpGetStatusCode(r, s); };
    a.content_length = [](int r, int* k, uint64_t* l) { return sceHttpGetResponseContentLength(r, k, l); };
    a.headers = [](int r, std::string* h) {
        char* p = nullptr;
        size_t n = 0;
        int e = sceHttpGetAllResponseHeaders(r, &p, &n);
        if (e == 0 && p) h->assign(p, n);
        return e;
    };
    a.read = [](int r, void* b, size_t n) { return sceHttpReadData(r, b, n); };
    a.delete_request = [](int r) { sceHttpDeleteRequest(r); };
    a.delete_connection = [](int c) { sceHttpDeleteConnection(c); };
    return a;
}

std::unique_ptr<Transport> sce_http_transport() { return std::make_unique<SceHttpTransport>(sce_http_api()); }
#endif

}  // namespace sf3::fetch
