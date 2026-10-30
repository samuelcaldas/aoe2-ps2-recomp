#pragma once

#include <string_view>

class PS2Runtime;

namespace aoe2::game_hooks {

bool LoadModularOverlayOnDemand(PS2Runtime& runtime, std::string_view filename);

}  // namespace aoe2::game_hooks
