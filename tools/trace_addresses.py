#!/usr/bin/env python3
"""Write the ignored call-trace address list from a decomp module's function definitions.

The diagnostics build (game_hooks/src/aoe2_call_trace.cpp) wraps each listed guest
function with a logger. The decomp names functions by guest address, so the
definitions in one module give the list. Output is game/SLES-50282/trace_addrs.txt
(ignored).

Exit codes: 0 written, 1 input missing or no functions found.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO_ROOT / "game" / "SLES-50282" / "trace_addrs.txt"
DEFINITION = re.compile(r"^[A-Za-z_][\w \*]*\bfunc_([0-9A-Fa-f]{6,8})\s*\([^;{]*\)\s*\{", re.MULTILINE)


class TraceListError(ValueError):
    pass


def parse_function_addresses(source: str) -> list:
    addresses = sorted({int(match, 16) for match in DEFINITION.findall(source)})
    if not addresses:
        raise TraceListError("no function definitions found")
    return addresses


def render_trace_list(addresses: list) -> str:
    return "".join(f"0x{address:x}\n" for address in sorted(addresses))


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--module", required=True, type=Path, help="decomp C file, e.g. .../kt_kernel.c")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        addresses = parse_function_addresses(args.module.read_text(errors="replace"))
    except (OSError, TraceListError) as error:
        sys.stderr.write(f"FAILED: {error}\n")
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render_trace_list(addresses))
    print(f"Wrote {len(addresses)} addresses to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
