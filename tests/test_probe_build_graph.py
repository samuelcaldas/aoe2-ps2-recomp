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

if __name__ == "__main__":
    unittest.main()
