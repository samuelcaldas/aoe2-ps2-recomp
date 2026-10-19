#pragma once

#include <cstdint>
#include <string>

namespace aoe2::input {

enum class ActionType : uint32_t {
    PointerDown,
    PointerMove,
    PointerUp,
    PrimaryClick,
    SecondaryClick,
    Pan,
    Zoom,
    MenuAccept,
    MenuBack,
    Pause
};

struct ActionEvent {
    ActionType type;
    float x{0.0f};
    float y{0.0f};
    float delta_x{0.0f};
    float delta_y{0.0f};
    uint64_t timestamp_us{0};
};

class InputSink {
public:
    virtual ~InputSink() = default;
    virtual void SubmitEvent(const ActionEvent& event) = 0;
};

const char* ToString(ActionType action);

} // namespace aoe2::input
