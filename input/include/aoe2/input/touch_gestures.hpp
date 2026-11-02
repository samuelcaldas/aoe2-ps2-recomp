#pragma once

#include <aoe2/input/actions.hpp>
#include <aoe2/input/surface_mapping.hpp>

#include <array>
#include <cstdint>
#include <optional>

namespace aoe2::input {

enum class TouchPhase : uint32_t {
    Began,
    Moved,
    Ended,
    Cancelled
};

// One raw touchscreen contact sample in host surface coordinates. Platform
// layers (libnx HID on Switch, SDL touch on desktop) produce these.
struct TouchContact {
    uint32_t id{0};
    float x{0.0f};
    float y{0.0f};
    TouchPhase phase{TouchPhase::Began};
    uint64_t timestamp_us{0};
};

struct GestureConfig {
    // Movement (guest pixels) before a touch becomes a drag instead of a tap.
    float drag_slop_px{8.0f};
    // Hold time before a stationary contact becomes a secondary-button gesture.
    uint64_t long_press_us{500000};
    // Double-tap maximum duration between releases (microseconds).
    uint64_t double_tap_threshold_us{280000};
    // Double-tap maximum spatial separation (guest pixels).
    float double_tap_max_distance_px{16.0f};
    // Vertical targeting offset (guest pixels) so the finger doesn't obscure the target tile.
    float vertical_target_offset_px{0.0f};
};

// Turns touchscreen contacts into the pointer actions the PS2 build expects
// from its USB mouse:
//   tap               -> PointerMove + PrimaryClick
//   double tap        -> PointerMove + DoubleClick (mass unit select)
//   long press        -> SecondaryDown ... SecondaryUp
//   one-finger drag   -> PointerDown / PointerMove... / PointerUp (box select)
//   multi-touch       -> cancel the active gesture without leaving a button held
// Touch has no hover, so the cursor jumps to the contact point.
class TouchGestureTranslator {
public:
    TouchGestureTranslator(InputSink& sink, SurfaceMapping mapping, GestureConfig config = {});

    void Submit(const TouchContact& contact);

    void SetVerticalTargetOffset(float offset_px);
    float GetVerticalTargetOffset() const;

    bool IsTouchActive() const;
    TouchInteractionModality GetModality() const;

private:
    enum class Mode { Idle, Pending, Dragging, SecondaryDragging, Suppressed };

    struct TrackedContact {
        uint32_t id;
        GuestPoint origin;
        GuestPoint last;
        uint64_t began_us;
    };

    void OnBegan(const TouchContact& contact, GuestPoint point);
    void OnMoved(TrackedContact& tracked, GuestPoint point, uint64_t timestamp_us);
    void OnEnded(const TouchContact& contact, GuestPoint point);
    void OnCancelled(const TouchContact& contact, GuestPoint point);
    void ReleaseTap(const TrackedContact& tracked, GuestPoint point, uint64_t timestamp_us);
    void Release(uint32_t id);

    bool Track(const TouchContact& contact, GuestPoint point);
    TrackedContact* Find(uint32_t id);
    bool HasTrackedContacts() const;
    bool ExceedsSlop(const TrackedContact& tracked, GuestPoint point) const;
    bool IsDoubleTap(GuestPoint point, uint64_t timestamp_us) const;
    void Emit(ActionType type, GuestPoint point, uint64_t timestamp_us,
              float delta_x = 0.0f, float delta_y = 0.0f);

    InputSink& sink_;
    SurfaceMapping mapping_;
    GestureConfig config_;
    Mode mode_{Mode::Idle};
    std::array<std::optional<TrackedContact>, 2> contacts_{};

    // Double-tap tracking
    std::optional<GuestPoint> last_tap_point_;
    uint64_t last_tap_timestamp_us_{0};

    // Modality & liveness
    bool is_touch_active_{false};
    TouchInteractionModality modality_{TouchInteractionModality::Touch};
};

} // namespace aoe2::input
