// MIT, see LICENSE. Port of sfiii-native tools/sf3patch/netjson.py.
#include "netjson.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sf3::netjson {
using namespace dotnet;

const Value* Value::get(const U32& k) const {
    for (auto& [n, v] : obj) if (n == k) return &v;
    return nullptr;
}

namespace {

// Arbitrary-precision non-negative integer as decimal text.
std::string dec_mul_add(const std::string& d, int mul, int add) {
    std::string out(d.size(), '0');
    int carry = add;
    for (size_t k = d.size(); k-- > 0;) {
        int v = (d[k] - '0') * mul + carry;
        out[k] = char('0' + v % 10);
        carry = v / 10;
    }
    while (carry) { out.insert(out.begin(), char('0' + carry % 10)); carry /= 10; }
    size_t nz = out.find_first_not_of('0');
    return nz == std::string::npos ? "0" : out.substr(nz);
}
void set_int(Value& v, bool neg, const std::string& digits_dec) {
    v.kind = Value::Int;
    v.dec = (neg && digits_dec != "0" ? "-" : "") + digits_dec;
    errno = 0;
    char* end;
    long long x = strtoll(v.dec.c_str(), &end, 10);
    v.fits = errno == 0 && *end == 0;
    v.i = x;
}
bool parse_base(const U32& t, int base, std::string& dec) {
    // Python int(text, base): optional sign, digits with single underscores between them.
    size_t i = 0;
    if (t.empty()) return false;
    dec = "0";
    bool any = false, last_us = false;
    for (; i < t.size(); i++) {
        char32_t c = t[i];
        if (c == U'_') { if (!any || last_us) return false; last_us = true; continue; }
        int d = c >= U'0' && c <= U'9' ? int(c - U'0') : c >= U'a' && c <= U'z' ? int(c - U'a' + 10)
              : c >= U'A' && c <= U'Z' ? int(c - U'A' + 10) : 99;
        if (d >= base) return false;
        dec = dec_mul_add(dec, base, d);
        any = true;
        last_us = false;
    }
    return any && !last_us;
}
bool py_int(const U32& text0, int base, Value& out) {
    U32 t = python_strip(text0);
    bool neg = false;
    if (!t.empty() && (t[0] == U'+' || t[0] == U'-')) { neg = t[0] == U'-'; t = t.substr(1); }
    if (base == 16 && t.size() >= 2 && t[0] == U'0' && (t[1] == U'x' || t[1] == U'X')) t = t.substr(2);
    if (base == 8 && t.size() >= 2 && t[0] == U'0' && (t[1] == U'o' || t[1] == U'O')) t = t.substr(2);
    std::string dec;
    if (!parse_base(t, base, dec)) return false;
    set_int(out, neg, dec);
    return true;
}
bool py_float(const U32& text, double& out) {
    // float(text) for the characters the number scanner allows (ASCII only).
    std::string a = utf8_encode(python_strip(text));
    if (a.empty()) return false;
    for (char c : a) if (!strchr("0123456789.eE+-_", c)) return false;
    std::string b;
    for (size_t k = 0; k < a.size(); k++) {
        if (a[k] == '_') {
            if (k == 0 || k + 1 >= a.size() || !isdigit((unsigned char)a[k - 1]) || !isdigit((unsigned char)a[k + 1])) return false;
            continue;
        }
        b += a[k];
    }
    char* end;
    errno = 0;
    out = strtod(b.c_str(), &end);
    if (*end) return false;
    // strtod accepts hex floats and "inf"/"nan"; those characters are not allowed above.
    return true;
}

bool is_alnum_ident(char32_t c) {
    if ((c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
    if (c < 0x80) return false;
    if (is_white_space(c)) return false;
    if (c >= 0x2000 && c <= 0x206F) return false;   // general punctuation
    if (c == 0xA7 || c == 0xA9 || c == 0xAB || c == 0xAE || c == 0xB0 || c == 0xB1 || c == 0xB6 || c == 0xB7 || c == 0xBB ||
        c == 0xBF || c == 0xD7 || c == 0xF7 || c == 0xA1 || c == 0xA2 || c == 0xA3 || c == 0xA4 || c == 0xA5 || c == 0xA6 ||
        c == 0xA8 || c == 0xAC || c == 0xAD || c == 0xAF || c == 0xB4 || c == 0xB8)
        return false;
    return true;
}

struct Reader {
    const U32& s;
    size_t i = 0;
    explicit Reader(const U32& t) : s(t) {}

    [[noreturn]] void error(const std::string& msg) {
        size_t line = 1;
        for (size_t k = 0; k < i && k < s.size(); k++) if (s[k] == U'\n') line++;
        throw JsonError(msg + " (line " + std::to_string(line) + ", position " + std::to_string(i) + ")");
    }
    char32_t peek() { return i < s.size() ? s[i] : 0; }
    bool at_end() { return i >= s.size(); }
    bool starts(const char32_t* w) {
        size_t n = std::char_traits<char32_t>::length(w);
        return s.compare(i, n, w) == 0;
    }
    std::string ch(char32_t c) { return utf8_encode(U32(1, c)); }
    void skip() {
        while (i < s.size()) {
            char32_t c = s[i];
            if (is_white_space(c) || c == 0xFEFF) i++;
            else if (starts(U"//")) { while (i < s.size() && s[i] != U'\r' && s[i] != U'\n') i++; }
            else if (starts(U"/*")) {
                size_t j = s.find(U"*/", i + 2);
                if (j == U32::npos) error("Unexpected end while parsing comment");
                i = j + 2;
            } else break;
        }
    }
    Value value() {
        skip();
        if (at_end()) error("Unexpected end when reading JSON");
        char32_t c = peek();
        if (c == U'{') return obj();
        if (c == U'[') return arr();
        if (c == U'"' || c == U'\'') { Value v; v.kind = Value::String; v.s = str(); return v; }
        if ((c >= U'0' && c <= U'9') || c == U'-' || c == U'.') return number();
        Value v;
        if (starts(U"true")) { i += 4; v.kind = Value::Bool; v.b = true; return v; }
        if (starts(U"false")) { i += 5; v.kind = Value::Bool; v.b = false; return v; }
        if (starts(U"null")) { i += 4; v.kind = Value::Null; return v; }
        if (starts(U"undefined")) { i += 9; v.kind = Value::Undefined; return v; }
        if (starts(U"NaN")) { i += 3; v.kind = Value::Float; v.f = NAN; return v; }
        if (starts(U"Infinity")) { i += 8; v.kind = Value::Float; v.f = INFINITY; return v; }
        error("Unexpected character encountered while parsing value: " + ch(c));
    }
    Value obj() {
        i++;
        Value out;
        out.kind = Value::Object;
        for (;;) {
            skip();
            char32_t c = peek();
            if (!at_end() && c == U'}') { i++; return out; }
            U32 key;
            if (!at_end() && (c == U'"' || c == U'\'')) key = str();
            else {
                size_t j = i;
                while (i < s.size() && (is_alnum_ident(s[i]) || s[i] == U'_' || s[i] == U'$')) i++;
                if (j == i) error("Invalid property identifier character: " + (at_end() ? std::string("EOF") : ch(c)));
                key = s.substr(j, i - j);
            }
            skip();
            if (at_end() || peek() != U':') error("Invalid character after parsing property name. Expected ':'");
            i++;
            Value v = value();
            bool found = false;
            for (auto& [k, old] : out.obj) if (k == key) { old = std::move(v); found = true; break; }
            if (!found) out.obj.emplace_back(key, std::move(v));
            skip();
            c = peek();
            if (!at_end() && c == U',') i++;
            else if (!at_end() && c == U'}') { i++; return out; }
            else error("After parsing a value an unexpected character was encountered: " + (at_end() ? std::string("EOF") : ch(c)));
        }
    }
    Value arr() {
        i++;
        Value out;
        out.kind = Value::Array;
        for (;;) {
            skip();
            if (!at_end() && peek() == U']') { i++; return out; }
            out.arr.push_back(value());
            skip();
            char32_t c = peek();
            if (!at_end() && c == U',') i++;
            else if (!at_end() && c == U']') { i++; return out; }
            else error("After parsing a value an unexpected character was encountered: " + (at_end() ? std::string("EOF") : ch(c)));
        }
    }
    U32 str() {
        char32_t quote = s[i++];
        U32 out;
        for (;;) {
            if (i >= s.size()) error("Unterminated string. Expected delimiter: " + ch(quote));
            char32_t c = s[i++];
            if (c == quote) return out;
            if (c != U'\\') { out += c; continue; }
            if (i >= s.size()) error("Unterminated string");
            char32_t e = s[i++];
            switch (e) {
            case U'b': out += U'\b'; break;
            case U't': out += U'\t'; break;
            case U'n': out += U'\n'; break;
            case U'f': out += U'\f'; break;
            case U'r': out += U'\r'; break;
            case U'\\': out += U'\\'; break;
            case U'/': out += U'/'; break;
            case U'"': out += U'"'; break;
            case U'\'': out += U'\''; break;
            case U'u': {
                if (i + 4 > s.size()) error("Invalid Unicode escape sequence");
                uint32_t v = 0;
                for (int k = 0; k < 4; k++) {
                    char32_t h = s[i + size_t(k)];
                    int d = h >= U'0' && h <= U'9' ? int(h - U'0') : h >= U'a' && h <= U'f' ? int(h - U'a' + 10)
                          : h >= U'A' && h <= U'F' ? int(h - U'A' + 10) : -1;
                    if (d < 0) error("Invalid Unicode escape sequence");
                    v = v * 16 + uint32_t(d);
                }
                out += char32_t(v);
                i += 4;
                break;
            }
            default: error("Bad JSON escape sequence: \\" + ch(e));
            }
        }
    }
    Value number() {
        size_t j = i;
        auto ok = [](char32_t c) { return c < 0x80 && strchr("0123456789.eE+-xXabcdefABCDEF", int(c)) && c != 0; };
        while (i < s.size() && ok(s[i])) i++;
        U32 text = s.substr(j, i - j);
        Value v;
        if (text == U"-" && starts(U"Infinity")) { i += 8; v.kind = Value::Float; v.f = -INFINITY; return v; }
        std::string bad = "Input string '" + utf8_encode(text) + "' is not a valid number";
        if (text[0] == U'0' && text.size() > 1 && text[1] != U'.' && text[1] != U'e' && text[1] != U'E') {
            bool good = (text[1] == U'x' || text[1] == U'X') ? py_int(text.substr(2), 16, v) : py_int(text, 8, v);
            if (!good) error(bad);
            return v;
        }
        if (text.find_first_of(U".eE") != U32::npos) {
            v.kind = Value::Float;
            if (!py_float(text, v.f)) error(bad);
            return v;
        }
        if (!py_int(text, 10, v)) error(bad);
        return v;
    }
};

// Python repr(float).
std::string float_repr(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    int p;
    for (p = 1; p <= 17; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, v);
        if (strtod(buf, nullptr) == v) break;
    }
    // buf is d.ddde[+-]XX: split into digits and exponent
    std::string m = buf;
    size_t e = m.find('e');
    int exp = atoi(m.c_str() + e + 1);
    std::string mant = m.substr(0, e);
    bool neg = mant[0] == '-';
    if (neg) mant = mant.substr(1);
    std::string digits;
    for (char c : mant) if (c != '.') digits += c;
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out;
    if (exp < -4 || exp >= 16) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char eb[16];
        snprintf(eb, sizeof eb, "e%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
        out += eb;
    } else if (exp < 0) {
        out = "0." + std::string(size_t(-exp - 1), '0') + digits;
    } else {
        if (digits.size() <= size_t(exp) + 1) out = digits + std::string(size_t(exp) + 1 - digits.size(), '0') + ".0";
        else out = digits.substr(0, size_t(exp) + 1) + "." + digits.substr(size_t(exp) + 1);
    }
    return (neg ? "-" : "") + out;
}

// json.dumps(v, indent=2) (ensure_ascii, Python's separators).
void dumps(const Value& v, int depth, std::string& o) {
    auto ind = [&](int d) { o += "\n" + std::string(size_t(2 * d), ' '); };
    auto qs = [&](const U32& s) {
        o += '"';
        for (char32_t c : s) {
            switch (c) {
            case U'"': o += "\\\""; break;
            case U'\\': o += "\\\\"; break;
            case U'\n': o += "\\n"; break;
            case U'\r': o += "\\r"; break;
            case U'\t': o += "\\t"; break;
            case U'\b': o += "\\b"; break;
            case U'\f': o += "\\f"; break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    char b[16];
                    if (c > 0xFFFF) {
                        uint32_t x = c - 0x10000;
                        snprintf(b, sizeof b, "\\u%04x\\u%04x", 0xD800 + (x >> 10), 0xDC00 + (x & 0x3FF));
                    } else snprintf(b, sizeof b, "\\u%04x", unsigned(c));
                    o += b;
                } else o += char(c);
            }
        }
        o += '"';
    };
    switch (v.kind) {
    case Value::Null: case Value::Undefined:
        if (v.kind == Value::Undefined) throw JsonError("Object of type Undefined is not JSON serializable");
        o += "null"; break;
    case Value::Bool: o += v.b ? "true" : "false"; break;
    case Value::Int: o += v.dec; break;
    case Value::Float:
        o += std::isnan(v.f) ? "NaN" : std::isinf(v.f) ? (v.f > 0 ? "Infinity" : "-Infinity") : float_repr(v.f); break;
    case Value::String: qs(v.s); break;
    case Value::Array:
        if (v.arr.empty()) { o += "[]"; break; }
        o += "[";
        for (size_t k = 0; k < v.arr.size(); k++) { if (k) o += ","; ind(depth + 1); dumps(v.arr[k], depth + 1, o); }
        ind(depth); o += "]"; break;
    case Value::Object:
        if (v.obj.empty()) { o += "{}"; break; }
        o += "{";
        for (size_t k = 0; k < v.obj.size(); k++) {
            if (k) o += ",";
            ind(depth + 1); qs(v.obj[k].first); o += ": "; dumps(v.obj[k].second, depth + 1, o);
        }
        ind(depth); o += "}"; break;
    }
}

U32 decode_utf16(const uint8_t* p, size_t n, bool be) {
    U32 out;
    size_t i = 0;
    while (i + 1 < n) {
        uint32_t u = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
        i += 2;
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < n) {
                uint32_t l = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
                if (l >= 0xDC00 && l <= 0xDFFF) { out += char32_t(0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00)); i += 2; continue; }
            }
            out += U'\uFFFD';
        } else if (u >= 0xDC00 && u <= 0xDFFF) out += U'\uFFFD';
        else out += char32_t(u);
    }
    if (i < n) out += U'\uFFFD';
    return out;
}
U32 decode_utf32(const uint8_t* p, size_t n, bool be) {
    U32 out;
    size_t i = 0;
    for (; i + 3 < n; i += 4) {
        uint32_t u = be ? (uint32_t(p[i]) << 24 | p[i + 1] << 16 | p[i + 2] << 8 | p[i + 3])
                        : (uint32_t(p[i + 3]) << 24 | p[i + 2] << 16 | p[i + 1] << 8 | p[i]);
        out += (u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) ? U'\uFFFD' : char32_t(u);
    }
    if (i < n) out += U'\uFFFD';
    return out;
}
}  // namespace

