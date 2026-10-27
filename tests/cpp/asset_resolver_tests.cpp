#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>

#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

class MockAssetProvider : public aoe2::assets::IAssetProvider {
public:
    bool Initialize(std::string_view root) override { return true; }
    bool HasAsset(aoe2::assets::AssetCategory category, uint32_t numeric_id) const override {
        return assets_.find(numeric_id) != assets_.end();
    }
    std::optional<aoe2::assets::AssetBlob> LoadAsset(aoe2::assets::AssetCategory category, uint32_t numeric_id) override {
        auto it = assets_.find(numeric_id);
        if (it != assets_.end()) {
            return aoe2::assets::AssetBlob{it->second, "slp-2.0N", numeric_id};
        }
        return std::nullopt;
    }
    std::string_view GetString(uint32_t string_id, std::string_view language = "en") override {
        return "";
    }

    void AddAsset(uint32_t id, std::vector<uint8_t> data) {
        assets_[id] = std::move(data);
    }

private:
    std::unordered_map<uint32_t, std::vector<uint8_t>> assets_;
};

aoe2::assets::JascPalette MakeSolidPalette(uint8_t r, uint8_t g, uint8_t b) {
    std::string text = "JASC-PAL\r\n0100\r\n256\r\n";
    for (int i = 0; i < 256; ++i) {
        text += std::to_string(r) + " " + std::to_string(g) + " " + std::to_string(b) + "\r\n";
    }
    return aoe2::assets::JascPalette::FromString(text);
}

void TestMappingAndResolution() {
    MockAssetProvider provider;
    aoe2::assets::AssetResolver resolver(provider);

    // Register mapping: PS2 ID 1000 -> Interface asset 50100
    resolver.RegisterMapping(1000, aoe2::assets::AssetCategory::Interface, 50100);
    const auto mapped = resolver.GetMapping(1000);
    Check(mapped.has_value(), "mapping exists for 1000");
    Check(mapped->first == aoe2::assets::AssetCategory::Interface, "category is Interface");
    Check(mapped->second == 50100, "mapped ID is 50100");

    // Unmapped PS2 ID returns nullopt (fallback)
    Check(!resolver.GetMapping(9999).has_value(), "unmapped ID returns nullopt");
    Check(!resolver.ResolvePs2Sprite(9999).has_value(), "unmapped resolve returns nullopt");
}

void TestPaletteCacheDifferentiation() {
    MockAssetProvider provider;
    const std::string path = "game/hd-edition/source/resources/_common/drs/interface/53210.slp";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "53210.slp not found, skipping palette cache test\n";
        return;
    }
    std::vector<uint8_t> slp_bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    provider.AddAsset(53210, slp_bytes);

    aoe2::assets::AssetResolver resolver(provider);

    // Palette A: Pure Red
    const auto palRed = MakeSolidPalette(255, 0, 0);
    resolver.SetActivePalette(palRed);

    auto spriteRed = resolver.ResolveSprite(aoe2::assets::AssetCategory::Interface, 53210, 0, 0);
    Check(spriteRed.has_value(), "resolved with red palette");
    // Pixel 3 is opaque (r=255, g=0, b=0, a=255) -> 0xFF0000FF in little-endian RGBA
    uint32_t pxRed = spriteRed->rgba_data[3];
    uint8_t rA = pxRed & 0xFF;
    uint8_t bA = (pxRed >> 16) & 0xFF;
    Check(rA == 255 && bA == 0, "pixel is red under palette A");

    // Palette B: Pure Blue
    const auto palBlue = MakeSolidPalette(0, 0, 255);
    resolver.SetActivePalette(palBlue);

    auto spriteBlue = resolver.ResolveSprite(aoe2::assets::AssetCategory::Interface, 53210, 0, 0);
    Check(spriteBlue.has_value(), "resolved with blue palette");
    uint32_t pxBlue = spriteBlue->rgba_data[3];
    uint8_t rB = pxBlue & 0xFF;
    uint8_t bB = (pxBlue >> 16) & 0xFF;
    Check(rB == 0 && bB == 255, "pixel is blue under palette B (cache differentiated)");
}

void TestMakeTextureId() {
    // Unique texture IDs across different categories and frames
    uint32_t id1 = aoe2::assets::AssetResolver::MakeTextureId(aoe2::assets::AssetCategory::Interface, 50100, 0, 0);
    uint32_t id2 = aoe2::assets::AssetResolver::MakeTextureId(aoe2::assets::AssetCategory::Interface, 50100, 1, 0);
    uint32_t id3 = aoe2::assets::AssetResolver::MakeTextureId(aoe2::assets::AssetCategory::Graphics, 50100, 0, 0);
    uint32_t id4 = aoe2::assets::AssetResolver::MakeTextureId(aoe2::assets::AssetCategory::Interface, 50100, 0, 1);

    Check(id1 != id2, "different frame has different texture ID");
    Check(id1 != id3, "different category has different texture ID");
    Check(id1 != id4, "different player has different texture ID");
}

} // namespace

int main() {
    TestMappingAndResolution();
    TestPaletteCacheDifferentiation();
    TestMakeTextureId();
    std::cout << "asset_resolver_tests passed\n";
    return 0;
}
