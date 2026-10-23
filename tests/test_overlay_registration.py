import tempfile
import unittest
from pathlib import Path

from tools.overlay_registration import (
    OverlayRegistrationError, build_registration, build_rename_header, parse_register_file)

REGISTER = """#include "ps2_runtime.h"
extern const uint32_t g_ps2RecompiledFunctionTableBase = 0x1d984b0u;
namespace {
struct GeneratedFunctionTableInitializer {
    GeneratedFunctionTableInitializer() {
        g_ps2RecompiledFunctionTable[0] = FUN_01d984b0_0x1d984b0; // 0x1d984b0
        g_ps2RecompiledFunctionTable[96] = FUN_01d98630_0x1d98630; // 0x1d98630
        g_ps2RecompiledFunctionTable[97] = FUN_01d98630_0x1d98630; // 0x1d98634
    }
};
}
"""
HEADER = """void FUN_01d984b0_0x1d984b0(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);
void FUN_01d98630_0x1d98630(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);
"""


class TestOverlayRegistration(unittest.TestCase):
    def test_parses_every_address_to_function_pair(self):
        self.assertEqual(parse_register_file(REGISTER), [
            (0x1D984B0, "FUN_01d984b0_0x1d984b0"),
            (0x1D98630, "FUN_01d98630_0x1d98630"),
            (0x1D98634, "FUN_01d98630_0x1d98630")])

    def test_empty_or_unrecognized_register_file_fails(self):
        with self.assertRaises(OverlayRegistrationError):
            parse_register_file("int main() {}")

    def test_rename_header_prefixes_each_declared_function_once(self):
        header = build_rename_header(HEADER, "ov4_")
        self.assertIn("#define FUN_01d984b0_0x1d984b0 ov4_FUN_01d984b0_0x1d984b0", header)
        self.assertIn("#define FUN_01d98630_0x1d98630 ov4_FUN_01d98630_0x1d98630", header)
        self.assertEqual(header.count("#define FUN_01d98630_0x1d98630 "), 1)

    def test_registration_uses_overlay_api_with_prefixed_names(self):
        source = build_registration(parse_register_file(REGISTER), 4, "ov4_", "multimcd")
        self.assertIn("runtime.registerOverlayFunction(4u, 0x1d98634u, ov4_FUN_01d98630_0x1d98630)", source)
        self.assertIn("void RegisterOverlay_multimcd(PS2Runtime& runtime)", source)
        self.assertNotIn("g_ps2RecompiledFunctionTable", source)

    def test_registration_failure_is_reported_not_ignored(self):
        source = build_registration(parse_register_file(REGISTER), 4, "ov4_", "multimcd")
        self.assertIn("throw std::runtime_error", source)

    def test_rejects_unsafe_prefix_or_name(self):
        pairs = parse_register_file(REGISTER)
        for prefix in ("", "4x_", "ov 4_"):
            with self.assertRaises(OverlayRegistrationError):
                build_registration(pairs, 4, prefix, "multimcd")
        with self.assertRaises(OverlayRegistrationError):
            build_registration(pairs, 4, "ov4_", "bad name")


if __name__ == "__main__":
    unittest.main()
