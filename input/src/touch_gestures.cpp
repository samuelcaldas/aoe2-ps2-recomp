#include <aoe2/input/touch_gestures.hpp>

#include <cmath>

namespace aoe2::input {

TouchGestureTranslator::TouchGestureTranslator(InputSink& sink, SurfaceMapping mapping,
                                               GestureConfig config)
    : sink_(sink), mapping_(mapping), config_(config) {}

void TouchGestureTranslator::Submit(const TouchContact& contact) {
    GuestPoint point = mapping_.ToGuest(contact.x, contact.y);
    if (config_.vertical_target_offset_px > 0.0f) {
        point.y = std::max(0.0f, point.y - config_.vertical_target_offset_px);
    }
    is_touch_active_ = true;
    modality_ = TouchInteractionModality::Touch;

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
    if (mode_ == Mode::Idle) {
        mode_ = Mode::Pending;
        two_finger_candidate_ = false;
    } else if (mode_ == Mode::Pending) {
        mode_ = Mode::Panning;
        two_finger_candidate_ = true;
        two_finger_began_us_ = contact.timestamp_us;
    }
}

void TouchGestureTranslator::OnMoved(TrackedContact& tracked, GuestPoint point,
                                     uint64_t timestamp_us) {
    const GuestPoint previous = tracked.last;
    tracked.last = point;
    if (mode_ == Mode::Panning) {
        // Each finger contributes half of the two-finger centroid motion.
        Emit(ActionType::Pan, point, timestamp_us,
             (point.x - previous.x) / 2.0f, (point.y - previous.y) / 2.0f);
        if (ExceedsSlop(tracked, point)) {
            two_finger_candidate_ = false;
        }
        return;
    }
    if (mode_ == Mode::Pending && ExceedsSlop(tracked, point)) {
        if (config_.one_finger_pan) {
            mode_ = Mode::Panning;
            Emit(ActionType::Pan, point, timestamp_us,
                 point.x - previous.x, point.y - previous.y);
        } else {
            mode_ = Mode::Dragging;
            Emit(ActionType::PointerDown, tracked.origin, timestamp_us);
        }
    }
    if (mode_ == Mode::Dragging) {
        Emit(ActionType::PointerMove, point, timestamp_us);
    }
}

void TouchGestureTranslator::OnEnded(const TouchContact& contact, GuestPoint point) {
    TrackedContact* found = Find(contact.id);
    if (found == nullptr) {
        return;
    }
    const TrackedContact tracked = *found;
    Release(contact.id);

    if (two_finger_candidate_ && mode_ == Mode::Panning) {
        if (contact.timestamp_us - two_finger_began_us_ < config_.long_press_us) {
            Emit(ActionType::AttackMove, point, contact.timestamp_us);
            two_finger_candidate_ = false;
        }
    } else if (mode_ == Mode::Pending) {
        ReleaseTap(tracked, point, contact.timestamp_us);
    }

    if (mode_ == Mode::Dragging) {
        Emit(ActionType::PointerUp, point, contact.timestamp_us);
    }
    if (!HasTrackedContacts()) {
        mode_ = Mode::Idle;
        two_finger_candidate_ = false;
        is_touch_active_ = false;
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
        two_finger_candidate_ = false;
        is_touch_active_ = false;
    }
}

void TouchGestureTranslator::ReleaseTap(const TrackedContact& tracked, GuestPoint point,
                                        uint64_t timestamp_us) {
    const bool long_press = timestamp_us - tracked.began_us >= config_.long_press_us;
    Emit(ActionType::PointerMove, point, timestamp_us);

    if (long_press) {
        Emit(ActionType::SecondaryClick, point, timestamp_us);
        last_tap_point_.reset();
        last_tap_timestamp_us_ = 0;
        return;
    }

    if (IsDoubleTap(point, timestamp_us)) {
        Emit(ActionType::DoubleClick, point, timestamp_us);
        last_tap_point_.reset();
        last_tap_timestamp_us_ = 0;
        return;
    }

    last_tap_point_ = point;
    last_tap_timestamp_us_ = timestamp_us;

    if (has_active_selection_ && touch_region_ == TouchRegion::Map) {
        Emit(ActionType::SecondaryClick, point, timestamp_us);
    } else {
        Emit(ActionType::PrimaryClick, point, timestamp_us);
    }
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

bool TouchGestureTranslator::IsDoubleTap(GuestPoint point, uint64_t timestamp_us) const {
    if (!last_tap_point_ || last_tap_timestamp_us_ == 0) {
        return false;
    }
    const uint64_t elapsed_us = timestamp_us - last_tap_timestamp_us_;
    if (elapsed_us > config_.double_tap_threshold_us) {
        return false;
    }
    const float dist = std::hypot(point.x - last_tap_point_->x, point.y - last_tap_point_->y);
    return dist <= config_.double_tap_max_distance_px;
}

void TouchGestureTranslator::SetHasActiveSelection(bool has_selection) {
    has_active_selection_ = has_selection;
}

bool TouchGestureTranslator::HasActiveSelection() const {
    return has_active_selection_;
}

void TouchGestureTranslator::SetTouchRegion(TouchRegion region) {
    touch_region_ = region;
}

TouchRegion TouchGestureTranslator::GetTouchRegion() const {
    return touch_region_;
}

void TouchGestureTranslator::SetOneFingerPan(bool enable) {
    config_.one_finger_pan = enable;
}

bool TouchGestureTranslator::IsOneFingerPan() const {
    return config_.one_finger_pan;
}

void TouchGestureTranslator::SetVerticalTargetOffset(float offset_px) {
    config_.vertical_target_offset_px = offset_px;
}

float TouchGestureTranslator::GetVerticalTargetOffset() const {
    return config_.vertical_target_offset_px;
}

bool TouchGestureTranslator::IsTouchActive() const {
    return is_touch_active_;
}

TouchInteractionModality TouchGestureTranslator::GetModality() const {
    return modality_;
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
