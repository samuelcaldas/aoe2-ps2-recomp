import unittest

from tests.test_kcet_resolver_keyboard import BASE, EPILOGUE, PROLOGUE, function, synthetic_program
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.mips import addiu, jal, lui, ori
from tools.kcet_resolver.sound import (
    _SDCD_CLIENT,
    _SDCD_DEV,
    _SDD_CLIENT,
    _SDD_DEV,
    _CMD_OPEN_STREAM,
    _CMD_QUERY_STATUS,
    _REG_A1,
    resolve_sound_hooks,
)

_CALL_TARGET = BASE + 0xF00


def set_a1(n):
    """addiu $a1, $zero, n  (n fits in 16-bit signed)."""
    return addiu(_REG_A1, 0, n & 0xFFFF)


def build_const(value):
    """lui + ori to load a 32-bit constant into $r2 (not $a1)."""
    hi = (value >> 16) & 0xFFFF
    lo = value & 0xFFFF
    return [lui(2, hi), ori(2, 2, lo)]


def init_body():
    """Both device IDs (0x20 and 0x30) loaded into a1 before two JAL calls."""
    return function(
        set_a1(_SDCD_DEV), jal(_CALL_TARGET), 0,
        set_a1(_SDD_DEV),  jal(_CALL_TARGET), 0,
    )


def open_stream_body():
    """Builds SDCD client constant + cmd 0x10 in a1 before JAL."""
    return function(*build_const(_SDCD_CLIENT), set_a1(_CMD_OPEN_STREAM), jal(_CALL_TARGET), 0)


def query_status_body():
    """Builds SDD client constant + cmd 3 in a1 before JAL."""
    return function(*build_const(_SDD_CLIENT), set_a1(_CMD_QUERY_STATUS), jal(_CALL_TARGET), 0)


def synthetic_sound():
    return synthetic_program([init_body(), open_stream_body(), query_status_body()])


class TestResolveSoundHooks(unittest.TestCase):
    def test_resolves_all_three_roles(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        self.assertEqual(set(hooks), {
            "kcet.sound.init",
            "kcet.sound.open_stream",
            "kcet.sound.query_status",
        })

    def test_all_three_addresses_are_distinct(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        self.assertEqual(len(set(hooks.values())), 3)

    def test_init_is_the_function_with_both_device_ids_in_a1(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        # init is the first function; its start is BASE.
        self.assertEqual(hooks["kcet.sound.init"], BASE)
        for role in ("kcet.sound.open_stream", "kcet.sound.query_status"):
            self.assertNotEqual(hooks[role], BASE)

    def test_open_stream_and_query_status_are_distinct(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        self.assertNotEqual(hooks["kcet.sound.open_stream"], hooks["kcet.sound.query_status"])

    def test_missing_init_fails(self):
        # Only wrappers, no function carrying both device IDs in a1.
        image, functions = synthetic_program([open_stream_body(), query_status_body()])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_duplicate_init_fails(self):
        image, functions = synthetic_program([
            init_body(), init_body(),
            open_stream_body(), query_status_body(),
        ])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_missing_query_status_fails(self):
        # No function with SDD client + cmd 3.
        image, functions = synthetic_program([init_body(), open_stream_body()])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_missing_open_stream_fails(self):
        # No function with SDCD client + cmd 0x10.
        image, functions = synthetic_program([init_body(), query_status_body()])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_extra_function_with_same_cmd_does_not_confuse_resolver(self):
        # An unrelated function that also calls with a1=3 but without the SDD client.
        unrelated = function(set_a1(_CMD_QUERY_STATUS), jal(_CALL_TARGET), 0)
        image, functions = synthetic_program([
            init_body(), open_stream_body(), query_status_body(), unrelated,
        ])
        # Must still resolve; query_status is the only one with both the SDD client AND a1=3.
        hooks = resolve_sound_hooks(image, functions)
        self.assertIn("kcet.sound.query_status", hooks)


if __name__ == "__main__":
    unittest.main()
