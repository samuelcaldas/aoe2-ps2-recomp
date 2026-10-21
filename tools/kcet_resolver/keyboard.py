"""Resolve KCET USB keyboard RPC functions by their ELF instruction structure."""
from __future__ import annotations

from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.functions import FunctionMap, single, single_function
from tools.kcet_resolver.mips import (
    OP_ADDIU,
    OP_LUI,
    OP_ORI,
    constants_built,
    find_address_loads,
    immediate_of,
    opcode_of,
    rs_of,
    rt_of,
    signed16,
    small_arguments_before_calls,
)

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


def _builds_client_address(words: list[int], client: int) -> bool:
    for index, high_word in enumerate(words):
        if opcode_of(high_word) != OP_LUI:
            continue
        register = rt_of(high_word)
        for low_word in words[index + 1:]:
            if opcode_of(low_word) not in (OP_ADDIU, OP_ORI):
                continue
            if rs_of(low_word) != register or rt_of(low_word) != register:
                continue
            high = immediate_of(high_word) << 16
            low = immediate_of(low_word)
            value = high + signed16(low) if opcode_of(low_word) == OP_ADDIU else high | low
            if value & 0xFFFFFFFF == client:
                return True
    return False


def _client_low_half_wrappers(image: ElfImage, functions: FunctionMap, client: int,
                              rpc_function: int) -> list:
    """Find wrappers using the client global through a base-plus-low-half address form."""
    matches = []
    for start, end, _ in functions._rows:
        if rpc_function not in small_arguments_before_calls(image, start, end, ARGUMENT_REGISTER_A1):
            continue
        words = [int.from_bytes(image.read(start + offset, 4), "little")
                 for offset in range(0, end - start, 4)]
        if _builds_client_address(words, client):
            matches.append((start, end, ""))
    return matches


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
        if not matches:
            matches = _client_low_half_wrappers(image, functions, client, rpc_function)
        hooks[hook] = single([function[0] for function in matches], f"keyboard RPC function {rpc_function}")
    for address in hooks.values():
        functions.require_start(address)
    return hooks
