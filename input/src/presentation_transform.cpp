#include <aoe2/input/presentation_transform.hpp>

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

void RequireInside(FrameRect rect, Extent frame) {
    const bool inside = rect.width > 0.0f && rect.height > 0.0f && rect.x >= 0.0f && rect.y >= 0.0f &&
                        rect.x + rect.width <= frame.width && rect.y + rect.height <= frame.height;
    if (inside) {
        return;
    }
    throw std::invalid_argument("logical rectangle must be non-empty and inside the frame");
}

FrameRect FitFrame(Extent window, Extent frame) {
    const float scale = std::min(window.width / frame.width, window.height / frame.height);
    const float width = frame.width * scale;
    const float height = frame.height * scale;
    return {(window.width - width) / 2.0f, (window.height - height) / 2.0f, width, height};
}

} // namespace

PresentationTransform::PresentationTransform(Extent window, Extent frame, Extent logical,
                                             FrameRect logical_in_frame)
    : logical_(logical) {
    RequirePositive(window, "window");
    RequirePositive(frame, "frame");
    RequirePositive(logical, "logical");
    RequireInside(logical_in_frame, frame);
    const FrameRect destination = FitFrame(window, frame);
    const float scale = destination.width / frame.width;
    logical_in_window_ = {destination.x + logical_in_frame.x * scale, destination.y + logical_in_frame.y * scale,
                          logical_in_frame.width * scale, logical_in_frame.height * scale};
    frame_destination_ = destination;
}

PresentationTransform PresentationTransform::LogicalFillsFrame(Extent window, Extent frame, Extent logical) {
    return PresentationTransform(window, frame, logical, {0.0f, 0.0f, frame.width, frame.height});
}

FrameRect PresentationTransform::FrameDestination() const {
    return frame_destination_;
}

GuestPoint PresentationTransform::ToLogical(float window_x, float window_y) const {
    const float x = (window_x - logical_in_window_.x) * logical_.width / logical_in_window_.width;
    const float y = (window_y - logical_in_window_.y) * logical_.height / logical_in_window_.height;
    return {std::clamp(x, 0.0f, logical_.width - 1.0f), std::clamp(y, 0.0f, logical_.height - 1.0f)};
}

GuestPoint PresentationTransform::ToWindow(float logical_x, float logical_y) const {
    return {logical_in_window_.x + logical_x * logical_in_window_.width / logical_.width,
            logical_in_window_.y + logical_y * logical_in_window_.height / logical_.height};
}

} // namespace aoe2::input
