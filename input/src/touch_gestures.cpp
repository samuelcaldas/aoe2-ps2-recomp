#include <aoe2/input/touch_gestures.hpp>

#include <cmath>

namespace aoe2::input {

TouchGestureTranslator::TouchGestureTranslator(InputSink& sink, SurfaceMapping mapping,
                                               GestureConfig config)
    : sink_(sink), mapping_(mapping), config_(config) {}

void TouchGestureTranslator::Submit(const TouchContact& contact) {
    const GuestPoint point = mapping_.ToGuest(contact.x, contact.y);
    if (contact.phase == TouchPhase::Began) {
        OnBegan(contact, point);
        return;
    }
    TrackedContact* tracked = Find(contact.id);
    if (tracked == nullptr) {
        return;
    }
    switch (contact.phase) {
        case TouchPhase::Moved: OnMoved(*tracked, point, contact.timestamp_us); break;
        case TouchPhase::Ended: OnEnded(contact, point); break;
        case TouchPhase::Cancelled: OnCancelled(contact, point); break;
        case TouchPhase::Began: break;
    }
}

void TouchGestureTranslator::OnBegan(const TouchContact& contact, GuestPoint point) {
    if (mode_ == Mode::Dragging) {
        return;
    }
    if (!Track(contact, point)) {
        return;
    }
    // A second finger while the first is still undecided starts a pan.
    mode_ = (mode_ == Mode::Idle) ? Mode::Pending : Mode::Panning;
}

void TouchGestureTranslator::OnMoved(TrackedContact& tracked, GuestPoint point,
                                     uint64_t timestamp_us) {
    const GuestPoint previous = tracked.last;
    tracked.last = point;
    if (mode_ == Mode::Panning) {
        // Each finger contributes half of the two-finger centroid motion.
        Emit(ActionType::Pan, point, timestamp_us,
             (point.x - previous.x) / 2.0f, (point.y - previous.y) / 2.0f);
        return;
    }
    if (mode_ == Mode::Pending && ExceedsSlop(tracked, point)) {
        mode_ = Mode::Dragging;
        Emit(ActionType::PointerDown, tracked.origin, timestamp_us);
    }
    if (mode_ == Mode::Dragging) {
        Emit(ActionType::PointerMove, point, timestamp_us);
    }
}

void TouchGestureTranslator::OnEnded(const TouchContact& contact, GuestPoint point) {
    const TrackedContact tracked = *Find(contact.id);
    Release(contact.id);
    if (mode_ == Mode::Pending) {
        ReleaseTap(tracked, point, contact.timestamp_us);
    }
    if (mode_ == Mode::Dragging) {
        Emit(ActionType::PointerUp, point, contact.timestamp_us);
    }
    if (!HasTrackedContacts()) {
        mode_ = Mode::Idle;
    }
}

void TouchGestureTranslator::OnCancelled(const TouchContact& contact, GuestPoint point) {
    Release(contact.id);
    // Never leave the guest mouse button stuck down.
    if (mode_ == Mode::Dragging) {
        Emit(ActionType::PointerUp, point, contact.timestamp_us);
    }
    if (!HasTrackedContacts()) {
        mode_ = Mode::Idle;
    }
}

void TouchGestureTranslator::ReleaseTap(const TrackedContact& tracked, GuestPoint point,
                                        uint64_t timestamp_us) {
    const bool long_press = timestamp_us - tracked.began_us >= config_.long_press_us;
    Emit(ActionType::PointerMove, point, timestamp_us);
    Emit(long_press ? ActionType::SecondaryClick : ActionType::PrimaryClick, point, timestamp_us);
}

void TouchGestureTranslator::Release(uint32_t id) {
    for (auto& slot : contacts_) {
        if (slot && slot->id == id) {
            slot.reset();
        }
    }
}

bool TouchGestureTranslator::Track(const TouchContact& contact, GuestPoint point) {
    for (auto& slot : contacts_) {
        if (!slot) {
            slot = TrackedContact{contact.id, point, point, contact.timestamp_us};
            return true;
        }
    }
    return false;
}

TouchGestureTranslator::TrackedContact* TouchGestureTranslator::Find(uint32_t id) {
    for (auto& slot : contacts_) {
        if (slot && slot->id == id) {
            return &*slot;
        }
    }
    return nullptr;
}

bool TouchGestureTranslator::HasTrackedContacts() const {
    for (const auto& slot : contacts_) {
        if (slot) {
            return true;
        }
    }
    return false;
}

bool TouchGestureTranslator::ExceedsSlop(const TrackedContact& tracked, GuestPoint point) const {
    return std::hypot(point.x - tracked.origin.x, point.y - tracked.origin.y) > config_.drag_slop_px;
}

void TouchGestureTranslator::Emit(ActionType type, GuestPoint point, uint64_t timestamp_us,
                                  float delta_x, float delta_y) {
    sink_.SubmitEvent(ActionEvent{
        .type = type,
        .x = point.x,
        .y = point.y,
        .delta_x = delta_x,
        .delta_y = delta_y,
        .timestamp_us = timestamp_us,
    });
}

} // namespace aoe2::input
