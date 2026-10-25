#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace aoe2::game_hooks::native_input {

class Arguments {
public:
    static Arguments FromRegisters(const std::array<uint32_t, 32>& registers) {
        Arguments arguments;
        for (std::size_t index = 0; index < arguments.values_.size(); ++index)
            arguments.values_[index] = registers[index + 4];
        return arguments;
    }

    uint32_t U(std::size_t index) const {
        return index < values_.size() ? values_[index] : 0;
    }

    int32_t I(std::size_t index) const {
        return static_cast<int32_t>(U(index));
    }

private:
    std::array<uint32_t, 8> values_{};
};

}  // namespace aoe2::game_hooks::native_input
