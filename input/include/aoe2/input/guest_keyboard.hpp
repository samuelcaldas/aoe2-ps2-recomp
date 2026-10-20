#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace aoe2::input {

inline constexpr std::size_t kBootKeyboardKeySlots = 6;
inline constexpr uint8_t kUsageErrorRollOver = 0x01;
inline constexpr uint8_t kFirstModifierUsage = 0xE0;
inline constexpr uint8_t kLastModifierUsage = 0xE7;

// A key transition identified by its USB HID keyboard usage ID.
struct KeyEvent {
    uint8_t usage{0};
    bool pressed{false};
};

// USB HID boot-protocol keyboard report: what a USB keyboard delivers to the
// PS2 and what the recompiled game reads once per poll. Keys are listed in
// press order; more than six held keys report ErrorRollOver in every slot.
struct GuestKeyboardState {
    uint8_t modifiers{0};
    std::array<uint8_t, kBootKeyboardKeySlots> keys{};

    bool operator==(const GuestKeyboardState&) const = default;
};

// High-level replacement for the PS2 USB keyboard path: host key transitions
// (Switch docked USB keyboard, desktop keyboard) are folded into per-poll boot
// reports. Every transition is queued so a key pressed and released within one
// host frame is still visible to the guest for one poll.
class GuestKeyboardAdapter {
public:
    void Submit(const KeyEvent& event);

    // Advances one guest poll and returns the report the game should observe.
    GuestKeyboardState NextFrame();

private:
    bool Apply(const KeyEvent& event);
    GuestKeyboardState Snapshot() const;

    uint8_t modifiers_{0};
    std::vector<uint8_t> held_keys_;
    std::deque<GuestKeyboardState> pending_frames_;
};

// Returns the transitions between two host key snapshots indexed by HID usage
// (libnx HidKeyboardState uses the same indexing): releases first, then presses.
std::vector<KeyEvent> DiffPressedKeys(const std::bitset<256>& previous, const std::bitset<256>& current);

} // namespace aoe2::input
