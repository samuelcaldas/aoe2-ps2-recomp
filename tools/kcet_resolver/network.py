"""Resolve the game's i.LINK socket wrappers from the owned ELF by what the code does.

Each wrapper performs one SIF RPC call whose function number is the service id of its
operation (docs/sot/ilink-socket-wrapper-spec.md). The init function loads every
service id too, so it is identified by the id only it uses and excluded; every role
must then map to exactly one other function that loads its id and makes a call.
"""

from __future__ import annotations

from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.functions import FunctionMap, single_function
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.mips import OP_JAL, find_address_loads_any_register as find_address_loads, opcode_of, words_between

INIT_SID = 0x80000B84
ROLE_SIDS = {
    "kcet.net.open": 0x80000B88,
    "kcet.net.bind": 0x80000B8A,
    "kcet.net.connect": 0x80000B8B,
    "kcet.net.send": 0x80000B8C,
    "kcet.net.recv": 0x80000B8E,
    "kcet.net.recvfrom": 0x80000B8F,
    "kcet.net.node_id": 0x80000BC0,
}


def _makes_a_call(image: ElfImage, function: tuple) -> bool:
    return any(opcode_of(word) == OP_JAL for word in words_between(image, function[0], function[1]))


def _wrapper_for(image: ElfImage, functions: FunctionMap, init: tuple, role: str, sid: int) -> int:
    candidates = {
        function
        for function in (functions.containing(address) for address in find_address_loads(image, sid))
        if function and function != init
    }
    if len(candidates) != 1:
        raise HookResolutionError(f"expected exactly one {role} wrapper, found {len(candidates)}")
    wrapper = candidates.pop()
    if not _makes_a_call(image, wrapper):
        raise HookResolutionError(f"{role} wrapper makes no call")
    return wrapper[0]


def resolve_network_hooks(image: ElfImage, functions: FunctionMap) -> dict:
    """Return hook name -> guest function start for the i.LINK socket wrappers."""
    init = single_function(functions, find_address_loads(image, INIT_SID), "socket init")
    hooks = {
        role: _wrapper_for(image, functions, init, role, sid) for role, sid in ROLE_SIDS.items()
    }
    for address in hooks.values():
        functions.require_start(address)
    return hooks
