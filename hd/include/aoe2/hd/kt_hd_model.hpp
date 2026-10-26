#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoe2::hd {

class KtHdModel {
public:
    explicit KtHdModel(aoe2::runtime::GuestAccess& memory);

    int32_t TableLookup(uint32_t table_addr, int32_t count, int32_t key);
    int32_t SetFlag(int32_t flag);
    int32_t GetFlag() const;
    int32_t ReadAndClearRegister();
    void SetHostname(std::string_view name);

private:
    aoe2::runtime::GuestAccess& memory_;
    std::string hostname_;
    static constexpr uint32_t kFlagAddr = 0x004EA180;
    static constexpr uint32_t kRegAddr = 0x004ED300 + 0x16 * 4;
};

}  // namespace aoe2::hd
