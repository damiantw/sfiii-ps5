// DFR byte patches (the translation project's text patch format). Port of
// sfiii-native tools/sf3patch/dfr.py (MIT); see that file for the wizard quirks reproduced.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf3::dfr {

struct DFRError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Op {
    enum Kind { Replace, Append, Truncate } kind;
    int line;
    int64_t offset;
    std::vector<uint8_t> old_, new_;
};

struct Patch {
    std::string name;
    std::vector<Op> ops;
};

Patch parse(const std::vector<uint8_t>& data, const std::string& name = "patch");
std::vector<uint8_t> apply(const Patch& p, std::vector<uint8_t> data, const std::string& target = "file");

}  // namespace sf3::dfr
