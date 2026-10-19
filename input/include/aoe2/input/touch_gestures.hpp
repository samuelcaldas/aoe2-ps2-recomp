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
    // Hold time before a stationary release counts as a secondary (right) click.
    uint64_t long_press_us{500000};
};

// Turns touchscreen contacts into the pointer actions the PS2 build expects
// from its USB mouse:
//   tap               -> PointerMove + PrimaryClick
//   long press        -> PointerMove + SecondaryClick
//   one-finger drag   -> PointerDown / PointerMove... / PointerUp (box select)
//   two-finger drag   -> Pan (delta in guest pixels)
// Touch has no hover, so the cursor jumps to the contact point.
class TouchGestureTranslator {
public:
    TouchGestureTranslator(InputSink& sink, SurfaceMapping mapping, GestureConfig config = {});

    void Submit(const TouchContact& contact);

private:
    enum class Mode { Idle, Pending, Dragging, Panning };

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
    void Emit(ActionType type, GuestPoint point, uint64_t timestamp_us,
              float delta_x = 0.0f, float delta_y = 0.0f);

    InputSink& sink_;
    SurfaceMapping mapping_;
    GestureConfig config_;
    Mode mode_{Mode::Idle};
    std::array<std::optional<TrackedContact>, 2> contacts_{};
};

} // namespace aoe2::input
