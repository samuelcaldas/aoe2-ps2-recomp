#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>
#include <string>
#include <string_view>

namespace aoe2::mc {

struct KtMcFileInfo {
    char name[32]{};
    int32_t size{0};
    int32_t attr{0};
    uint8_t date[8]{};
};

class McStorageModel {
public:
    explicit McStorageModel(aoe2::runtime::GuestAccess& memory);

    bool Initialize(std::string_view root_path);
    int32_t GetDir(int32_t port, int32_t slot, std::string_view pattern, uint32_t out_entries, int32_t max_entries);
    int32_t ReadFile(int32_t port, int32_t slot, std::string_view filename, uint32_t dest_guest_addr);
    int32_t WriteFile(int32_t port, int32_t slot, std::string_view filename, uint32_t src_guest_addr, uint32_t size);
    int32_t DeleteFile(int32_t port, int32_t slot, std::string_view filename);

private:
    aoe2::runtime::GuestAccess& memory_;
    std::string root_path_;
    std::string CardDirPath(int32_t port, int32_t slot) const;
};

}  // namespace aoe2::mc
