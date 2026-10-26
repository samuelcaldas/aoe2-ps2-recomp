"""Resolve the KCET SDD/SDCD sound client functions from the owned ELF by what the code does.

Origin of anchor knowledge: samuelcaldas/age-of-empires-2-decomp src/kcet/kt_sound.c.

Three functions are hooked (ADR 0001). func_0012D840 (query_volume) is not separately
exported in the Ghidra function map and is therefore not resolvable; it is not needed
to unblock boot because the critical stall is in func_0012D888 (query_status).

  kcet.sound.init         - binds both SIF RPC clients: SDCD (dev 0x20) and SDD (dev 0x30).
                            Anchor: a1=0x20 and a1=0x30 before JAL calls.
  kcet.sound.open_stream  - opens a CD audio stream via SDCD, cmd 0x10 in a1.
                            Anchor: builds SDCD client address (0x4f1640) and a1=0x10 before JAL.
  kcet.sound.query_status - SDD readiness poll; a1=3 before JAL; returns D_0038C880[0].
                            Anchor: builds SDD client address (0x4f1680) and a1=3 before JAL.
                            This is the only hook required to break the boot stall.
"""
from __future__ import annotations

from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import (
    OP_JAL,
    constants_built,
    opcode_of,
    small_arguments_before_calls,
    words_between,
)

# SIF RPC device identifiers loaded into $a1 (r5) before sceSifBindRpc in init.
_SDCD_DEV = 0x20   # CD audio daemon
_SDD_DEV  = 0x30   # Sound Driver Daemon

# SIF RPC function numbers loaded into $a1 before sceSifCallRpc in the wrappers.
_CMD_OPEN_STREAM  = 0x10   # func_0012D7E0 (SDCD)
_CMD_QUERY_STATUS = 3      # func_0012D888 (SDD)

# SIF RPC client globals built by lui/addiu inside each wrapper (not device IDs).
_SDCD_CLIENT = 0x004F1640  # D_004F1640 in decomp
_SDD_CLIENT  = 0x004F1680  # D_004F1680 in decomp

_REG_A1 = 5
_CONST_WINDOW = 6


def _a1_args(image: ElfImage, function: tuple) -> set:
    """Small immediates loaded into $a1 before any JAL inside this function."""
    return set(small_arguments_before_calls(image, function[0], function[1], _REG_A1))


def _consts(image: ElfImage, function: tuple) -> set:
    """32-bit constants built by lui/addiu or lui/ori pairs inside this function."""
    return set(constants_built(image, function[0], function[1], window=_CONST_WINDOW))


def _makes_any_call(image: ElfImage, function: tuple) -> bool:
    return any(opcode_of(w) == OP_JAL for w in words_between(image, function[0], function[1]))


def resolve_sound_hooks(image: ElfImage, functions: FunctionMap) -> dict:
    """Return hook name -> guest function start for the KCET sound client layer."""

    all_fns = [f for f in functions.all() if _makes_any_call(image, f)]

    # init: a1 carries BOTH SDCD dev (0x20) AND SDD dev (0x30) before JAL calls.
    init_matches = [f for f in all_fns
                    if _SDCD_DEV in _a1_args(image, f) and _SDD_DEV in _a1_args(image, f)]
    if len(init_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound init (a1=0x20 and a1=0x30), found {len(init_matches)}"
        )
    init_fn = init_matches[0]

    # open_stream: a1=0x10 before JAL, builds the SDCD client address constant.
    open_stream_matches = [
        f for f in all_fns
        if f != init_fn
        and _CMD_OPEN_STREAM in _a1_args(image, f)
        and _SDCD_CLIENT in _consts(image, f)
    ]
    if len(open_stream_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound open_stream (a1=0x10 + SDCD client), "
            f"found {len(open_stream_matches)}"
        )
    open_stream_fn = open_stream_matches[0]

    # query_status: a1=3 before JAL, builds the SDD client address constant.
    query_status_matches = [
        f for f in all_fns
        if f != init_fn
        and _CMD_QUERY_STATUS in _a1_args(image, f)
        and _SDD_CLIENT in _consts(image, f)
    ]
    if len(query_status_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound query_status (a1=3 + SDD client), "
            f"found {len(query_status_matches)}"
        )
    query_status_fn = query_status_matches[0]

    hooks = {
        "kcet.sound.init":         init_fn[0],
        "kcet.sound.open_stream":  open_stream_fn[0],
        "kcet.sound.query_status": query_status_fn[0],
    }
    for address in hooks.values():
        functions.require_start(address)
    return hooks
