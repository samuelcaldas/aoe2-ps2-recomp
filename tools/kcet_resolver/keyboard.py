"""Resolve KCET USB keyboard RPC functions by their ELF instruction structure."""
from __future__ import annotations

from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.functions import FunctionMap, single, single_function
from tools.kcet_resolver.mips import constants_built, find_address_loads, small_arguments_before_calls

USB_KEYBOARD_RPC_SID = 0x80000211
WRAPPER_HOOKS = {
    1: "kcet.keyboard.get_info",
    2: "kcet.keyboard.read",
    3: "kcet.keyboard.device_info",
    4: "kcet.keyboard.set_led",
    5: "kcet.keyboard.set_caps_type",
}
BIND_HOOK = "kcet.keyboard.bind"
ARGUMENT_REGISTER_A1 = 5


def resolve_keyboard_hooks(image: ElfImage, functions: FunctionMap) -> dict:
    """Return hook names and exported function starts for the keyboard RPC service."""
    bind = single_function(functions, find_address_loads(image, USB_KEYBOARD_RPC_SID), "keyboard service id")
    client = single([value for value in constants_built(image, bind[0], bind[1])
                     if value != USB_KEYBOARD_RPC_SID], "keyboard RPC client constant in bind")
    hooks = {BIND_HOOK: bind[0]}
    wrappers = {function for function in
                (functions.containing(address) for address in find_address_loads(image, client)) if function}
    wrappers.discard(bind)
    for rpc_function, hook in WRAPPER_HOOKS.items():
        matches = [function for function in wrappers
                   if rpc_function in small_arguments_before_calls(
                       image, function[0], function[1], ARGUMENT_REGISTER_A1)]
        hooks[hook] = single([function[0] for function in matches], f"keyboard RPC function {rpc_function}")
    for address in hooks.values():
        functions.require_start(address)
    return hooks
