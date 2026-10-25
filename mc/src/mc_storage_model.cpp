#include <aoe2/mc/mc_storage_model.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace aoe2::mc {

McStorageModel::McStorageModel(aoe2::runtime::GuestAccess& memory)
    : memory_(memory) {}

bool McStorageModel::Initialize(std::string_view root_path) {
    root_path_ = std::string(root_path);
    return true;
}

std::string McStorageModel::CardDirPath(int32_t port, int32_t /*slot*/) const {
    if (root_path_.empty()) return "";
    return (std::filesystem::path(root_path_) / ("mc" + std::to_string(port))).string();
}

int32_t McStorageModel::GetDir(int32_t port, int32_t slot, std::string_view /*pattern*/,
                              uint32_t out_entries, int32_t max_entries) {
    const std::string dir_path = CardDirPath(port, slot);
    if (dir_path.empty() || !std::filesystem::exists(dir_path)) {
        return 0;
    }

    int32_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (count >= max_entries) break;
        if (!entry.is_regular_file()) continue;

        KtMcFileInfo info{};
        const std::string filename = entry.path().filename().string();
        std::strncpy(info.name, filename.c_str(), sizeof(info.name) - 1);
        info.size = static_cast<int32_t>(entry.file_size());
        info.attr = 0x8411; // Standard PS2 save file attribute

        const uint32_t entry_addr = out_entries + count * sizeof(KtMcFileInfo);
        memory_.WriteBytes(entry_addr, reinterpret_cast<const uint8_t*>(&info), sizeof(info));
        ++count;
    }
    return count;
}

int32_t McStorageModel::ReadFile(int32_t port, int32_t slot, std::string_view filename, uint32_t dest_guest_addr) {
    const std::string dir_path = CardDirPath(port, slot);
    if (dir_path.empty()) return -1;

    const std::filesystem::path file_path = std::filesystem::path(dir_path) / filename;
    if (!std::filesystem::exists(file_path)) return -1;

    std::ifstream file(file_path, std::ios::binary);
    if (!file) return -1;

    const auto file_size = std::filesystem::file_size(file_path);
    std::vector<uint8_t> buffer(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);

    memory_.WriteBytes(dest_guest_addr, buffer.data(), file_size);
    return static_cast<int32_t>(file_size);
}

int32_t McStorageModel::WriteFile(int32_t port, int32_t slot, std::string_view filename,
                                 uint32_t src_guest_addr, uint32_t size) {
    const std::string dir_path = CardDirPath(port, slot);
    if (dir_path.empty()) return -1;

    std::filesystem::create_directories(dir_path);
    const std::filesystem::path file_path = std::filesystem::path(dir_path) / filename;

    std::vector<uint8_t> buffer(size);
    memory_.ReadBytes(src_guest_addr, buffer.data(), size);

    std::ofstream file(file_path, std::ios::binary);
    if (!file) return -1;

    file.write(reinterpret_cast<const char*>(buffer.data()), size);
    return static_cast<int32_t>(size);
}

int32_t McStorageModel::DeleteFile(int32_t port, int32_t slot, std::string_view filename) {
    const std::string dir_path = CardDirPath(port, slot);
    if (dir_path.empty()) return -1;

    const std::filesystem::path file_path = std::filesystem::path(dir_path) / filename;
    if (!std::filesystem::exists(file_path)) return -1;

    std::filesystem::remove(file_path);
    return 0;
}

}  // namespace aoe2::mc
