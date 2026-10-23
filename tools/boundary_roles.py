#!/usr/bin/env python3
"""Give each boundary entry point a role from evidence inside the code it reaches.

For every compat-layer function the engine calls, follow direct calls that stay out of engine code and
collect: SIF service ids (mapped to roles, unmapped ones kept as numbers), hardware register bases, and
syscalls. A role needs evidence; nothing is guessed. Addresses go only to a git-ignored report.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
from tools.boundary_inventory import MWCC, SYSCALL, _function_words, _jump_targets, build_report, classify_function, find_boundary  # noqa: E402
from tools.inventory_overlays import InventoryError, require_ignored  # noqa: E402
from tools.kcet_resolver import mips  # noqa: E402
from tools.kcet_resolver.elf_image import load_elf  # noqa: E402
from tools.kcet_resolver.functions import FunctionMap  # noqa: E402

ROLE_HARDWARE, ROLE_KERNEL, ROLE_UNKNOWN = "hardware", "kernel", "unknown"
ROLE_TRANSPORT = "sif_transport"
# Ids below this are SIF command ids used by the transport itself, shared by every RPC wrapper.
SIF_COMMAND_LIMIT = 0x80000100
SID_PREFIX = 0x80000000
SID_RANGES = (
    (0x80000592, 0x8000059F, "cdvd"),
    (0x80000100, 0x80000101, "pad"),
    (0x80000400, 0x80000400, "memory_card"),
    (0x80000210, 0x80000210, "usb_mouse"),
    (0x80000211, 0x80000211, "usb_keyboard"),
    (0x80000B84, 0x80000BC1, "socket"),
    (0x80000006, 0x80000006, "loadfile"),
)
HARDWARE_BYTES = (0x10, 0x11, 0x12, 0x13)
MAX_CLOSURE = 300


def sid_role(value: int) -> str:
    if value < SIF_COMMAND_LIMIT:
        return ROLE_TRANSPORT
    for low, high, role in SID_RANGES:
        if low <= value <= high:
            return role
    return f"sid_0x{value:08x}"


def _touches_hardware(words: list) -> bool:
    return any(mips.opcode_of(word) == mips.OP_LUI and (mips.immediate_of(word) >> 8) in HARDWARE_BYTES for word in words)


def _built_constants(words: list) -> set:
    values = set()
    for index, word in enumerate(words):
        if mips.opcode_of(word) != mips.OP_LUI:
            continue
        for follower in words[index + 1:index + 5]:
            value = mips._constant_built_into_any_register(word, follower)
            if value is not None:
                values.add(value)
    return values


def _closure(image, functions, classes: dict, entry: int) -> list:
    seen, queue = {entry}, [entry]
    while queue and len(seen) < MAX_CLOSURE:
        start = queue.pop()
        function = functions.containing(start)
        for target in _jump_targets(_function_words(image, function[0], function[1]), function[0]):
            callee = functions.containing(target)
            if callee and callee[0] == target and callee[0] not in seen and classes.get(target) != MWCC:
                seen.add(target)
                queue.append(target)
    return sorted(seen)


def _evidence(image, functions, classes: dict, entry: int) -> tuple:
    sids, hardware, syscalls = set(), False, False
    for start in _closure(image, functions, classes, entry):
        end = functions.containing(start)[1]
        words = _function_words(image, start, end)
        syscalls = syscalls or classes.get(start) == SYSCALL
        for value in _built_constants(words):
            if value & 0xFFFF0000 == SID_PREFIX:
                sids.add(sid_role(value))
        hardware = hardware or _touches_hardware(words)
    return sids, hardware, syscalls


def assign_roles(image, functions, rows: list, edges: list) -> dict:
    classes = {start: classify_function(_function_words(image, start, end)) for start, end, _ in rows}
    roles = {}
    for callee in sorted({edge.callee for edge in edges}):
        sids, hardware, syscalls = _evidence(image, functions, classes, callee)
        services = sids - {ROLE_TRANSPORT}
        if services:
            roles[callee] = "+".join(sorted(services))
        elif sids:
            roles[callee] = ROLE_TRANSPORT
        elif hardware:
            roles[callee] = ROLE_HARDWARE
        elif syscalls:
            roles[callee] = ROLE_KERNEL
        else:
            roles[callee] = ROLE_UNKNOWN
    return roles


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=REPO_ROOT / "game/SLES-50282/AOK.ELF")
    parser.add_argument("--function-map", type=Path, default=REPO_ROOT / "analysis/generated/function_map.csv")
    parser.add_argument("--log", type=Path, help="runtime log; SIF service ids seen there are cross-checked")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "analysis/generated/boundary_roles.json")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        require_ignored(args.out)
        image = load_elf(args.elf)
        functions = FunctionMap.from_csv(args.function_map)
        rows = functions._rows
        edges = find_boundary(image, functions, rows)
        roles = assign_roles(image, functions, rows, edges)
        callers = defaultdict(set)
        for edge in edges:
            callers[edge.callee].add(edge.caller)
        report = {hex(callee): {"role": role, "engine_callers": len(callers[callee])} for callee, role in roles.items()}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
        seen_sids = _sids_in_log(args.log) if args.log else None
    except (OSError, InventoryError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    totals = Counter(roles.values())
    for role, count in totals.most_common():
        print(f"{role}: {count} entry points")
    if seen_sids is not None:
        static_ids = {r for role in roles.values() for r in role.split("+")}
        print(f"service ids seen at run time: {len(seen_sids)}; static roles: {len(static_ids)}")
    return 0


def _sids_in_log(path: Path) -> set:
    import re
    return set(re.findall(r"sif:call\] sid=(0x[0-9a-f]+)", path.read_text(errors="replace")))


if __name__ == "__main__":
    sys.exit(main())
