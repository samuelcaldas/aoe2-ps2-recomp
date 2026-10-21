#include <aoe2/assets/asset_path_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

void TestAssetPathResolver() {
    auto path_override = aoe2::assets::AssetPathResolver::ResolveRoot("/custom/path");
    assert(path_override == "/custom/path");

    auto path_switch = aoe2::assets::AssetPathResolver::ResolveRoot({}, aoe2::assets::PlatformTarget::Switch);
    assert(path_switch == "sdmc:/switch/aoe2/assets");

    auto path_android = aoe2::assets::AssetPathResolver::ResolveRoot({}, aoe2::assets::PlatformTarget::Android);
    assert(path_android == "assets");
}

void TestLooseHdAssetProvider() {
    const std::filesystem::path test_dir = std::filesystem::temp_directory_path() / "aoe2_test_assets";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir / "interface");
    std::filesystem::create_directories(test_dir / "strings");

    // Write a mock SLP 2.0N asset file
    {
        std::ofstream slp(test_dir / "interface" / "50100.slp", std::ios::binary);
        const char header[] = "2.0N\x01\x00\x00\x00";
        slp.write(header, sizeof(header) - 1);
    }

    // Write a mock key-value strings file
    {
        std::ofstream str(test_dir / "strings" / "key-value-strings-utf8.txt");
        str << "// Comment line\n";
        str << "100 \"Town Center\"\n";
        str << "101 \"Villager\"\n";
    }

    aoe2::assets::LooseHdAssetProvider provider;
    assert(provider.Initialize(test_dir.string()));

    // Test asset loading and format sniffing
    assert(provider.HasAsset(aoe2::assets::AssetCategory::Interface, 50100));
    assert(!provider.HasAsset(aoe2::assets::AssetCategory::Interface, 99999));

    auto blob = provider.LoadAsset(aoe2::assets::AssetCategory::Interface, 50100);
    assert(blob.has_value());
    assert(blob->asset_id == 50100);
    assert(blob->format == "slp-2.0N");
    assert(blob->data.size() >= 4);

    // Test strings parsing
    assert(provider.LoadStringTable("en", test_dir / "strings" / "key-value-strings-utf8.txt"));
    assert(provider.GetString(100, "en") == "Town Center");
    assert(provider.GetString(101, "en") == "Villager");
    assert(provider.GetString(999, "en").empty());

    std::filesystem::remove_all(test_dir);
}

int main() {
    std::cout << "[aoe2_asset_tests] Running asset path resolver tests...\n";
    TestAssetPathResolver();

    std::cout << "[aoe2_asset_tests] Running loose HD asset provider tests...\n";
    TestLooseHdAssetProvider();

    std::cout << "[aoe2_asset_tests] All asset provider tests passed successfully!\n";
    return 0;
}
