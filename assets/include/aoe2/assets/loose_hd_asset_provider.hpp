#pragma once

#include <aoe2/assets/asset_provider.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace aoe2::assets {

class LooseHdAssetProvider : public IAssetProvider {
public:
    LooseHdAssetProvider() = default;
    ~LooseHdAssetProvider() override = default;

    bool Initialize(std::string_view root_path) override;
    bool HasAsset(AssetCategory category, uint32_t asset_id) const override;
    std::optional<AssetBlob> LoadAsset(AssetCategory category, uint32_t asset_id) override;
    std::string_view GetString(uint32_t string_id, std::string_view language = "en") override;

    bool LoadStringTable(std::string_view language, const std::filesystem::path& file_path);

private:
    std::filesystem::path root_path_;
    std::map<std::pair<std::string, uint32_t>, std::string> strings_;

    std::filesystem::path GetCategoryFolder(AssetCategory category) const;
    std::filesystem::path FindAssetFile(AssetCategory category, uint32_t asset_id) const;
};

} // namespace aoe2::assets
