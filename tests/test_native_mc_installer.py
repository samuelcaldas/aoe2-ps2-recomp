"""Execute the real MC installer with minimal runtime boundary doubles.

The installer is copied byte-for-byte beside lightweight headers so this test
requires neither the pinned runtime nor a full game build. The hook map remains
real; only external runtime/model interfaces are substituted.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SUPPORT = r'''
#pragma once
#include <array>
#include <cstdint>
inline bool mc_enabled = false;
inline int memory_constructions = 0;
inline int model_constructions = 0;
struct R5900Context { uint32_t pc = 0; };
inline void setReturnU32(R5900Context*, uint32_t) {}
inline uint32_t getRegU32(R5900Context*, int) { return 0; }
class PS2Runtime {
public:
    using RecompiledFunction = void (*)(uint8_t*, R5900Context*, PS2Runtime*);
    int queries = 0;
    int replacements = 0;
    bool hasFunction(uint32_t) { ++queries; return true; }
    bool replaceFunction(uint32_t, RecompiledFunction) { ++replacements; return true; }
};
#define PS2_REGISTER_GAME_OVERRIDE(...)
namespace aoe2::game_hooks {
struct RdramGuestAccess {
    RdramGuestAccess() { ++memory_constructions; }
    void Bind(uint8_t*) {}
};
namespace native_input {
struct Arguments { uint32_t U(int) const { return 0; } };
struct FunctionEntry { uint32_t address; PS2Runtime::RecompiledFunction handler; const char* name; };
struct Switches { bool Enabled(const char*) const { return mc_enabled; } };
inline Switches ReadSwitches() { return {}; }
}
}
namespace aoe2::mc {
struct KtMcModel {
    explicit KtMcModel(aoe2::game_hooks::RdramGuestAccess&) { ++model_constructions; }
};
}
'''

HARNESS = r'''
#include "kcet_mc_module.cpp"
#include <sstream>
#include <stdexcept>

void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char**) {
    try {
        PS2Runtime runtime;
        mc_enabled = argc > 1;
        std::ostringstream output;
        auto* previous = std::cout.rdbuf(output.rdbuf());
        bool rejected = false;
        std::string error;
        try { InstallNativeMc(runtime); }
        catch (const std::runtime_error& exception) {
            rejected = true;
            error = exception.what();
        }
        std::cout.rdbuf(previous);
        if (mc_enabled) {
            Check(rejected, "enabled MC must reject an empty unverified hook table");
            Check(error.find("unsupported") != std::string::npos, "error must explain unsupported status");
            Check(error.find("unverified") != std::string::npos, "error must explain verification gap");
            Check(error.find("memory_card=off") != std::string::npos, "error must provide recovery action");
            Check(output.str().find("installed") == std::string::npos, "failure must not report success");
        } else {
            Check(!rejected, "disabled MC must not throw");
            Check(output.str().find(": off") != std::string::npos, "disabled MC must report off");
        }
        Check(memory_constructions == 0, "must not construct guest access");
        Check(model_constructions == 0, "must not construct model");
        Check(!g_mc, "must not allocate or mutate module state");
        Check(runtime.queries == 0 && runtime.replacements == 0, "must not query or mutate runtime hooks");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''


class TestNativeMcInstaller(unittest.TestCase):
    def test_enabled_rejected_and_disabled_noop(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            staging = Path(directory)
            for name in ("kcet_mc_module.cpp", "kt_mc_entries.inc"):
                shutil.copyfile(ROOT / "game_hooks/src" / name, staging / name)
            (staging / "support.hpp").write_text(SUPPORT)
            for name in ("game_overrides.h", "guest_rdram_access.hpp",
                         "native_input_arguments.hpp", "native_input_module_support.hpp",
                         "ps2_runtime.h", "aoe2/mc/kt_mc_model.hpp"):
                header = staging / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "support.hpp"\n')
            (staging / "harness.cpp").write_text(HARNESS)
            executable = staging / "mc-installer-test"
            command = ["g++", "-std=c++20", f"-I{staging}",
                       str(staging / "harness.cpp"), "-o", str(executable)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for arguments in ([], ["enabled"]):
                with self.subTest(enabled=bool(arguments)):
                    result = subprocess.run([str(executable), *arguments], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
