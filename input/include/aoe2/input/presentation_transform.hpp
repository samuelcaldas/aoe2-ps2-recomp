#pragma once

#include <aoe2/input/surface_mapping.hpp>

namespace aoe2::input {

struct FrameRect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

// One mapping shared by drawing and pointing: the presented GS frame is
// aspect-fit and centered in the host window, and the game's logical screen
// occupies a known rectangle of that frame (PAL: 640x448 stretched to 640x512).
class PresentationTransform {
public:
    // Throws std::invalid_argument if an extent is not strictly positive or the
    // logical rectangle is empty or not inside the frame.
    PresentationTransform(Extent window, Extent frame, Extent logical, FrameRect logical_in_frame);

    // The game's logical screen covers the whole presented frame (stretched if needed).
    static PresentationTransform LogicalFillsFrame(Extent window, Extent frame, Extent logical);

    // Where the whole frame is drawn in window pixels.
    FrameRect FrameDestination() const;
    // Window pixel -> logical pixel, clamped to the logical screen.
    GuestPoint ToLogical(float window_x, float window_y) const;
    // Logical pixel -> window pixel (not clamped).
    GuestPoint ToWindow(float logical_x, float logical_y) const;

private:
    Extent logical_;
    FrameRect frame_destination_;
    FrameRect logical_in_window_;
};

} // namespace aoe2::input
