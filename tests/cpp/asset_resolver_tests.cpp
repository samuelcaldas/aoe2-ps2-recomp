#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>

#include <cassert>
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

} // namespace

int main() {
    TestMappingAndResolution();
    std::cout << "asset_resolver_tests passed\n";
    return 0;
}
