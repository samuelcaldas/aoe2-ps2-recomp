#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace aoe2::assets {

enum class PlatformTarget {
    Desktop,
    Switch,
    Android
};

class AssetPathResolver {
public:
    static std::filesystem::path ResolveRoot(
        std::string_view override_path = {},
        PlatformTarget platform = DefaultPlatform()
    );

    static constexpr PlatformTarget DefaultPlatform() {
#if defined(__SWITCH__)
        return PlatformTarget::Switch;
#elif defined(__ANDROID__)
        return PlatformTarget::Android;
#else
        return PlatformTarget::Desktop;
#endif
    }
};

} // namespace aoe2::assets