U32 decode_bom_text(const std::vector<uint8_t>& d) {
    auto starts = [&](const char* b, size_t n) { return d.size() >= n && memcmp(d.data(), b, n) == 0; };
    if (starts("\xEF\xBB\xBF", 3)) return utf8_decode(d.data() + 3, d.size() - 3);
    if (starts("\xFF\xFE\x00\x00", 4)) return decode_utf32(d.data() + 4, d.size() - 4, false);
    if (starts("\x00\x00\xFE\xFF", 4)) return decode_utf32(d.data() + 4, d.size() - 4, true);
    if (starts("\xFF\xFE", 2)) return decode_utf16(d.data() + 2, d.size() - 2, false);
    if (starts("\xFE\xFF", 2)) return decode_utf16(d.data() + 2, d.size() - 2, true);
    return utf8_decode(d.data(), d.size());
}
U32 read_text(const std::vector<uint8_t>& data) { return decode_bom_text(data); }

Value loads(const U32& text) {
    Reader r(text);
    Value v = r.value();
    r.skip();
    if (r.i != text.size()) r.error("Additional text encountered after finished reading JSON content: " + r.ch(r.peek()));
    return v;
}

std::optional<U32> to_net_string(const Value* v) {
    if (!v || v->kind == Value::Null || v->kind == Value::Undefined) return std::nullopt;
    switch (v->kind) {
    case Value::Bool: return utf8_decode(std::string(v->b ? "True" : "False"));
    case Value::Float: {
        if (std::isnan(v->f)) throw JsonError("cannot convert float NaN to integer");
        if (std::isinf(v->f)) throw JsonError("cannot convert float infinity to integer");
        if (v->f != std::trunc(v->f)) return utf8_decode(float_repr(v->f));
        char b[400];
        snprintf(b, sizeof b, "%.0f", v->f);
        std::string t = b;
        if (t == "-0") t = "0";
        return utf8_decode(t);
    }
    case Value::Object: case Value::Array: throw JsonError("Can not convert Object/Array to String");
    case Value::Int: return utf8_decode(v->dec);
    default: return v->s;
    }
}

