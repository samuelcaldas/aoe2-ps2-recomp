#include <aoe2/input/surface_mapping.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace aoe2::input {

namespace {

void RequirePositive(Extent extent, const char* name) {
    if (extent.width > 0.0f && extent.height > 0.0f) {
        return;
    }
    throw std::invalid_argument(std::string(name) + " extent must be positive");
}

} // namespace

SurfaceMapping::SurfaceMapping(Extent host, Extent guest)
    : guest_(guest), scale_(1.0f), offset_x_(0.0f), offset_y_(0.0f) {
    RequirePositive(host, "host");
    RequirePositive(guest, "guest");
    scale_ = std::min(host.width / guest.width, host.height / guest.height);
    offset_x_ = (host.width - guest.width * scale_) / 2.0f;
    offset_y_ = (host.height - guest.height * scale_) / 2.0f;
}

GuestPoint SurfaceMapping::ToGuest(float host_x, float host_y) const {
    const float x = (host_x - offset_x_) / scale_;
    const float y = (host_y - offset_y_) / scale_;
    return GuestPoint{
        std::clamp(x, 0.0f, guest_.width - 1.0f),
        std::clamp(y, 0.0f, guest_.height - 1.0f),
    };
}

} // namespace aoe2::input
