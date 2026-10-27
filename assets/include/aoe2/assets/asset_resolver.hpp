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
    uint32_t texture_id{0};
};

struct FrameCacheKey {
    AssetCategory category;
    uint32_t asset_id;
    uint32_t frame_index;
    uint8_t player_id;
    uint32_t palette_version;

    bool operator==(const FrameCacheKey& other) const {
        return category == other.category &&
               asset_id == other.asset_id &&
               frame_index == other.frame_index &&
               player_id == other.player_id &&
               palette_version == other.palette_version;
    }
};

struct FrameCacheKeyHash {
    std::size_t operator()(const FrameCacheKey& k) const {
        std::size_t h = 2166136261u;
        auto mix = [&h](std::size_t v) {
            h = (h ^ v) * 16777619u;
        };
        mix(static_cast<std::size_t>(k.category));
        mix(k.asset_id);
        mix(k.frame_index);
        mix(k.player_id);
        mix(k.palette_version);
        return h;
    }
};

class AssetResolver {
public:
    explicit AssetResolver(IAssetProvider& provider);

    static uint32_t MakeTextureId(
        AssetCategory category,
        uint32_t asset_id,
        uint32_t frame_index = 0,
        uint8_t player_id = 0
    );

    bool LoadPalette(AssetCategory category, uint32_t palette_id);
    void SetActivePalette(const JascPalette& palette, uint32_t palette_id = 0);
    const JascPalette* GetActivePalette() const;
    uint32_t GetActivePaletteId() const { return active_palette_id_; }

    void RegisterMapping(uint32_t ps2_id, AssetCategory category, uint32_t hd_asset_id);
    std::optional<std::pair<AssetCategory, uint32_t>> GetMapping(uint32_t ps2_id) const;

    void ClearCache();

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
    uint32_t active_palette_id_{0};
    uint32_t palette_version_{1};
    std::unordered_map<uint32_t, std::pair<AssetCategory, uint32_t>> ps2_to_hd_map_;
    std::unordered_map<FrameCacheKey, DecodedFrame, FrameCacheKeyHash> frame_cache_;
};

} // namespace aoe2::assets
