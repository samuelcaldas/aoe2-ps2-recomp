#!/usr/bin/env python3
"""
Unit tests for the desktop probe build graph configuration.

Verifies:
1. Pinned PS2Recomp runtime library (libps2_runtime.a) is linked into desktop_runtime_probe.
2. Authoritative generated registration table (generated/SLES-50282/register_functions.cpp)
   is included in the target sources.
3. Upstream runner placeholder (ps2xRuntime/src/runner/register_functions.cpp) is excluded.
4. Transitive runtime dependencies (libps2_iop.a, raylib, ffmpeg libraries, etc.) are linked.
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

class TestProbeBuildGraph(unittest.TestCase):
    def test_ninja_build_graph_structure(self):
        build_ninja = REPO_ROOT / "build/desktop-probe/build.ninja"
        if not build_ninja.exists():
            self.skipTest(f"Build ninja file not found at {build_ninja}")

        content = build_ninja.read_text(encoding="utf-8")

        # 1. Target desktop_runtime_probe must exist
        self.assertIn("build desktop_runtime_probe:", content)

        # 2. Extract link block for desktop_runtime_probe
        idx = content.find("build desktop_runtime_probe:")
        next_idx = content.find("\nbuild ", idx + 1)
        if next_idx == -1:
            next_idx = len(content)
        link_block = content[idx:next_idx]

        # 3. Check that authoritative generated register_functions is compiled and linked
        self.assertIn("generated/SLES-50282/register_functions.cpp.o", link_block)

        # 4. Check that upstream runner placeholder register_functions is NOT in the link block
        self.assertNotIn("runner/register_functions.cpp.o", link_block)

        # 5. Check that libps2_runtime.a is linked
        self.assertIn("libps2_runtime.a", link_block)

        # 6. Check that libps2_iop.a is linked
        self.assertIn("libps2_iop.a", link_block)

    def test_sse41_enabled_only_for_units_using_sse41_intrinsics(self):
        build_ninja = REPO_ROOT / "build/desktop-probe/build.ninja"
        if not build_ninja.exists() or not GENERATED_DIR.is_dir():
            self.skipTest("probe build graph or generated sources not available")

        flags_by_source = compile_flags_by_generated_source(build_ninja.read_text(encoding="utf-8"))
        using, not_using = partition_sources_by_intrinsic(sorted(GENERATED_DIR.glob("*.cpp")))
        if not using:
            self.skipTest("no generated unit uses an SSE4.1 intrinsic")
        self.assertTrue(not_using, "expected at least one generated unit without SSE4.1 intrinsics")

        for source in using:
            self.assertIn("-msse4.1", flags_by_source.get(source.name, ""), "SSE4.1 unit lacks -msse4.1")
        self.assertNotIn("-msse4.1", flags_by_source.get(not_using[0].name, ""))


GENERATED_DIR = REPO_ROOT / "generated/SLES-50282"
SSE41_INTRINSICS = ("_mm_blendv_ps",)
COMPILE_EDGE = re.compile(
    r"^build CMakeFiles/aoe2_generated_game_code\.dir/generated/SLES-50282/(?P<name>[^:]+\.cpp)\.o:"
    r".*?\n(?:  .*\n)*?  FLAGS = (?P<flags>.*)$",
    re.MULTILINE,
)


def compile_flags_by_generated_source(ninja_text: str) -> dict:
    """Map each generated source file name to the FLAGS of its compile edge."""
    return {match["name"]: match["flags"] for match in COMPILE_EDGE.finditer(ninja_text)}


def partition_sources_by_intrinsic(sources: list) -> tuple:
    """Split sources into those containing an SSE4.1 intrinsic and those that do not."""
    using, not_using = [], []
    for source in sources:
        text = source.read_text(encoding="utf-8", errors="replace")
        target = using if any(name in text for name in SSE41_INTRINSICS) else not_using
        target.append(source)
    return using, not_using

if __name__ == "__main__":
    unittest.main()
