#include <aoe2/input/guest_keyboard.hpp>

#include <algorithm>

namespace aoe2::input {

namespace {

bool IsModifier(uint8_t usage) {
    return usage >= kFirstModifierUsage && usage <= kLastModifierUsage;
}

uint8_t ModifierBit(uint8_t usage) {
    return static_cast<uint8_t>(1u << (usage - kFirstModifierUsage));
}

} // namespace

void GuestKeyboardAdapter::Submit(const KeyEvent& event) {
    if (Apply(event)) {
        pending_frames_.push_back(Snapshot());
    }
}

GuestKeyboardState GuestKeyboardAdapter::NextFrame() {
    if (pending_frames_.empty()) {
        return Snapshot();
    }
    const GuestKeyboardState frame = pending_frames_.front();
    pending_frames_.pop_front();
    return frame;
}

bool GuestKeyboardAdapter::Apply(const KeyEvent& event) {
    if (event.usage == 0) {
        return false;
    }
    if (IsModifier(event.usage)) {
        const uint8_t before = modifiers_;
        modifiers_ = event.pressed ? (modifiers_ | ModifierBit(event.usage)) : (modifiers_ & ~ModifierBit(event.usage));
        return modifiers_ != before;
    }
    const auto held = std::find(held_keys_.begin(), held_keys_.end(), event.usage);
    if (event.pressed == (held != held_keys_.end())) {
        return false;
    }
    if (event.pressed) {
        held_keys_.push_back(event.usage);
        return true;
    }
    held_keys_.erase(held);
    return true;
}

GuestKeyboardState GuestKeyboardAdapter::Snapshot() const {
    GuestKeyboardState state{.modifiers = modifiers_};
    if (held_keys_.size() > kBootKeyboardKeySlots) {
        state.keys.fill(kUsageErrorRollOver);
        return state;
    }
    std::copy(held_keys_.begin(), held_keys_.end(), state.keys.begin());
    return state;
}

std::vector<KeyEvent> DiffPressedKeys(const std::bitset<256>& previous, const std::bitset<256>& current) {
    std::vector<KeyEvent> events;
    const std::bitset<256> released = previous & ~current;
    const std::bitset<256> pressed = current & ~previous;
    for (std::size_t usage = 0; usage < released.size(); ++usage) {
        if (released.test(usage)) {
            events.push_back(KeyEvent{static_cast<uint8_t>(usage), false});
        }
    }
    for (std::size_t usage = 0; usage < pressed.size(); ++usage) {
        if (pressed.test(usage)) {
            events.push_back(KeyEvent{static_cast<uint8_t>(usage), true});
        }
    }
    return events;
}

} // namespace aoe2::input
