#!/usr/bin/env python3
"""ELF-anchored resolution of the KCET USB mouse RPC layer, on a synthetic image.

The synthetic layout mirrors what the owned ELF contains: one bind function that
builds the SIF RPC client global and the USB mouse service id, and three wrappers
that build the same client and call it with RPC functions 1, 2, and 3.
"""
import unittest

from tests.test_kcet_resolver_elf import PF_RX, build_elf, code
from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import JR_RA, addiu, jal, lui, ori
from tools.kcet_resolver.mouse import HookResolutionError, resolve_mouse_hooks

BASE = 0x100000
CLIENT = 0x00510000
SID = 0x80000210
PROLOGUE = addiu(29, 29, 0xFFE0)
EPILOGUE = [JR_RA, addiu(29, 29, 0x20)]
BIND_BYTES = 4 * (1 + 4 + 2)  # prologue + four body words + epilogue
WRAPPER_BYTES = 4 * (1 + 5 + 2)  # prologue + five body words + epilogue


def function(*words: int) -> list:
    return [PROLOGUE, *words, *EPILOGUE]


def build_mouse_image(bind_extra: list | None = None, wrapper_rpc: tuple = (1, 2, 3)) -> tuple:
    """Return (ElfImage, FunctionMap) for a synthetic mouse layer; wrappers follow bind."""
    bind = function(lui(16, CLIENT >> 16), addiu(16, 16, 0), lui(5, SID >> 16), ori(5, 5, SID & 0xFFFF),
                    *(bind_extra or []))
    functions = [bind]
    for rpc in wrapper_rpc:
        functions.append(function(lui(4, CLIENT >> 16), addiu(4, 4, 0), addiu(5, 0, rpc),
                                  jal(BASE + 0x200), 0))
    starts = []
    words = []
    for body in functions:
        starts.append(BASE + 4 * len(words))
        words += body
    segment_code = code(*words)
    image = ElfImage(build_elf([(BASE, segment_code, len(segment_code), PF_RX)]))
    ends = starts[1:] + [BASE + 4 * len(words)]
    rows = [(start, end, f"FUN_{start:08x}") for start, end in zip(starts, ends)]
    return image, FunctionMap.from_rows(rows)


class TestResolveMouseHooks(unittest.TestCase):
    def test_resolves_bind_and_the_three_wrappers_by_structure(self):
        image, functions = build_mouse_image()
        hooks = resolve_mouse_hooks(image, functions)
        self.assertEqual(hooks, {
            "kcet.mouse.bind": BASE,
            "kcet.mouse.get_info": BASE + BIND_BYTES,
            "kcet.mouse.read": BASE + BIND_BYTES + WRAPPER_BYTES,
            "kcet.mouse.device_info": BASE + BIND_BYTES + 2 * WRAPPER_BYTES,
        })

    def test_missing_service_id_fails(self):
        image = ElfImage(build_elf([(BASE, code(JR_RA, 0), 8, PF_RX)]))
        with self.assertRaises(HookResolutionError):
            resolve_mouse_hooks(image, FunctionMap.from_rows([(BASE, BASE + 8, "FUN_a")]))

    def test_two_functions_with_the_service_id_are_ambiguous(self):
        second_bind = [lui(5, SID >> 16), ori(5, 5, SID & 0xFFFF), JR_RA, 0]
        code_bytes = code(*second_bind, *second_bind)
        image = ElfImage(build_elf([(BASE, code_bytes, len(code_bytes), PF_RX)]))
        functions = FunctionMap.from_rows([(BASE, BASE + 0x10, "FUN_a"), (BASE + 0x10, BASE + 0x20, "FUN_b")])
        with self.assertRaises(HookResolutionError):
            resolve_mouse_hooks(image, functions)

    def test_missing_wrapper_rpc_number_fails(self):
        image, functions = build_mouse_image(wrapper_rpc=(1, 2))
        with self.assertRaises(HookResolutionError):
            resolve_mouse_hooks(image, functions)

    def test_duplicate_wrapper_rpc_number_fails(self):
        image, functions = build_mouse_image(wrapper_rpc=(1, 2, 2))
        with self.assertRaises(HookResolutionError):
            resolve_mouse_hooks(image, functions)

    def test_extra_client_constant_in_bind_is_ambiguous(self):
        image, functions = build_mouse_image(bind_extra=[lui(6, 0x0055), addiu(6, 6, 0)])
        with self.assertRaises(HookResolutionError):
            resolve_mouse_hooks(image, functions)


class TestFunctionMapVerification(unittest.TestCase):
    def setUp(self):
        self.functions = FunctionMap.from_rows([(0x100000, 0x100020, "FUN_100000"), (0x100020, 0x100040, "FUN_100020")])

    def test_accepts_a_function_start(self):
        self.functions.require_start(0x100020)

    def test_rejects_an_address_inside_a_function(self):
        with self.assertRaises(HookResolutionError):
            self.functions.require_start(0x100024)

    def test_rejects_an_unaligned_address(self):
        with self.assertRaises(HookResolutionError):
            self.functions.require_start(0x100022)

    def test_containing_finds_the_function_for_an_instruction(self):
        self.assertEqual(self.functions.containing(0x100024), (0x100020, 0x100040, "FUN_100020"))
        self.assertIsNone(self.functions.containing(0x200000))


if __name__ == "__main__":
    unittest.main()
