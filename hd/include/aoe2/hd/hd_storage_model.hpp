#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoe2::hd {

class HdStorageModel {
public:
    explicit HdStorageModel(aoe2::runtime::GuestAccess& memory);

    bool Initialize(std::string_view root_path);
    int32_t ReadFile(std::string_view filename, uint32_t dest_guest_addr, int32_t size, int32_t offset);
    int32_t FileExists(std::string_view filename) const;

private:
    aoe2::runtime::GuestAccess& memory_;
    std::string root_path_;
    std::string ResolvePath(std::string_view filename) const;
};

}  // namespace aoe2::hd
