#pragma once

#include <aoe2/input/actions.hpp>
#include <aoe2/input/surface_mapping.hpp>

#include <deque>

namespace aoe2::input {

// Snapshot of the PS2 USB mouse as the recompiled game polls it once per frame.
struct GuestMouseState {
    float x{0.0f};
    float y{0.0f};
    bool left{false};
    bool right{false};
};

// High-level replacement for the PS2 USB mouse path (IOP driver over SIF):
// pointer actions from any host source (Switch touch, desktop mouse) are folded
// into the per-frame state the guest reads, instead of emulating USB HID.
// Every button transition is queued so each one is visible to the guest for at
// least one poll, even when a tap presses and releases within a single frame.
class GuestMouseAdapter : public InputSink {
public:
    explicit GuestMouseAdapter(GuestPoint initial_position = {});

    void SubmitEvent(const ActionEvent& event) override;

    // Advances one guest poll and returns the state the game should observe.
    GuestMouseState NextFrame();

private:
    void MoveTo(const ActionEvent& event);
    void SetButtons(bool left, bool right);

    GuestMouseState current_;
    std::deque<GuestMouseState> pending_frames_;
};

} // namespace aoe2::input
