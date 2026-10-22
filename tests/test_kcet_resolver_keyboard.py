import unittest

from tests.test_kcet_resolver_elf import PF_RX, build_elf, code
from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import JR_RA, addiu, jal, lui, ori
from tools.kcet_resolver.keyboard import resolve_keyboard_hooks

BASE = 0x100000
CLIENT = 0x00720000
SPLIT_CLIENT = 0x0071B6C0
SID = 0x80000211
PROLOGUE = addiu(29, 29, 0xFFE0)
EPILOGUE = [JR_RA, addiu(29, 29, 0x20)]


def function(*words):
    return [PROLOGUE, *words, *EPILOGUE]


def synthetic_program(bodies):
    rows = []
    address = BASE
    for body in bodies:
        end = address + len(body) * 4
        rows.append((address, end, f"FUN_{address:08x}"))
        address = end
    image_bytes = code(*(word for body in bodies for word in body))
    image = ElfImage(build_elf([(BASE, image_bytes, len(image_bytes), PF_RX)]))
    return image, FunctionMap.from_rows(rows)


def bind_body(client, extra_bind=()):
    return function(lui(16, client >> 16), ori(16, 16, client & 0xFFFF),
                    lui(5, SID >> 16), ori(5, 5, SID & 0xFFFF), *extra_bind)


def synthetic_keyboard(rpcs=(1, 2, 3, 4, 5), extra_bind=()):
    bodies = [bind_body(CLIENT, extra_bind)]
    bodies.extend(function(lui(4, CLIENT >> 16), addiu(4, 4, 0), addiu(5, 0, rpc),
                           jal(BASE + 0x200), 0) for rpc in rpcs)
    return synthetic_program(bodies)


def split_client_bodies(intervening):
    bodies = [bind_body(SPLIT_CLIENT)]
    bodies.extend(function(lui(2, 0x0072), *intervening, addiu(4, 2, 0xB6C0),
                           addiu(5, 0, rpc), jal(BASE + 0x800), 0) for rpc in (1, 2, 3, 4, 5))
    return bodies


class TestResolveKeyboardHooks(unittest.TestCase):
    def test_resolves_bind_and_five_rpc_wrappers(self):
        image, functions = synthetic_keyboard()
        hooks = resolve_keyboard_hooks(image, functions)
        self.assertEqual(set(hooks), {
            "kcet.keyboard.bind", "kcet.keyboard.get_info", "kcet.keyboard.read",
            "kcet.keyboard.device_info", "kcet.keyboard.set_led", "kcet.keyboard.set_caps_type",
        })
        self.assertEqual(len(set(hooks.values())), 6)
        for address in hooks.values():
            self.assertEqual(int.from_bytes(image.read(address, 4), "little"), PROLOGUE)

    def test_delayed_client_load_may_use_another_destination_register(self):
        bodies = split_client_bodies([0] * 6)
        image, functions = synthetic_program(bodies)
        hooks = resolve_keyboard_hooks(image, functions)
        self.assertEqual(hooks["kcet.keyboard.set_led"], BASE + sum(len(body) * 4 for body in bodies[:4]))
        self.assertEqual(hooks["kcet.keyboard.set_caps_type"], BASE + sum(len(body) * 4 for body in bodies[:5]))

    def test_client_load_rejects_a_clobbered_or_unproven_base(self):
        interruptions = (addiu(2, 0, 0x1234), 0x00041021, 0x8C820000,
                         0x70001628, jal(BASE + 0x800), 0x45000000)
        for interruption in interruptions:
            with self.subTest(interruption=interruption):
                image, functions = synthetic_program(split_client_bodies([interruption]))
                with self.assertRaises(HookResolutionError):
                    resolve_keyboard_hooks(image, functions)

    def test_missing_wrapper_fails(self):
        image, functions = synthetic_keyboard(rpcs=(1, 2, 3, 4))
        with self.assertRaises(HookResolutionError):
            resolve_keyboard_hooks(image, functions)

    def test_low_half_fallback_rejects_unrelated_immediate(self):
        bodies = [bind_body(SPLIT_CLIENT)]
        bodies.extend(function(addiu(5, 0, rpc), ori(7, 7, 0xB6C0),
                               jal(BASE + 0x200), 0, 0) for rpc in (1, 2, 3, 4, 5))
        image, functions = synthetic_program(bodies)
        with self.assertRaises(HookResolutionError):
            resolve_keyboard_hooks(image, functions)

    def test_ambiguous_bind_constant_fails(self):
        image, functions = synthetic_keyboard(extra_bind=(lui(6, 0x0077), addiu(6, 6, 0)))
        with self.assertRaises(HookResolutionError):
            resolve_keyboard_hooks(image, functions)


if __name__ == "__main__":
    unittest.main()
