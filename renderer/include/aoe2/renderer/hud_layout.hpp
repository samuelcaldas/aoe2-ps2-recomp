#pragma once

#include <aoe2/input/surface_mapping.hpp>

#include <vector>

namespace aoe2::renderer {

struct Rect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

enum class HudRegionRole {
    ResourceBar,
    CommandBar,
    SidePanel,
};

enum class HudAnchor {
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct HudRegionSpec {
    HudRegionRole role;
    HudAnchor anchor;
    Rect guest_source;
    float destination_width;
    float destination_height;
    float margin_x{0.0f};
    float margin_y{0.0f};
};

struct HudRegion {
    HudRegionRole role;
    Rect guest_source;
    Rect destination;
};

class HudLayout {
public:
    static HudLayout ForExtent(
        aoe2::input::Extent display_extent,
        const std::vector<HudRegionSpec>& specifications);

    static HudLayout ForExtent(
        aoe2::input::Extent guest_extent,
        aoe2::input::Extent display_extent,
        const std::vector<HudRegionSpec>& specifications);

    const std::vector<HudRegion>& regions() const;

private:
    explicit HudLayout(std::vector<HudRegion> regions);
    std::vector<HudRegion> regions_;
};

} // namespace aoe2::renderer
