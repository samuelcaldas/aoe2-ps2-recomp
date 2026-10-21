#include <aoe2/assets/asset_path_resolver.hpp>

#include <cstdlib>

namespace aoe2::assets {

std::filesystem::path AssetPathResolver::ResolveRoot(
    std::string_view override_path,
    PlatformTarget platform)
{
    if (!override_path.empty()) {
        return std::filesystem::path(override_path);
    }

    switch (platform) {
        case PlatformTarget::Switch:
            return "sdmc:/switch/aoe2/assets";

        case PlatformTarget::Android:
            return "assets";

        case PlatformTarget::Desktop:
        default: {
            const char* xdg_data = std::getenv("XDG_DATA_HOME");
            if (xdg_data != nullptr && *xdg_data != '\0') {
                auto path = std::filesystem::path(xdg_data) / "aoe2-recomp" / "assets";
                if (std::filesystem::exists(path)) {
                    return path;
                }
            }
            return "assets";
        }
    }
}

} // namespace aoe2::assets
