// Prints how the C++ patcher reads metadata.json and DFR inputs, for tests/patch_quirks.py to
// compare against sfiii-native's Python tools/sf3patch. MIT.
#include "patch/dfr.h"
#include "patch/netjson.h"
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace sf3;

static std::vector<uint8_t> slurp(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::string kind = argv[1];
    auto data = slurp(argv[2]);
    if (kind == "meta") {
        std::string o;
        auto P = [&](const std::string& k, const std::string& v) { o += k + "=" + v + "\n"; };
        try {
            auto doc = netjson::loads(netjson::read_text(data));
            if (doc.kind != netjson::Value::Object) throw netjson::JsonError("not an object");
            auto g = [&](const char* k) { return doc.get(dotnet::utf8_decode(std::string(k))); };
            P("category", dotnet::utf8_encode(dotnet::lower(netjson::category_string(g("category")))));
            auto t = netjson::to_net_string(g("text"));
            P("text", t ? dotnet::utf8_encode(*t) : "<None>");
            P("enabled", netjson::to_net_bool(g("enabled")) ? "1" : "0");
            auto pr = g("priority");
            if (pr && pr->kind == netjson::Value::Null) throw netjson::JsonError("null priority");
            P("priority", std::to_string(pr ? netjson::to_net_int(pr) : 9999));
            fputs(o.c_str(), stdout);
        } catch (const std::exception&) {
            printf("error\n");
        }
    } else {
        try {
            auto p = dfr::parse(data, "t");
            for (auto& op : p.ops) {
                printf("%d %lld %d ", op.line, (long long)op.offset, int(op.kind));
                for (auto b : op.old_) printf("%02x", b);
                printf(" ");
                for (auto b : op.new_) printf("%02x", b);
                printf("\n");
            }
            std::vector<uint8_t> buf(64);
            for (size_t i = 0; i < buf.size(); i++) buf[i] = uint8_t(i);
            try {
                auto out = dfr::apply(p, buf, "f");
                printf("applied %zu:", out.size());
                for (auto b : out) printf("%02x", b);
                printf("\n");
            } catch (const std::exception& e) { printf("apply error: %s\n", e.what()); }
        } catch (const std::exception&) {
            printf("parse error\n");
        }
    }
    return 0;
}
