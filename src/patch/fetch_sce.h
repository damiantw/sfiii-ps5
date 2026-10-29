// fetch::Transport on the PS5's system HTTP library (libSceHttp over libSceSsl/libSceNet).
// The calls go through SceHttpApi so the logic (redirects, status, length checks, progress,
// cleanup) is tested on PC against a fake (tests/fetch_sce_test.cpp); sce_http_api() binds the
// real library in a console build (SF3_PS5_CONSOLE).
// HTTPS uses the system's certificate checks as they are (no callback that waives them).
// Name resolution is the console's own DNS: a console whose DNS is pointed at a blocker gets
// "could not reach" and the menu says to copy the files by hand instead. MIT, see LICENSE.
#pragma once
#include "fetch.h"
#include <functional>

namespace sf3::fetch {

struct SceHttpApi {
    // set up once: returns the template id (<0: error code)
    std::function<int(const char* user_agent)> init;
    std::function<int(int tmpl, const char* url)> connect;                 // connection id
    std::function<int(int conn, const char* url)> request;                 // GET request id
    std::function<int(int req)> send;                                       // 0 ok
    std::function<int(int req, int* status)> status;
    // known: 0 = Content-Length given (len set), else unknown (chunked or none)
    std::function<int(int req, int* known, uint64_t* len)> content_length;
    std::function<int(int req, std::string* headers)> headers;              // raw response headers
    std::function<int(int req, void* buf, size_t size)> read;              // bytes, 0 at end, <0 error
    std::function<void(int req)> delete_request;
    std::function<void(int conn)> delete_connection;
};

class SceHttpTransport : public Transport {
public:
    explicit SceHttpTransport(SceHttpApi api) : api_(std::move(api)) {}
    std::string get_text(const std::string& url) override;
    bool get_file(const std::string& url, const std::function<bool(uint64_t)>& want, const std::string& path,
                  const std::function<void(uint64_t, uint64_t)>& progress) override;
private:
    struct Open;   // an answered request (after redirects)
    Open open(const std::string& url);
    int tmpl();
    SceHttpApi api_;
    int tmpl_ = 0;   // 0: not set up yet
};

SceHttpApi sce_http_api();                      // console builds only
std::unique_ptr<Transport> sce_http_transport();   // console builds only

}  // namespace sf3::fetch
