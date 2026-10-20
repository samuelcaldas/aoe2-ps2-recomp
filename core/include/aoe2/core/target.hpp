#pragma once

#include <cstdint>
#include <string_view>

namespace aoe2::core {

struct TargetInfo {
    static constexpr std::string_view kSerial = "SLES-50282";
    static constexpr std::string_view kFilename = "AOK.ELF";
    static constexpr std::string_view kExpectedSha1 = "f478e19f2059d52899b691483ef5c7522e30e17c";
    static constexpr std::string_view kRegion = "PAL";
    static constexpr std::string_view kTitle = "Age of Empires II: The Age of Kings (PS2 PAL)";
    static constexpr std::string_view kDiscElfPath = "EN/AGE2_PS2.ELF";
};

} // namespace aoe2::core
