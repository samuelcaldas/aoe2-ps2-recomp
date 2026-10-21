import unittest

from tests.test_kcet_resolver_elf import PF_RX, build_elf, code
from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import JR_RA, addiu, jal, lui, ori
from tools.kcet_resolver.keyboard import resolve_keyboard_hooks

BASE = 0x100000
CLIENT = 0x00510000
SID = 0x80000211
PROLOGUE = addiu(29, 29, 0xFFE0)
EPILOGUE = [JR_RA, addiu(29, 29, 0x20)]
BIND_WORDS = 7
WRAPPER_WORDS = 8


def function(*words):
    return [PROLOGUE, *words, *EPILOGUE]


def synthetic_keyboard(rpcs=(1, 2, 3, 4, 5), extra_bind=()):
    bodies = [function(lui(16, CLIENT >> 16), addiu(16, 16, 0),
                       lui(5, SID >> 16), ori(5, 5, SID & 0xFFFF), *extra_bind)]
    bodies.extend(function(lui(4, CLIENT >> 16), addiu(4, 4, 0), addiu(5, 0, rpc),
                           jal(BASE + 0x200), 0) for rpc in rpcs)
    words = [word for body in bodies for word in body]
    image_bytes = code(*words)
    image = ElfImage(build_elf([(BASE, image_bytes, len(image_bytes), PF_RX)]))
    starts = [BASE + index * (BIND_WORDS if index == 0 else WRAPPER_WORDS) * 4
              for index in range(len(bodies))]
    ends = starts[1:] + [BASE + len(words) * 4]
    functions = FunctionMap.from_rows([(start, end, f"FUN_{start:08x}") for start, end in zip(starts, ends)])
    return image, functions


class TestResolveKeyboardHooks(unittest.TestCase):
    def test_resolves_bind_and_five_rpc_wrappers(self):
        image, functions = synthetic_keyboard()
        hooks = resolve_keyboard_hooks(image, functions)
        self.assertEqual(set(hooks), {
            "kcet.keyboard.bind", "kcet.keyboard.get_info", "kcet.keyboard.read",
            "kcet.keyboard.device_info", "kcet.keyboard.set_led", "kcet.keyboard.set_caps_type",
        })
        self.assertEqual(len(set(hooks.values())), 6)

    def test_missing_wrapper_fails(self):
        image, functions = synthetic_keyboard(rpcs=(1, 2, 3, 4))
        with self.assertRaises(HookResolutionError):
            resolve_keyboard_hooks(image, functions)

    def test_ambiguous_bind_constant_fails(self):
        image, functions = synthetic_keyboard(extra_bind=(lui(6, 0x0055), addiu(6, 6, 0)))
        with self.assertRaises(HookResolutionError):
            resolve_keyboard_hooks(image, functions)


if __name__ == "__main__":
    unittest.main()
