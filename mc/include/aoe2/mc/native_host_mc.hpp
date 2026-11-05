#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aoe2::mc {

// Sony PS2 SDK sceMc structures
struct SceMcStDateTime {
    uint8_t Resv2{0};
    uint8_t Sec{0};
    uint8_t Min{0};
    uint8_t Hour{0};
    uint8_t Day{0};
    uint8_t Month{0};
    uint16_t Year{0};
};

struct SceMcTblGetDir {
    SceMcStDateTime _Create{};
    SceMcStDateTime _Modify{};
    uint32_t FileSizeByte{0};
    uint16_t AttrFile{0};
    uint16_t Reserve1{0};
    uint32_t Reserve2{0};
    uint32_t PdaAplNo{0};
    char EntryName[32]{};
};
static_assert(sizeof(SceMcTblGetDir) == 64, "SceMcTblGetDir must match PS2 SDK 64-byte layout");

constexpr int32_t kMcCmdGetInfo = 0x01;
constexpr int32_t kMcCmdOpen = 0x02;
constexpr int32_t kMcCmdClose = 0x03;
constexpr int32_t kMcCmdSeek = 0x04;
constexpr int32_t kMcCmdRead = 0x05;
constexpr int32_t kMcCmdWrite = 0x06;
constexpr int32_t kMcCmdFlush = 0x0A;
constexpr int32_t kMcCmdMkdir = 0x0B;
constexpr int32_t kMcCmdChdir = 0x0C;
constexpr int32_t kMcCmdGetDir = 0x0D;
constexpr int32_t kMcCmdSetFileInfo = 0x0E;
constexpr int32_t kMcCmdDelete = 0x0F;
constexpr int32_t kMcCmdFormat = 0x10;
constexpr int32_t kMcCmdUnformat = 0x11;
constexpr int32_t kMcCmdGetEntSpace = 0x12;
constexpr int32_t kMcCmdRename = 0x13;

constexpr int32_t kMcResultSucceed = 0;
constexpr int32_t kMcResultChangedCard = -1;
constexpr int32_t kMcResultNoFormat = -2;
constexpr int32_t kMcResultNoEntry = -4;
constexpr int32_t kMcResultDeniedPermit = -5;
constexpr int32_t kMcResultNotEmpty = -6;
constexpr int32_t kMcResultUpLimitHandle = -7;

constexpr int32_t kMcTypePs2 = 2;
constexpr int32_t kMcFormatted = 1;
constexpr int32_t kMcFreeClusters = 0x2000;

constexpr uint16_t kMcAttrReadable = 0x0001;
constexpr uint16_t kMcAttrWriteable = 0x0002;
constexpr uint16_t kMcAttrExecutable = 0x0004;
constexpr uint16_t kMcAttrDupProhibit = 0x0008;
constexpr uint16_t kMcAttrFile = 0x0010;
constexpr uint16_t kMcAttrSubdir = 0x0020;
constexpr uint16_t kMcAttrClosed = 0x0080;
constexpr uint16_t kMcAttrPDAExec = 0x0800;
constexpr uint16_t kMcAttrPSX = 0x1000;
constexpr uint16_t kMcAttrHiddenFile = 0x2000;
constexpr uint16_t kMcAttrExists = 0x8000;

class NativeHostMemoryCard {
public:
    static NativeHostMemoryCard& Instance();

    void SetRootDirectory(int32_t port, const std::filesystem::path& root_path);
    std::filesystem::path GetRootDirectory(int32_t port) const;

    // Direct sceMc API implementation
    void SceMcGetInfo(int32_t port, int32_t slot, uint32_t type_ptr, uint32_t free_ptr, uint32_t formatted_ptr,
                      aoe2::runtime::GuestAccess& memory);
    void SceMcOpen(int32_t port, int32_t slot, const std::string& path, uint32_t flags,
                   aoe2::runtime::GuestAccess& memory);
    void SceMcClose(int32_t fd);
    void SceMcSeek(int32_t fd, int32_t offset, int32_t whence);
    void SceMcRead(int32_t fd, uint32_t buffer_guest_addr, int32_t size,
                   aoe2::runtime::GuestAccess& memory);
    void SceMcWrite(int32_t fd, uint32_t buffer_guest_addr, int32_t size,
                    aoe2::runtime::GuestAccess& memory);
    void SceMcFlush(int32_t fd);
    void SceMcMkdir(int32_t port, int32_t slot, const std::string& path);
    void SceMcChdir(int32_t port, int32_t slot, const std::string& path, uint32_t current_dir_ptr,
                    aoe2::runtime::GuestAccess& memory);
    void SceMcGetDir(int32_t port, int32_t slot, const std::string& path, uint32_t mode, int32_t max_entries,
                     uint32_t table_guest_addr, aoe2::runtime::GuestAccess& memory);
    void SceMcSetFileInfo(int32_t port, int32_t slot, const std::string& path, uint32_t info_guest_addr, uint32_t flags,
                          aoe2::runtime::GuestAccess& memory);
    void SceMcDelete(int32_t port, int32_t slot, const std::string& path);
    void SceMcFormat(int32_t port, int32_t slot);
    void SceMcUnformat(int32_t port, int32_t slot);
    void SceMcGetEntSpace(int32_t port, int32_t slot, const std::string& path);
    void SceMcRename(int32_t port, int32_t slot, const std::string& old_path, const std::string& new_path);
    int32_t SceMcSync(int32_t mode, uint32_t cmd_ptr, uint32_t result_ptr,
                      aoe2::runtime::GuestAccess& memory);

    void Reset();

private:
    NativeHostMemoryCard();

    struct OpenFile {
        int32_t fd{0};
        int32_t port{0};
        std::filesystem::path host_path;
        FILE* handle{nullptr};
    };

    struct PortState {
        std::string current_dir{"/"};
        bool formatted{true};
        std::filesystem::path root_path;
    };

    std::filesystem::path ResolveHostPath(int32_t port, const std::string& guest_path) const;
    std::string NormalizeGuestPath(int32_t port, std::string path) const;
    void SetCommandResult(int32_t cmd, int32_t result);

    mutable std::mutex mutex_;
    std::array<PortState, 2> ports_;
    std::unordered_map<int32_t, OpenFile> files_;
    int32_t next_fd_{1};
    int32_t last_cmd_{0};
    int32_t last_result_{0};
    bool command_pending_{false};
};

}  // namespace aoe2::mc
