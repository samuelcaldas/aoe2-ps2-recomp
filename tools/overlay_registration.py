#!/usr/bin/env python3
"""Turn one overlay's generated register file into overlay-aware registration code.

Six overlays share load addresses, and generated function names embed addresses, so
the same symbol would be defined several times. Each overlay gets a rename header
(force-included when compiling its sources) that prefixes every function name, and a
registration source that calls PS2Runtime::registerOverlayFunction for every
(address, function) pair the generator emitted. Output is generated, git-ignored code.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

TABLE_ENTRY = re.compile(
    r"(?:g_ps2RecompiledFunctionTable\[\d+\]\s*=\s*(\w+);|\{\s*\d+u?,\s*(\w+)\s*\},?)\s*//\s*0x([0-9a-fA-F]+)"
)
DECLARATION = re.compile(r"^void\s+(\w+)\s*\(\s*uint8_t\*\s*rdram", re.MULTILINE)
PREFIX = re.compile(r"^[A-Za-z_]\w*_$")
NAME = re.compile(r"^[A-Za-z_]\w*$")


class OverlayRegistrationError(ValueError):
    pass


def parse_register_file(text: str) -> list:
    """Return (address, function name) for every table entry, in file order."""
    pairs = []
    for match in TABLE_ENTRY.finditer(text):
        name = match.group(1) or match.group(2)
        address = match.group(3)
        pairs.append((int(address, 16), name))
    if not pairs:
        raise OverlayRegistrationError("no function table entries found in the register file")
    return pairs


def build_rename_header(functions_header: str, prefix: str) -> str:
    _require_prefix(prefix)
    names = list(dict.fromkeys(DECLARATION.findall(functions_header)))
    if not names:
        raise OverlayRegistrationError("no function declarations found in the functions header")
    lines = ["#pragma once", "// Generated: gives this overlay's functions unique link names."]
    lines += [f"#define {name} {prefix}{name}" for name in names]
    return "\n".join(lines) + "\n"


def build_registration(pairs: list, overlay_id: int, prefix: str, name: str) -> str:
    _require_prefix(prefix)
    if not NAME.match(name):
        raise OverlayRegistrationError(f"unsafe overlay name: {name!r}")
    declared = list(dict.fromkeys(function for _, function in pairs))
    lines = ['#include "ps2_runtime.h"', "#include <stdexcept>", ""]
    lines += [f"void {prefix}{function}(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime);" for function in declared]
    lines += ["", f"void RegisterOverlay_{name}(PS2Runtime& runtime) {{"]
    for address, function in pairs:
        lines.append(f"    if (!runtime.registerOverlayFunction({overlay_id}u, 0x{address:x}u, {prefix}{function}))")
        lines.append(f'        throw std::runtime_error("overlay {name}: cannot register 0x{address:x}");')
    lines.append("}")
    return "\n".join(lines) + "\n"


def merge_regions(overlays: list) -> list:
    """One (base, size) per distinct load address, sized for the largest code image; reject overlaps."""
    sizes = {}
    for overlay in overlays:
        base = overlay["load_address"]
        sizes[base] = max(sizes.get(base, 0), overlay["code_bytes"])
    regions = sorted(sizes.items())
    for (base, size), (next_base, _) in zip(regions, regions[1:]):
        if base + size > next_base:
            raise OverlayRegistrationError("overlay code regions overlap; the id-selected table cannot tell them apart")
    return regions


def build_aggregate(overlays: list) -> str:
    """Source adding every code region, then registering every overlay; returns the overlay count."""
    names = [overlay["name"] for overlay in overlays]
    if not all(NAME.match(name) for name in names):
        raise OverlayRegistrationError("unsafe overlay name")
    lines = ['#include "ps2_runtime.h"', "#include <stdexcept>", ""]
    lines += [f"void RegisterOverlay_{name}(PS2Runtime& runtime);" for name in names]
    lines += ["", "int RegisterGeneratedOverlays(PS2Runtime& runtime) {"]
    for base, size in merge_regions(overlays):
        lines.append(f"    if (!runtime.addOverlayRegion(0x{base:x}u, 0x{size:x}u, 0x{base + 4:x}u))")
        lines.append(f'        throw std::runtime_error("cannot add overlay region 0x{base:x}");')
    lines += [f"    RegisterOverlay_{name}(runtime);" for name in names]
    lines += [f"    return {len(names)};", "}"]
    return "\n".join(lines) + "\n"


def _require_prefix(prefix: str) -> None:
    if not PREFIX.match(prefix):
        raise OverlayRegistrationError(f"prefix must be an identifier ending in '_': {prefix!r}")


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--overlay-dir", type=Path, required=True, help="ps2_recomp output directory")
    parser.add_argument("--id", type=int, required=True, help="overlay id from the file header")
    parser.add_argument("--name", required=True)
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    prefix = f"ov{args.id}_"
    try:
        pairs = parse_register_file((args.overlay_dir / "register_functions.cpp").read_text())
        header = (args.overlay_dir / "ps2_recompiled_functions.h").read_text()
        (args.overlay_dir / "overlay_rename.h").write_text(build_rename_header(header, prefix))
        (args.overlay_dir / "overlay_registration.cpp").write_text(build_registration(pairs, args.id, prefix, args.name))
    except (OSError, OverlayRegistrationError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    print(f"Wrote overlay registration for {args.name}: {len(pairs)} entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
