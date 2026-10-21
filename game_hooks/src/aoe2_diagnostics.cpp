// Diagnostic-build behavior, compiled only when AOE2_RUNTIME_DIAGNOSTICS is ON.
//
// A missing guest target normally logs and lets the run continue, which can
// hide a spin in place. Stopping at the first one makes the run end with
// [guest-branch:missing-target] in the log, which tools/classify_diagnostics.py
// reports as MISSING_FUNCTION. Shipped builds never include this file.
#include "game_overrides.h"
#include "ps2_runtime.h"

namespace {

void StopOnMissingFunction(PS2Runtime& runtime)
{
    runtime.setMissingFunctionPolicy(PS2Runtime::MissingFunctionPolicy::Stop);
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 diagnostics", "AOK.ELF", 0, 0, StopOnMissingFunction)