static std::string py_repr(const Value& v) {
    switch (v.kind) {
    case Value::Null: return "None";
    case Value::Undefined: return "undefined";
    case Value::Bool: return v.b ? "True" : "False";
    case Value::Int: return v.dec;
    case Value::Float: return float_repr(v.f);
    case Value::String: return "'" + utf8_encode(v.s) + "'";
    case Value::Array: return "[...]";
    default: return "{...}";
    }
}

bool to_net_bool(const Value* v) {
    if (!v) throw JsonError("Cannot convert null to 'bool' because it is a non-nullable value type");
    if (v->kind == Value::Bool) return v->b;
    if (v->kind == Value::Int) return v->dec != "0";
    if (v->kind == Value::Float) return v->f != 0;
    if (v->kind == Value::String) {
        U32 t = lower(trim(v->s));
        if (t == U"true") return true;
        if (t == U"false") return false;
        throw JsonError("String '" + utf8_encode(v->s) + "' was not recognized as a valid Boolean.");
    }
    throw JsonError("Can not convert " + py_repr(*v) + " to Boolean.");
}

static int32_t int_range(const Value& v) {
    if (!v.fits || v.i < INT32_MIN || v.i > INT32_MAX) throw JsonError("Value was either too large or too small for an Int32.");
    return int32_t(v.i);
}

