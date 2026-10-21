#include <aoe2/input/hud_inverse_mapper.hpp>
#include <aoe2/renderer/hud_layout.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, std::string_view name) {
    if (condition) {
        return;
    }
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
}

bool Near(float first, float second) {
    return std::abs(first - second) < 0.001f;
}

aoe2::renderer::HudRegionSpec MakeRegion(
    aoe2::renderer::HudRegionRole role,
    aoe2::renderer::HudAnchor anchor,
    aoe2::renderer::Rect guest,
    float width,
    float height)
{
    return {role, anchor, guest, width, height, 12.0f, 8.0f};
}

void TestFourThreeKeepsAnchoredPanelsAtTheirSourceRectangles() {
    using namespace aoe2;
    std::vector<renderer::HudRegionSpec> specs{
        MakeRegion(renderer::HudRegionRole::ResourceBar, renderer::HudAnchor::Top, {0, 0, 640, 48}, 640, 48),
        MakeRegion(renderer::HudRegionRole::CommandBar, renderer::HudAnchor::BottomRight, {0, 400, 640, 80}, 640, 80),
    };
    const renderer::HudLayout layout = renderer::HudLayout::ForExtent({640, 480}, specs);

    Check(layout.regions().size() == 2, "preserves 4:3 region count");
    Check(Near(layout.regions()[0].destination.x, 0.0f), "4:3 top panel starts at left edge");
    Check(Near(layout.regions()[0].destination.y, 0.0f), "4:3 top panel starts at top edge");
    Check(Near(layout.regions()[1].destination.y, 400.0f), "4:3 bottom panel keeps original y");
}

void TestWidescreenAnchorsPanelsToOppositeEdges() {
    using namespace aoe2;
    std::vector<renderer::HudRegionSpec> specs{
        MakeRegion(renderer::HudRegionRole::ResourceBar, renderer::HudAnchor::Top, {0, 0, 640, 48}, 640, 48),
        MakeRegion(renderer::HudRegionRole::CommandBar, renderer::HudAnchor::BottomRight, {0, 400, 640, 80}, 640, 80),
    };
    const renderer::HudLayout layout = renderer::HudLayout::ForExtent({1280, 720}, specs);

    Check(Near(layout.regions()[0].destination.x, 12.0f), "top panel respects left safe margin");
    Check(Near(layout.regions()[1].destination.x, 628.0f), "bottom panel is right anchored");
}

void TestInverseMapperReturnsOriginalGuestCoordinates() {
    using namespace aoe2;
    const renderer::HudLayout layout = renderer::HudLayout::ForExtent(
        {1280, 720}, {MakeRegion(renderer::HudRegionRole::CommandBar, renderer::HudAnchor::BottomRight,
                                 {0, 400, 640, 80}, 640, 80)});
    const input::HudInverseMapper mapper(layout.regions());
    const auto mapped = mapper.ToGuest(700.0f, 650.0f);

    Check(mapped.has_value(), "point in relocated panel is mapped");
    if (!mapped.has_value()) {
        return;
    }
    Check(Near(mapped->x, 72.0f), "inverse map returns guest x");
    Check(Near(mapped->y, 418.0f), "inverse map returns guest y");
}

void TestInverseMapperDoesNotCapturePlayfieldGaps() {
    using namespace aoe2;
    const renderer::HudLayout layout = renderer::HudLayout::ForExtent(
        {1280, 720}, {MakeRegion(renderer::HudRegionRole::CommandBar, renderer::HudAnchor::BottomRight,
                                 {0, 400, 640, 80}, 640, 80)});
    const input::HudInverseMapper mapper(layout.regions());

    Check(!mapper.ToGuest(700.0f, 300.0f).has_value(), "point outside relocated regions falls through");
}

void TestLayoutRejectsNonFiniteGeometry() {
    using namespace aoe2;
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto not_a_number = std::numeric_limits<float>::quiet_NaN();
    bool rejected_infinite_extent = false;
    bool rejected_nan_source = false;
    bool rejected_nan_margin = false;
    try {
        (void)renderer::HudLayout::ForExtent({infinity, 720}, {});
    } catch (const std::invalid_argument&) {
        rejected_infinite_extent = true;
    }
    try {
        auto specification = MakeRegion(renderer::HudRegionRole::ResourceBar,
            renderer::HudAnchor::Top, {not_a_number, 0, 100, 20}, 100, 20);
        (void)renderer::HudLayout::ForExtent({1280, 720}, {specification});
    } catch (const std::invalid_argument&) {
        rejected_nan_source = true;
    }
    try {
        auto specification = MakeRegion(renderer::HudRegionRole::ResourceBar,
            renderer::HudAnchor::Top, {0, 0, 100, 20}, 100, 20);
        specification.margin_x = not_a_number;
        (void)renderer::HudLayout::ForExtent({1280, 720}, {specification});
    } catch (const std::invalid_argument&) {
        rejected_nan_margin = true;
    }
    Check(rejected_infinite_extent, "infinite extents are rejected");
    Check(rejected_nan_source, "NaN source geometry is rejected");
    Check(rejected_nan_margin, "NaN anchor margins are rejected");
}

void TestLayoutRejectsPanelsOutsideGuestOrDestinationExtent() {
    using namespace aoe2;
    bool rejected = false;
    try {
        const std::vector<renderer::HudRegionSpec> specs{
            MakeRegion(renderer::HudRegionRole::ResourceBar, renderer::HudAnchor::Top, {0, 0, 700, 48}, 700, 48),
        };
        (void)renderer::HudLayout::ForExtent({640, 480}, specs);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    Check(rejected, "invalid source geometry rejected");
}

} // namespace

int main() {
    TestFourThreeKeepsAnchoredPanelsAtTheirSourceRectangles();
    TestWidescreenAnchorsPanelsToOppositeEdges();
    TestInverseMapperReturnsOriginalGuestCoordinates();
    TestInverseMapperDoesNotCapturePlayfieldGaps();
    TestLayoutRejectsNonFiniteGeometry();
    TestLayoutRejectsPanelsOutsideGuestOrDestinationExtent();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
