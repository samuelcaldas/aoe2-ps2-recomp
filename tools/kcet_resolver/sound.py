"""Resolve the KCET SDD/SDCD sound client functions from the owned ELF by what the code does.

Origin of anchor knowledge: samuelcaldas/age-of-empires-2-decomp src/kcet/kt_sound.c.

Four functions are hooked (ADR 0001):
  kcet.sound.init         - binds both SIF RPC clients: SDCD (dev 0x20) and SDD (dev 0x30).
  kcet.sound.open_stream  - opens a CD audio stream via SDCD, cmd 0x10 in a1.
  kcet.sound.query_status - SDD readiness poll; sets a1=3 before the JAL; returns D_0038C880[0].
  kcet.sound.query_volume - SDD extended status/volume; sets a1=cmd before the JAL (not 3 or 0x10).

Anchors (all small immediates, no lui/addiu needed):
  1. init: the only function that carries BOTH dev 0x20 AND dev 0x30 in a1 before calls.
  2. open_stream: loads dev 0x20 in a1 AND cmd 0x10 in a1 before calls, excluding init.
  3. query_status: loads 0x30 in a1 AND 3 in a1 before calls, excluding init.
  4. query_volume: loads 0x30 in a1 AND some cmd (not 3 or 0x10) before calls; what remains.

We rely on small_arguments_before_calls (addiu a1, zero, N followed within CALL_WINDOW by jal).
"""
from __future__ import annotations

from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import (
    OP_JAL,
    opcode_of,
    small_arguments_before_calls,
    words_between,
)

# SIF RPC device identifiers (passed as a1 to sceSifBindRpc / sceSifCallRpc).
_SDCD_DEV = 0x20   # CD audio daemon
_SDD_DEV  = 0x30   # Sound Driver Daemon

# SIF RPC function numbers that appear in a1 before the call.
_CMD_OPEN_STREAM  = 0x10   # func_0012D7E0 (SDCD)
_CMD_QUERY_STATUS = 3      # func_0012D888 (SDD)

_REG_A1 = 5


def _args_in_a1(image: ElfImage, function: tuple) -> set:
    """Set of small immediates loaded into a1 before any JAL inside this function."""
    return set(small_arguments_before_calls(image, function[0], function[1], _REG_A1))


def _makes_any_call(image: ElfImage, function: tuple) -> bool:
    return any(opcode_of(w) == OP_JAL for w in words_between(image, function[0], function[1]))


def resolve_sound_hooks(image: ElfImage, functions: FunctionMap) -> dict:
    """Return hook name -> guest function start for the KCET sound client layer."""

    # Collect every known function that makes a JAL and carries either SID in a1.
    candidates = [
        f for f in functions.all()
        if _makes_any_call(image, f) and (_SDCD_DEV in _args_in_a1(image, f) or
                                           _SDD_DEV in _args_in_a1(image, f))
    ]

    # init: carries BOTH SIDs.
    init_matches = [f for f in candidates
                    if _SDCD_DEV in _args_in_a1(image, f) and _SDD_DEV in _args_in_a1(image, f)]
    if len(init_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound init function (with both SDCD and SDD devices), "
            f"found {len(init_matches)}"
        )
    init_fn = init_matches[0]

    # Remaining candidates (not init).
    rest = [f for f in candidates if f != init_fn]

    # open_stream: has SDCD dev (0x20) and cmd 0x10 in a1.
    open_stream_matches = [f for f in rest
                           if _SDCD_DEV in _args_in_a1(image, f) and
                              _CMD_OPEN_STREAM in _args_in_a1(image, f)]
    if len(open_stream_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound open_stream function, found {len(open_stream_matches)}"
        )
    open_stream_fn = open_stream_matches[0]

    # SDD wrappers: carry SDD dev (0x30) but not SDCD dev.
    sdd_wrappers = [f for f in rest
                    if _SDD_DEV in _args_in_a1(image, f) and
                       _SDCD_DEV not in _args_in_a1(image, f)]

    # query_status: carries cmd 3.
    query_status_matches = [f for f in sdd_wrappers if _CMD_QUERY_STATUS in _args_in_a1(image, f)]
    if len(query_status_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound query_status function (cmd 3), "
            f"found {len(query_status_matches)}"
        )
    query_status_fn = query_status_matches[0]

    # query_volume: the remaining SDD wrapper.
    query_volume_matches = [f for f in sdd_wrappers if f != query_status_fn]
    if len(query_volume_matches) != 1:
        raise HookResolutionError(
            f"expected exactly one sound query_volume function, found {len(query_volume_matches)}"
        )
    query_volume_fn = query_volume_matches[0]

    hooks = {
        "kcet.sound.init":         init_fn[0],
        "kcet.sound.open_stream":  open_stream_fn[0],
        "kcet.sound.query_status": query_status_fn[0],
        "kcet.sound.query_volume": query_volume_fn[0],
    }
    for address in hooks.values():
        functions.require_start(address)
    return hooks
