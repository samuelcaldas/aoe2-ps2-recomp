#pragma once

#include <aoe2/assets/asset_provider.hpp>
#include <aoe2/assets/jasc_palette.hpp>
#include <aoe2/assets/slp_decoder.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace aoe2::assets {

struct ResolvedSprite {
    uint32_t asset_id{0};
    int32_t width{0};
    int32_t height{0};
    int32_t hotspot_x{0};
    int32_t hotspot_y{0};
    const uint32_t* rgba_data{nullptr};
};

class AssetResolver {
public:
    explicit AssetResolver(IAssetProvider& provider);

    bool LoadPalette(AssetCategory category, uint32_t palette_id);
    void SetActivePalette(const JascPalette& palette);
    const JascPalette* GetActivePalette() const;

    void RegisterMapping(uint32_t ps2_id, AssetCategory category, uint32_t hd_asset_id);
    std::optional<std::pair<AssetCategory, uint32_t>> GetMapping(uint32_t ps2_id) const;

    std::optional<ResolvedSprite> ResolveSprite(
        AssetCategory category,
        uint32_t asset_id,
        uint32_t frame_index = 0,
        uint8_t player_id = 0
    );

    std::optional<ResolvedSprite> ResolvePs2Sprite(
        uint32_t ps2_id,
        uint32_t frame_index = 0,
        uint8_t player_id = 0
    );

private:
    IAssetProvider& provider_;
    std::optional<JascPalette> active_palette_;
    std::unordered_map<uint32_t, std::pair<AssetCategory, uint32_t>> ps2_to_hd_map_;
    std::unordered_map<uint64_t, DecodedFrame> frame_cache_;
};

} // namespace aoe2::assets
