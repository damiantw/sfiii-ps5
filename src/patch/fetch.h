// Opt-in download of the translation project's files from its own server (never bundled or
// redistributed). Port of sfiii-native tools/sf3patch/fetch.py (MIT). The HTTP transport is
// pluggable: the PC test tool runs `curl`, the console build uses the system HTTP library.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sf3::fetch {

constexpr const char* SITE = "https://sf3transftp.shiningforcecentral.com/";
constexpr const char* WIZARD = "https://sf3transftp.shiningforcecentral.com/SF3Patch.exe";
constexpr const char* ADDONS = "https://sf3transftp.shiningforcecentral.com/V25+/";
constexpr const char* USER_AGENT = "sf3patch (sfiii-native)";
extern const char* const SKIP[];   // not patches (the wizard's Windows scaling fix)

struct FetchError : std::runtime_error { using std::runtime_error::runtime_error; };

class Transport {
public:
    virtual ~Transport() = default;
    virtual std::string get_text(const std::string& url) = 0;
    // GET url; once the headers are in, `want(content_length or 0)` decides whether to keep
    // going. The body is written to `path`. Returns false when `want` said no.
    virtual bool get_file(const std::string& url, const std::function<bool(uint64_t)>& want, const std::string& path,
                          const std::function<void(uint64_t, uint64_t)>& progress) = 0;
};
std::unique_ptr<Transport> curl_transport();   // PC only: runs the curl command

std::vector<std::string> html_links(const std::string& page);          // <a href> values, unescaped
std::string urljoin(const std::string& base, const std::string& ref);  // RFC 3986 reference resolution
std::string unquote(const std::string& s);                             // %XX decoding (UTF-8)

std::vector<std::pair<std::string, std::string>> addon_urls(Transport& t, const std::string& index_url = ADDONS);

using Log = std::function<void(const std::string&)>;
// Saves SF3Patch.exe and every V25+ add-on zip under dest (dest/V25+/...); skips a file whose
// size already matches Content-Length. Returns the saved paths.
std::vector<std::string> fetch(Transport& t, const std::string& dest, bool wizard, bool addons, const Log& log,
                               const std::function<void(const std::string&, uint64_t, uint64_t)>& progress = nullptr);

}  // namespace sf3::fetch
