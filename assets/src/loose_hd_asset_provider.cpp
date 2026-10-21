#include <aoe2/assets/loose_hd_asset_provider.hpp>

#include <fstream>
#include <sstream>

namespace aoe2::assets {

namespace {

std::string SniffFormat(const std::vector<uint8_t>& data) {
    if (data.size() >= 4) {
        if (data[0] == '2' && data[1] == '.' && data[2] == '0' && data[3] == 'N') {
            return "slp-2.0N";
        }
        if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F') {
            return "riff";
        }
        if (data[0] == 'B' && data[1] == 'I' && data[2] == 'N' && data[3] == 'A') {
            return "bina";
        }
    }
    return "unknown";
}

} // namespace

bool LooseHdAssetProvider::Initialize(std::string_view root_path) {
    root_path_ = root_path;
    return std::filesystem::exists(root_path_) && std::filesystem::is_directory(root_path_);
}

std::filesystem::path LooseHdAssetProvider::GetCategoryFolder(AssetCategory category) const {
    switch (category) {
        case AssetCategory::Interface: return root_path_ / "interface";
        case AssetCategory::Graphics:  return root_path_ / "graphics";
        case AssetCategory::Terrain:   return root_path_ / "terrain";
        case AssetCategory::Sound:     return root_path_ / "sound";
        case AssetCategory::Music:     return root_path_ / "music";
        case AssetCategory::GameData:  return root_path_ / "gamedata";
        case AssetCategory::Strings:   return root_path_ / "strings";
        default: return root_path_;
    }
}

std::filesystem::path LooseHdAssetProvider::FindAssetFile(AssetCategory category, uint32_t asset_id) const {
    const auto folder = GetCategoryFolder(category);
    if (!std::filesystem::exists(folder)) {
        return {};
    }

    const std::string id_str = std::to_string(asset_id);
    const char* extensions[] = {".slp", ".bina", ".wav", ".mp3", ".dat", ".txt", ""};

    for (const char* ext : extensions) {
        auto candidate = folder / (id_str + ext);
        if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool LooseHdAssetProvider::HasAsset(AssetCategory category, uint32_t asset_id) const {
    return !FindAssetFile(category, asset_id).empty();
}

std::optional<AssetBlob> LooseHdAssetProvider::LoadAsset(AssetCategory category, uint32_t asset_id) {
    auto path = FindAssetFile(category, asset_id);
    if (path.empty()) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }

    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    AssetBlob blob;
    blob.asset_id = asset_id;
    blob.data.resize(static_cast<size_t>(size));

    if (!file.read(reinterpret_cast<char*>(blob.data.data()), size)) {
        return std::nullopt;
    }

    blob.format = SniffFormat(blob.data);
    return blob;
}

bool LooseHdAssetProvider::LoadStringTable(std::string_view language, const std::filesystem::path& file_path) {
    std::ifstream file(file_path);
    if (!file) {
        return false;
    }

    std::string line;
    const std::string lang_str(language);

    while (std::getline(file, line)) {
        if (line.empty() || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) {
            continue;
        }
        std::istringstream iss(line);
        uint32_t id = 0;
        if (iss >> id) {
            std::string text;
            std::getline(iss, text);
            auto first_quote = text.find('\"');
            auto last_quote = text.rfind('\"');
            if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
                std::string parsed = text.substr(first_quote + 1, last_quote - first_quote - 1);
                strings_[{lang_str, id}] = std::move(parsed);
            }
        }
    }
    return true;
}

std::string_view LooseHdAssetProvider::GetString(uint32_t string_id, std::string_view language) {
    auto it = strings_.find({std::string(language), string_id});
    if (it != strings_.end()) {
        return it->second;
    }
    return {};
}

} // namespace aoe2::assets
