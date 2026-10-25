#include <aoe2/hd/hd_storage_model.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

namespace aoe2::hd {

HdStorageModel::HdStorageModel(aoe2::runtime::GuestAccess& memory)
    : memory_(memory) {}

bool HdStorageModel::Initialize(std::string_view root_path) {
    root_path_ = std::string(root_path);
    return true;
}

std::string HdStorageModel::ResolvePath(std::string_view filename) const {
    std::string clean_name(filename);
    for (const auto prefix : {"host0:", "sim:", "atfile:"}) {
        if (clean_name.find(prefix) == 0) {
            clean_name = clean_name.substr(std::string_view(prefix).length());
            break;
        }
    }
    if (root_path_.empty()) return clean_name;
    return (std::filesystem::path(root_path_) / clean_name).string();
}

int32_t HdStorageModel::FileExists(std::string_view filename) const {
    const std::string path = ResolvePath(filename);
    return std::filesystem::exists(path) ? 1 : 0;
}

int32_t HdStorageModel::ReadFile(std::string_view filename, uint32_t dest_guest_addr, int32_t size, int32_t offset) {
    const std::string path = ResolvePath(filename);
    if (!std::filesystem::exists(path)) return -1;

    std::ifstream file(path, std::ios::binary);
    if (!file) return -1;

    file.seekg(offset);
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    const auto bytes_read = file.gcount();

    if (bytes_read > 0) {
        memory_.WriteBytes(dest_guest_addr, buffer.data(), bytes_read);
    }
    return static_cast<int32_t>(bytes_read);
}

}  // namespace aoe2::hd
