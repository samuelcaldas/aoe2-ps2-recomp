#include <aoe2/assets/asset_resolver.hpp>

namespace aoe2::assets {

uint32_t AssetResolver::MakeTextureId(
    AssetCategory category,
    uint32_t asset_id,
    uint32_t frame_index,
    uint8_t player_id
) {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) {
        h = (h ^ v) * 16777619u;
    };
    mix(static_cast<uint32_t>(category));
    mix(asset_id);
    mix(frame_index);
    mix(static_cast<uint32_t>(player_id));
    return h == 0 ? 1u : h;
}

AssetResolver::AssetResolver(IAssetProvider& provider)
    : provider_(provider) {}

bool AssetResolver::LoadPalette(AssetCategory category, uint32_t palette_id) {
    if (!provider_.HasAsset(category, palette_id)) {
        return false;
    }
    const auto blob = provider_.LoadAsset(category, palette_id);
    if (!blob.has_value() || blob->data.empty()) {
        return false;
    }
    try {
        active_palette_ = JascPalette::FromBytes(blob->data);
        active_palette_id_ = palette_id;
        ++palette_version_;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void AssetResolver::SetActivePalette(const JascPalette& palette, uint32_t palette_id) {
    active_palette_ = palette;
    active_palette_id_ = palette_id;
    ++palette_version_;
}

const JascPalette* AssetResolver::GetActivePalette() const {
    if (active_palette_.has_value()) {
        return &active_palette_.value();
    }
    return nullptr;
}

void AssetResolver::RegisterMapping(uint32_t ps2_id, AssetCategory category, uint32_t hd_asset_id) {
    ps2_to_hd_map_[ps2_id] = {category, hd_asset_id};
}

std::optional<std::pair<AssetCategory, uint32_t>> AssetResolver::GetMapping(uint32_t ps2_id) const {
    auto it = ps2_to_hd_map_.find(ps2_id);
    if (it != ps2_to_hd_map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void AssetResolver::ClearCache() {
    frame_cache_.clear();
}

std::optional<ResolvedSprite> AssetResolver::ResolveSprite(
    AssetCategory category,
    uint32_t asset_id,
    uint32_t frame_index,
    uint8_t player_id
) {
    if (!active_palette_.has_value()) {
        // Fallback default grayscale palette if none active
        std::string fallback = "JASC-PAL\r\n0100\r\n256\r\n";
        for (int i = 0; i < 256; ++i) {
            fallback += std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(i) + "\r\n";
        }
        active_palette_ = JascPalette::FromString(fallback);
        active_palette_id_ = 0;
        ++palette_version_;
    }

    const FrameCacheKey key{category, asset_id, frame_index, player_id, palette_version_};
    const uint32_t texId = MakeTextureId(category, asset_id, frame_index, player_id);

    auto it = frame_cache_.find(key);
    if (it != frame_cache_.end()) {
        const auto& f = it->second;
        return ResolvedSprite{asset_id, f.width, f.height, f.hotspot_x, f.hotspot_y, f.rgba_pixels.data(), texId};
    }

    if (!provider_.HasAsset(category, asset_id)) {
        return std::nullopt;
    }

    const auto blob = provider_.LoadAsset(category, asset_id);
    if (!blob.has_value() || blob->data.empty()) {
        return std::nullopt;
    }

    try {
        DecodedFrame frame = SlpDecoder::DecodeFrame(blob->data, frame_index, active_palette_.value(), player_id);
        auto& cached = frame_cache_[key] = std::move(frame);
        return ResolvedSprite{asset_id, cached.width, cached.height, cached.hotspot_x, cached.hotspot_y, cached.rgba_pixels.data(), texId};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<ResolvedSprite> AssetResolver::ResolvePs2Sprite(
    uint32_t ps2_id,
    uint32_t frame_index,
    uint8_t player_id
) {
    const auto mapped = GetMapping(ps2_id);
    if (!mapped.has_value()) {
        return std::nullopt;
    }
    return ResolveSprite(mapped->first, mapped->second, frame_index, player_id);
}

} // namespace aoe2::assets
