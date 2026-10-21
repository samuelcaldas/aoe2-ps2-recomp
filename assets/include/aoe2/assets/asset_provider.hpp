#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aoe2::assets {

enum class AssetCategory {
    Interface,
    Graphics,
    Terrain,
    Sound,
    Music,
    GameData,
    Strings
};

struct AssetBlob {
    std::vector<uint8_t> data;
    std::string format; // "slp-2.0N", "riff", "bina", "txt", "dat"
    uint32_t asset_id{0};
};

class IAssetProvider {
public:
    virtual ~IAssetProvider() = default;

    virtual bool Initialize(std::string_view root_path) = 0;
    virtual bool HasAsset(AssetCategory category, uint32_t asset_id) const = 0;
    virtual std::optional<AssetBlob> LoadAsset(AssetCategory category, uint32_t asset_id) = 0;
    virtual std::string_view GetString(uint32_t string_id, std::string_view language = "en") = 0;
};

} // namespace aoe2::assets
