#include <aoe2/input/guest_mouse.hpp>

namespace aoe2::input {

GuestMouseAdapter::GuestMouseAdapter(GuestPoint initial_position)
    : current_{initial_position.x, initial_position.y, false, false, true},
      cursor_visible_{true} {}

void GuestMouseAdapter::SubmitEvent(const ActionEvent& event) {
    switch (event.type) {
        case ActionType::PointerMove:
            MoveTo(event);
            break;
        case ActionType::PointerDown:
            MoveTo(event);
            SetButtons(true, false);
            break;
        case ActionType::PointerUp:
            MoveTo(event);
            SetButtons(false, false);
            break;
        case ActionType::PrimaryClick:
            MoveTo(event);
            SetButtons(true, false);
            SetButtons(false, false);
            break;
        case ActionType::SecondaryClick:
            MoveTo(event);
            SetButtons(false, true);
            SetButtons(false, false);
            break;
        case ActionType::DoubleClick:
            MoveTo(event);
            SetButtons(true, false);
            SetButtons(false, false);
            SetButtons(true, false);
            SetButtons(false, false);
            break;
        case ActionType::AttackMove:
            MoveTo(event);
            SetButtons(false, true);
            SetButtons(false, false);
            break;
        case ActionType::TouchReticleFeedback:
            MoveTo(event);
            break;
        // Pan, Zoom and menu actions have no USB-mouse equivalent; they are
        // routed to high-level camera/menu hooks rather than the guest mouse.
        default:
            break;
    }
}

GuestMouseState GuestMouseAdapter::NextFrame() {
    if (pending_frames_.empty()) {
        return current_;
    }
    const GuestMouseState frame = pending_frames_.front();
    pending_frames_.pop_front();
    return frame;
}

void GuestMouseAdapter::SetCursorVisible(bool visible) {
    cursor_visible_ = visible;
    current_.cursor_visible = visible;
}

bool GuestMouseAdapter::IsCursorVisible() const {
    return cursor_visible_;
}

void GuestMouseAdapter::MoveTo(const ActionEvent& event) {
    current_.x = event.x;
    current_.y = event.y;
}

void GuestMouseAdapter::SetButtons(bool left, bool right) {
    current_.left = left;
    current_.right = right;
    current_.cursor_visible = cursor_visible_;
    pending_frames_.push_back(current_);
}

} // namespace aoe2::input
