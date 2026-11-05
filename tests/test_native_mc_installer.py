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
#include <set>
inline bool mc_enabled = false;
struct R5900Context { uint32_t pc = 0; };
inline uint32_t getRegU32(R5900Context*, int) { return 0; }
inline void setReturnS32(R5900Context*, int32_t) {}
class PS2Runtime {
public:
    using RecompiledFunction = void (*)(uint8_t*, R5900Context*, PS2Runtime*);
    int queries = 0;
    std::set<uint32_t> replaced;
    bool hasFunction(uint32_t) { ++queries; return true; }
    bool replaceFunction(uint32_t address, RecompiledFunction) { replaced.insert(address); return true; }
};
#define PS2_REGISTER_GAME_OVERRIDE(...)
namespace aoe2::game_hooks::native_input {
struct Switches { bool Enabled(const char*) const { return mc_enabled; } };
inline Switches ReadSwitches() { return {}; }
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
        InstallNativeMc(runtime);
        std::cout.rdbuf(previous);
        if (mc_enabled) {
            const std::set<uint32_t> expected{
                0x00110A78, 0x00110940, 0x00110C00, 0x001102A0,
                0x00110410, 0x001104C8, 0x00110630, 0x00110750,
                0x00111120, 0x001103D8, 0x00110DE0, 0x001111D8,
                0x00110FF8, 0x00110F30, 0x00111518, 0x001115E0,
                0x001113B0
            };
            Check(runtime.replaced == expected, "enabled MC must replace exactly the 17 routed libmc entries");
            Check(output.str().find("installed 17/17") != std::string::npos, "enabled MC must report 17/17");
        } else {
            Check(output.str().find(": off") != std::string::npos, "disabled MC must report off");
            Check(runtime.queries == 0 && runtime.replaced.empty(), "disabled MC must not touch runtime hooks");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''


class TestNativeMcInstaller(unittest.TestCase):
    def test_enabled_installs_libmc_and_disabled_noop(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            staging = Path(directory)
            for name in ("kcet_mc_module.cpp",):
                shutil.copyfile(ROOT / "game_hooks/src" / name, staging / name)
            (staging / "support.hpp").write_text(SUPPORT)
            for name in ("game_overrides.h", "native_input_module_support.hpp",
                         "ps2_runtime.h"):
                header = staging / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#include "support.hpp"\n')
            (staging / "harness.cpp").write_text(HARNESS)
            executable = staging / "mc-installer-test"
            command = ["g++", "-std=c++20", f"-I{staging}", f"-I{ROOT / 'mc/include'}",
                       f"-I{ROOT / 'runtime/include'}",
                       str(ROOT / "mc/src/native_host_mc.cpp"),
                       str(staging / "harness.cpp"), "-o", str(executable)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for arguments in ([], ["enabled"]):
                with self.subTest(enabled=bool(arguments)):
                    result = subprocess.run([str(executable), *arguments], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
