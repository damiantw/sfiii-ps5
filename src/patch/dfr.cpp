// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/dfr.py.
#include "dfr.h"
#include "dotnet.h"
#include "netjson.h"
#include <cstdio>

namespace sf3::dfr {
using namespace dotnet;

static std::vector<U32> lines(const U32& t) {
    std::vector<U32> out;
    size_t i = 0, n = t.size();
    while (i < n) {
        size_t j = i;
        while (j < n && t[j] != U'\r' && t[j] != U'\n') j++;
        out.push_back(t.substr(i, j - i));
        if (j < n && t[j] == U'\r' && j + 1 < n && t[j + 1] == U'\n') j++;
        i = j + 1;
    }
    return out;
}

static std::vector<uint8_t> pairs(const U32& chars, const std::string& name, int line) {
    std::vector<uint8_t> out;
    for (size_t k = 0; k < chars.size() / 2; k++) {
        try {
            out.push_back(parse_hex_byte(chars.substr(2 * k, 2)));
        } catch (const std::exception& e) {
            throw DFRError(name + " line " + std::to_string(line) + ": " + e.what());
        }
    }
    return out;
}

static std::vector<U32> split_comma(const U32& s) {
    std::vector<U32> out;
    size_t st = 0;
    for (;;) {
        size_t c = s.find(U',', st);
        if (c == U32::npos) { out.push_back(s.substr(st)); return out; }
        out.push_back(s.substr(st, c - st));
        st = c + 1;
    }
}

Patch parse(const std::vector<uint8_t>& data, const std::string& name) {
    Patch p;
    p.name = name;
    int num = 0;
    for (const U32& raw : lines(netjson::decode_bom_text(data))) {
        num++;
        U32 text = raw.substr(0, raw.find(U';'));
        if (trim(text).empty()) continue;
        auto f = split_comma(text);
        int64_t offset;
        try {
            offset = parse_hex_int(f[0]);
        } catch (const std::exception& e) {
            throw DFRError(name + " line " + std::to_string(num) + ": bad offset: " + e.what());
        }
        if (f.size() < 2)
            throw DFRError(name + " line " + std::to_string(num) + ": missing old bytes (Index was outside the bounds of the array)");
        U32 old = trim(f[1]);
        if (f.size() <= 2) { p.ops.push_back({Op::Truncate, num, offset, {}, {}}); break; }
        U32 nw = trim(f[2]);
        if (old.empty()) { p.ops.push_back({Op::Append, num, offset, {}, pairs(nw, name, num)}); continue; }
        if (old.size() != nw.size())
            throw DFRError("Error with patch file " + name + ".\nLengths are different. (line " + std::to_string(num) + ")");
        p.ops.push_back({Op::Replace, num, offset, pairs(old, name, num), pairs(nw, name, num)});
    }
    return p;
}

std::vector<uint8_t> apply(const Patch& p, std::vector<uint8_t> buf, const std::string& target) {
    for (const Op& op : p.ops) {
        if (op.offset < 0) throw DFRError(p.name + " line " + std::to_string(op.line) + ": negative offset");
        size_t off = size_t(op.offset);
        if (op.kind == Op::Truncate) {
            if (off > buf.size())
                throw DFRError(p.name + " line " + std::to_string(op.line) + ": cannot truncate " + target + " (" +
                               std::to_string(buf.size()) + " bytes) to " + std::to_string(off) + " bytes");
            buf.resize(off);
            break;
        }
        if (op.kind == Op::Append) {
            if (buf.size() <= off) buf.resize(off, 0);
            buf.insert(buf.end(), op.new_.begin(), op.new_.end());
            continue;
        }
        for (size_t j = 0; j < op.old_.size() && j < op.new_.size(); j++) {
            size_t pos = off + j;
            if (buf.size() <= pos) { buf.push_back(op.new_[j]); continue; }
            if (buf[pos] != op.old_[j]) {
                char m[160];
                snprintf(m, sizeof m, "Old value %X is different than %X on line %d(%llX).", op.old_[j], buf[pos], op.line,
                         (unsigned long long)op.offset);
                throw DFRError("Error with patch file " + target + ".\n" + m);
            }
            buf[pos] = op.new_[j];
        }
    }
    return buf;
}

}  // namespace sf3::dfr
