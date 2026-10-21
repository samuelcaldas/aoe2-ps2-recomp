#!/usr/bin/env python3
"""Tests for tools/resolve_kcet_hooks.py on synthetic decompiled C."""
import tempfile
import unittest
from pathlib import Path

from tools import resolve_kcet_hooks as resolver

SYNTHETIC_MOUSE_SOURCE = """
int func_00AAA000(void) {
    func_000F0000(0);
    func_000F1000(&D_DEVICE, 0x80000210, 0);
    return D_DEVICE.status < 1;
}

int func_00AAA100(void *a0) {
    return func_000F2000(&D_DEVICE, 1, 0, D_BUF, 0x20, a0, 0x20, 0, 0);
}

int func_00AAA200(int a0, void *a1) {
    D_BUF[0] = a0;
    return func_000F2000(&D_DEVICE, 2, 0, D_BUF, 0x20, a1, 0x20, 0, 0);
}

int func_00AAA300(int a0, void *a1) {
    D_BUF[0] = a0;
    return func_000F2000(&D_DEVICE, 3, 0, D_BUF, 0x20, a1, 0x20, 0, 0);
}
"""


class TestResolveKcetHooks(unittest.TestCase):
    def test_finds_bind_and_rpc_wrappers_by_structure(self):
        hooks = resolver.resolve_mouse_hooks(SYNTHETIC_MOUSE_SOURCE)
        self.assertEqual(hooks, {
            "kcet.mouse.bind": 0x00AAA000,
            "kcet.mouse.get_info": 0x00AAA100,
            "kcet.mouse.read": 0x00AAA200,
            "kcet.mouse.device_info": 0x00AAA300,
        })

    def test_missing_wrapper_fails(self):
        source = SYNTHETIC_MOUSE_SOURCE.replace("&D_DEVICE, 3,", "&D_OTHER, 3,")
        with self.assertRaises(resolver.HookResolutionError):
            resolver.resolve_mouse_hooks(source)

    def test_writes_ini_with_hex_addresses(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "kcet_hooks.ini"
            resolver.write_hook_file(output, {"kcet.mouse.bind": 0x00AAA000}, source_revision="abc123")
            text = output.read_text()
        self.assertIn("kcet.mouse.bind=0x00aaa000", text)
        self.assertIn("abc123", text)


if __name__ == "__main__":
    unittest.main()
