#pragma once

namespace aoe2::input {

struct Extent {
    float width{0.0f};
    float height{0.0f};
};

struct GuestPoint {
    float x{0.0f};
    float y{0.0f};
};

// Maps host surface coordinates (e.g. the 1280x720 Switch touchscreen) onto the
// guest framebuffer, assuming the guest image is aspect-fit and centered
// (pillarboxed/letterboxed). Points outside the image clamp to its edge.
class SurfaceMapping {
public:
    // Throws std::invalid_argument if either extent is not strictly positive.
    SurfaceMapping(Extent host, Extent guest);

    GuestPoint ToGuest(float host_x, float host_y) const;

private:
    Extent guest_;
    float scale_;
    float offset_x_;
    float offset_y_;
};

} // namespace aoe2::input
