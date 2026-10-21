#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aoe2::runtime {

class Deci2TtyPacket {
public:
    std::optional<std::string> append(std::span<const std::uint8_t> bytes);
    void clear();

private:
    std::vector<std::uint8_t> bytes_;
};

}
