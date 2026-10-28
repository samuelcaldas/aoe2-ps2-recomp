#pragma once

#include "ps2_runtime.h"

#include <map>
#include <string>

namespace aoe2::game_hooks {
using KcetHandlers = std::map<std::string, PS2Runtime::RecompiledFunction>;

// Adds host mouse replacements to the registry; addresses come from local configuration.
void RegisterMouseHandlers(KcetHandlers& handlers);
// Lets absolute pointing follow the frame size the runtime presents.
void SetMousePresentationSource(const PS2Runtime& runtime);
// Adds host keyboard replacements to the registry; addresses come from local configuration.
void RegisterKeyboardHandlers(KcetHandlers& handlers);
// Adds the i.LINK socket replacements (host socket model); addresses come from local configuration.
void RegisterNetworkHandlers(KcetHandlers& handlers);
// Adds host sound boundary replacements (SDD/SDCD RPC stubs); addresses come from local configuration.
void RegisterSoundHandlers(KcetHandlers& handlers);
}
