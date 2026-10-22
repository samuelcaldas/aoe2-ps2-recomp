#pragma once

#include "ps2_runtime.h"

#include <map>
#include <string>

namespace aoe2::game_hooks {
using KcetHandlers = std::map<std::string, PS2Runtime::RecompiledFunction>;

// Adds host mouse replacements to the registry; addresses come from local configuration.
void RegisterMouseHandlers(KcetHandlers& handlers);
// Adds host keyboard replacements to the registry; addresses come from local configuration.
void RegisterKeyboardHandlers(KcetHandlers& handlers);
// Adds the i.LINK socket replacements (host socket model); addresses come from local configuration.
void RegisterNetworkHandlers(KcetHandlers& handlers);
}
