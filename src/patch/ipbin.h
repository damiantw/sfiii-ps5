// Saturn IP.BIN header: reading it, and the wizard's rewrite. Port of sfiii-native
// tools/sf3patch/ipbin.py (MIT). Field values are .NET strings (code points).
#pragma once
#include "dotnet.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sf3::ipbin {

using dotnet::U32;

struct Header {
    U32 hardware, maker, product, version, date, device, areas, peripherals, title;
    U32 ip_size, stack_m, stack_s, read_addr, read_size;
};

constexpr const char* WIZARD_RELEASE_DATE = "20240825";

Header parse(const std::vector<uint8_t>& raw);
std::vector<uint8_t> serialize(const std::vector<uint8_t>& raw, const Header& h);
Header wizard_header(const Header& h, const U32& version, const U32& release_date);
std::vector<uint8_t> wizard_rewrite(const std::vector<uint8_t>& raw, const std::string& version,
                                    const std::string& release_date = WIZARD_RELEASE_DATE);

}  // namespace sf3::ipbin
