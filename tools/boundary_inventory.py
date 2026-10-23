#!/usr/bin/env python3
"""Measure the boundary between engine code and the PS2 compatibility layer in the owned ELF.

The engine is compiled with MWCC (functions save $ra with 128-bit `sq`); KCET, the Sony
libraries and the C runtime stubs come from EE-GCC (64-bit `sd`). A function is classified by
that frame save; kernel stubs by containing a `syscall`. A boundary edge is a direct call or tail
jump from an MWCC function to a GCC or syscall function. Addresses go only to a git-ignored
report; stdout carries counts. This is a measurement, not a proof of module identity.
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
from tools.inventory_overlays import InventoryError, require_ignored  # noqa: E402
from tools.kcet_resolver.elf_image import ElfImage, load_elf  # noqa: E402
from tools.kcet_resolver.functions import FunctionMap  # noqa: E402

MWCC, GCC, SYSCALL, OTHER = "mwcc", "gcc", "syscall", "other"
OPCODE_SQ, OPCODE_SD, OPCODE_J, OPCODE_JAL = 0x1F, 0x3F, 2, 3
REG_SP, REG_RA = 29, 31
SYSCALL_WORD = 0x0000000C
REGION_MASK = 0xF0000000
DEFAULT_GAP = 0x400


@dataclass(frozen=True)
class Edge:
    caller: int
    callee: int
    callee_class: str


def _saves_ra(word: int, opcode: int) -> bool:
    return word >> 26 == opcode and (word >> 21) & 31 == REG_SP and (word >> 16) & 31 == REG_RA


def classify_function(words: list) -> str:
    if any(word == SYSCALL_WORD for word in words):
        return SYSCALL
    if any(_saves_ra(word, OPCODE_SQ) for word in words):
        return MWCC
    if any(_saves_ra(word, OPCODE_SD) for word in words):
        return GCC
    return OTHER


def _function_words(image: ElfImage, start: int, end: int) -> list:
    data = image.read(start, end - start)
    return list(struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4]))


def _jump_targets(words: list, start: int) -> list:
    targets = []
    for index, word in enumerate(words):
        if word >> 26 in (OPCODE_J, OPCODE_JAL):
            targets.append(((start + 4 * index + 4) & REGION_MASK) | ((word & 0x3FFFFFF) << 2))
    return targets


def find_boundary(image: ElfImage, functions: FunctionMap, rows: list) -> list:
    """Edges from MWCC functions to the start of a GCC or syscall function."""
    classes = {start: classify_function(_function_words(image, start, end)) for start, end, _ in rows}
    edges = set()
    for start, end, _ in rows:
        if classes[start] != MWCC:
            continue
        for target in _jump_targets(_function_words(image, start, end), start):
            callee = functions.containing(target)
            if callee and callee[0] == target and classes.get(target) in (GCC, SYSCALL) and target != start:
                edges.add(Edge(start, target, classes[target]))
    return sorted(edges, key=lambda edge: (edge.callee, edge.caller))


def clusters(addresses: list, gap: int = DEFAULT_GAP) -> list:
    """(first, last) per run of addresses where neighbours are closer than gap."""
    result = []
    for address in sorted(set(addresses)):
        if result and address - result[-1][1] <= gap:
            result[-1] = (result[-1][0], address)
        else:
            result.append((address, address))
    return result


def build_report(edges: list) -> dict:
    by_callee = defaultdict(set)
    for edge in edges:
        by_callee[edge.callee].add(edge.caller)
    by_class = defaultdict(list)
    for edge in edges:
        by_class[edge.callee_class].append(edge.callee)
    return {
        "callees": {hex(callee): sorted(hex(caller) for caller in callers) for callee, callers in sorted(by_callee.items())},
        "clusters": {kind: [[hex(a), hex(b)] for a, b in clusters(targets)] for kind, targets in by_class.items()},
    }


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=REPO_ROOT / "game/SLES-50282/AOK.ELF")
    parser.add_argument("--function-map", type=Path, default=REPO_ROOT / "analysis/generated/function_map.csv")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "analysis/generated/boundary_inventory.json")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        require_ignored(args.out)
        image = load_elf(args.elf)
        functions = FunctionMap.from_csv(args.function_map)
        rows = functions._rows
        edges = find_boundary(image, functions, rows)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(build_report(edges), indent=1) + "\n", encoding="utf-8")
    except (OSError, InventoryError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    counts = defaultdict(lambda: [set(), set()])
    for edge in edges:
        counts[edge.callee_class][0].add(edge.callee)
        counts[edge.callee_class][1].add(edge.caller)
    classified = defaultdict(int)
    for start, end, _ in rows:
        classified[classify_function(_function_words(image, start, end))] += 1
    print("functions by class:", dict(classified))
    for kind, (callees, callers) in sorted(counts.items()):
        print(f"{kind}: {len(callees)} boundary entry points called from {len(callers)} engine functions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
