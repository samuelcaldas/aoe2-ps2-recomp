#pragma once

#include <aoe2/input/surface_mapping.hpp>
#include <aoe2/renderer/hud_layout.hpp>

#include <optional>
#include <vector>

namespace aoe2::input {

class HudInverseMapper {
public:
    explicit HudInverseMapper(const std::vector<renderer::HudRegion>& regions);

    std::optional<GuestPoint> ToGuest(float display_x, float display_y) const;

private:
    std::vector<renderer::HudRegion> regions_;
};

} // namespace aoe2::input
