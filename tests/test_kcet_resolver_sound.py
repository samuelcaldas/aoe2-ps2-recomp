import unittest

from tests.test_kcet_resolver_keyboard import BASE, EPILOGUE, PROLOGUE, function, synthetic_program
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.mips import addiu, jal
from tools.kcet_resolver.sound import (
    _SDCD_DEV,
    _SDD_DEV,
    _CMD_OPEN_STREAM,
    _CMD_QUERY_STATUS,
    _REG_A1,
    resolve_sound_hooks,
)

# SDD/SDCD device ids and command codes are small enough for addiu $a1, $zero, N directly.
_CALL_TARGET = BASE + 0xF00


def set_a1(n):
    """addiu $a1, $zero, n  (n must fit in 16-bit signed)."""
    return addiu(_REG_A1, 0, n & 0xFFFF)


def init_body():
    """Both SDCD (0x20) and SDD (0x30) in a1, two JAL calls."""
    return function(
        set_a1(_SDCD_DEV), jal(_CALL_TARGET), 0,
        set_a1(_SDD_DEV),   jal(_CALL_TARGET), 0,
    )


def open_stream_body():
    """SDCD (0x20) plus cmd 0x10 in a1, one JAL call."""
    return function(set_a1(_SDCD_DEV), set_a1(_CMD_OPEN_STREAM), jal(_CALL_TARGET), 0)


def query_status_body():
    """SDD (0x30) plus cmd 3 in a1, one JAL call."""
    return function(set_a1(_SDD_DEV), set_a1(_CMD_QUERY_STATUS), jal(_CALL_TARGET), 0)


def query_volume_body():
    """SDD (0x30) plus some other cmd (5) in a1, one JAL call."""
    return function(set_a1(_SDD_DEV), set_a1(5), jal(_CALL_TARGET), 0)


def synthetic_sound():
    return synthetic_program([init_body(), open_stream_body(), query_status_body(), query_volume_body()])


class TestResolveSoundHooks(unittest.TestCase):
    def test_resolves_all_four_roles(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        self.assertEqual(set(hooks), {
            "kcet.sound.init",
            "kcet.sound.open_stream",
            "kcet.sound.query_status",
            "kcet.sound.query_volume",
        })

    def test_all_four_addresses_are_distinct(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        self.assertEqual(len(set(hooks.values())), 4)

    def test_init_is_the_function_with_both_sids(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        # init is the first function; its start is BASE.
        self.assertEqual(hooks["kcet.sound.init"], BASE)
        # The other three must NOT be init.
        for role in ("kcet.sound.open_stream", "kcet.sound.query_status", "kcet.sound.query_volume"):
            self.assertNotEqual(hooks[role], BASE)

    def test_query_status_distinct_from_query_volume(self):
        image, functions = synthetic_sound()
        hooks = resolve_sound_hooks(image, functions)
        self.assertNotEqual(hooks["kcet.sound.query_status"], hooks["kcet.sound.query_volume"])

    def test_missing_init_fails(self):
        # Only wrappers, no function carrying both SIDs.
        image, functions = synthetic_program([
            open_stream_body(), query_status_body(), query_volume_body(),
        ])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_duplicate_init_fails(self):
        # Two functions carrying both SIDs.
        image, functions = synthetic_program([
            init_body(), init_body(),
            open_stream_body(), query_status_body(), query_volume_body(),
        ])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_missing_query_status_fails(self):
        # No function with SDD dev + cmd 3.
        image, functions = synthetic_program([
            init_body(), open_stream_body(), query_volume_body(),
        ])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)

    def test_missing_open_stream_fails(self):
        # No function with SDCD dev + cmd 0x10.
        image, functions = synthetic_program([
            init_body(), query_status_body(), query_volume_body(),
        ])
        with self.assertRaises(HookResolutionError):
            resolve_sound_hooks(image, functions)


if __name__ == "__main__":
    unittest.main()
