// The JSON dialect Newtonsoft.Json accepts (comments, single quotes, unquoted names, trailing
// commas, leading-zero octal/hex numbers), and the dynamic conversions the wizard applies to
// metadata.json values. Port of sfiii-native tools/sf3patch/netjson.py (MIT).
#pragma once
#include "dotnet.h"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sf3::netjson {

using dotnet::U32;

struct JsonError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Value {
    enum Kind { Null, Undefined, Bool, Int, Float, String, Array, Object } kind = Null;
    bool b = false;
    // Int: Python ints are unbounded; `dec` is the exact decimal text, `i` is set when it fits.
    std::string dec;
    bool fits = false;
    int64_t i = 0;
    double f = 0;
    U32 s;
    std::vector<Value> arr;
    std::vector<std::pair<U32, Value>> obj;   // insertion order; a repeated name keeps its first place
    const Value* get(const U32& k) const;
};

Value loads(const U32& text);
U32 read_text(const std::vector<uint8_t>& data);   // File.ReadAllText (BOM-aware, UTF-8 default)

// The conversions; `v == nullptr` is a missing property (C# null from a dynamic lookup).
std::optional<U32> to_net_string(const Value* v);
bool to_net_bool(const Value* v);
int32_t to_net_int(const Value* v);
U32 category_string(const Value* v);

U32 decode_bom_text(const std::vector<uint8_t>& data);   // StreamReader's default decoding

}  // namespace sf3::netjson
