"""Resolve the KCET USB mouse RPC layer from the owned ELF by what the code does.

Three structural anchors, each required to match exactly one function:
1. the SIF RPC service id of the USB mouse (0x80000210) is loaded by the bind function;
2. the bind function builds one other constant: the RPC client global;
3. the wrappers build that same client global and call with RPC functions 1, 2, and 3.
"""

from __future__ import annotations

from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.functions import FunctionMap, single, single_function
from tools.kcet_resolver.mips import (
    constants_built,
    find_address_loads,
    small_arguments_before_calls,
)

USB_MOUSE_RPC_SID = 0x80000210
WRAPPER_HOOKS = {
    1: "kcet.mouse.get_info",
    2: "kcet.mouse.read",
    3: "kcet.mouse.device_info",
}
BIND_HOOK = "kcet.mouse.bind"
ARGUMENT_REGISTER_A1 = 5


def resolve_mouse_hooks(image: ElfImage, functions: FunctionMap) -> dict:
    """Return hook name -> guest function start for the KCET USB mouse RPC layer."""
    bind = single_function(
        functions, find_address_loads(image, USB_MOUSE_RPC_SID), "service id"
    )
    client = single(
        [
            value
            for value in constants_built(image, bind[0], bind[1])
            if value != USB_MOUSE_RPC_SID
        ],
        "RPC client constant in the bind function",
    )
    hooks = {BIND_HOOK: bind[0]}
    wrappers = {
        function
        for function in (
            functions.containing(address)
            for address in find_address_loads(image, client)
        )
        if function
    }
    wrappers.discard(bind)
    for rpc_function, hook in WRAPPER_HOOKS.items():
        matches = [
            function
            for function in wrappers
            if rpc_function
            in small_arguments_before_calls(
                image, function[0], function[1], ARGUMENT_REGISTER_A1
            )
        ]
        hooks[hook] = single(
            [function[0] for function in matches],
            f"wrapper for RPC function {rpc_function}",
        )
    for address in hooks.values():
        functions.require_start(address)
    return hooks
