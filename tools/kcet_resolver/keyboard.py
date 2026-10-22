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


GPR_IMMEDIATE_WRITES = frozenset((*range(8, 16), *range(24, 28), 30,
                                  *range(32, 40), 48, 52, 55, 56, 60))
CONTROL_FLOW_OPCODES = frozenset((1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23))


def _writes_gpr(word: int, register: int) -> bool:
    opcode = opcode_of(word)
    if opcode in (0, 28):
        return (word >> 11) & 31 == register
    if opcode in (16, 17, 18) and rs_of(word) in (0, 1, 2):
        return rt_of(word) == register
    return opcode in GPR_IMMEDIATE_WRITES and rt_of(word) == register


def _ends_linear_scan(word: int) -> bool:
    opcode = opcode_of(word)
    return (opcode in CONTROL_FLOW_OPCODES
            or (opcode in (16, 17, 18) and rs_of(word) == 8)
            or (opcode == 0 and word & 63 in (8, 9, 12, 13)))


def _matches_client_low(word: int, register: int, high: int, client: int) -> bool:
    if opcode_of(word) not in (OP_ADDIU, OP_ORI) or rs_of(word) != register or rt_of(word) == 0:
        return False
    low = immediate_of(word)
    value = high + signed16(low) if opcode_of(word) == OP_ADDIU else high | low
    return value & 0xFFFFFFFF == client


def _client_built_after(words: list[int], index: int, client: int) -> bool:
    register = rt_of(words[index])
    high = immediate_of(words[index]) << 16
    for word in words[index + 1:]:
        if _ends_linear_scan(word):
            return False
        if _matches_client_low(word, register, high, client):
            return True
        if _writes_gpr(word, register):
            return False
    return False


def _builds_client_address(words: list[int], client: int) -> bool:
    return any(opcode_of(word) == OP_LUI and rt_of(word) != 0
               and _client_built_after(words, index, client) for index, word in enumerate(words))


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