int32_t to_net_int(const Value* v) {
    if (!v) throw JsonError("Can not convert <missing> to Int32.");
    if (v->kind == Value::Bool) return v->b ? 1 : 0;
    if (v->kind == Value::Int) return int_range(*v);
    if (v->kind == Value::Float) {
        if (std::isnan(v->f)) throw JsonError("cannot convert float NaN to integer");
        if (std::isinf(v->f)) throw JsonError("cannot convert float infinity to integer");
        double r = std::nearbyint(v->f);   // round half to even (default rounding mode)
        if (r < -2147483648.0 || r > 2147483647.0) throw JsonError("Value was either too large or too small for an Int32.");
        return int32_t(r);
    }
    if (v->kind == Value::String) {
        Value tmp;
        if (!py_int(trim_number(v->s), 10, tmp)) throw JsonError("Input string was not in a correct format.");
        return int_range(tmp);
    }
    throw JsonError("Can not convert " + py_repr(*v) + " to Int32.");
}

U32 category_string(const Value* v) {
    if (!v) throw JsonError("Cannot perform runtime binding on a null reference");
    if (v->kind == Value::Object || v->kind == Value::Array) {
        std::string o;
        dumps(*v, 0, o);
        return utf8_decode(o);
    }
    auto s = to_net_string(v);
    return s ? *s : U32();
}

}  // namespace sf3::netjson
