#include <aoe2/input/actions.hpp>

namespace aoe2::input {

const char* ToString(ActionType action) {
    switch (action) {
        case ActionType::PointerDown: return "PointerDown";
        case ActionType::PointerMove: return "PointerMove";
        case ActionType::PointerUp: return "PointerUp";
        case ActionType::PrimaryClick: return "PrimaryClick";
        case ActionType::SecondaryClick: return "SecondaryClick";
        case ActionType::Pan: return "Pan";
        case ActionType::Zoom: return "Zoom";
        case ActionType::MenuAccept: return "MenuAccept";
        case ActionType::MenuBack: return "MenuBack";
        case ActionType::Pause: return "Pause";
    }
    return "Unknown";
}

} // namespace aoe2::input
