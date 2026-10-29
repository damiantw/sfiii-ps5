// SHA-1 and SHA-256 (FIPS 180-4), written from the standard. MIT, see LICENSE.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace sf3::disc {

class Sha1 {
public:
    Sha1();
    void update(const void* data, size_t n);
    std::string hex();   // finishes the hash
private:
    void block(const uint8_t* p);
    uint32_t h_[5];
    uint8_t buf_[64];
    uint64_t len_ = 0;
    size_t fill_ = 0;
};

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t n);
    std::string hex();
    void digest(uint8_t out[32]);
private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    uint64_t len_ = 0;
    size_t fill_ = 0;
};

std::string sha256_hex(const void* data, size_t n);

}  // namespace sf3::disc
