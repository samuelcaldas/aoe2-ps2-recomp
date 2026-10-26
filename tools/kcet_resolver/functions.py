"""Function boundaries from the Ghidra export (analysis/generated/function_map.csv).

The export is the only source of function boundaries: prologue guesses are not
used. Its internal continuation labels are resumable dispatch targets, not
callable hook entries.
"""
from __future__ import annotations

import bisect
import csv
from pathlib import Path

from tools.kcet_resolver.errors import HookResolutionError


def single_function(functions: FunctionMap, addresses: list, role: str) -> tuple:
    found = {function for function in (functions.containing(address) for address in addresses) if function}
    if len(found) != 1:
        raise HookResolutionError(f"expected exactly one {role} function, found {len(found)}")
    return found.pop()


def single(values: list, role: str) -> int:
    if len(values) != 1:
        raise HookResolutionError(f"expected exactly one {role}, found {len(values)}")
    return values[0]


class FunctionMap:
    def __init__(self, rows: list):
        self._rows = sorted(row for row in rows if not row[2].startswith(("entry_", "caseD_")))
        self._starts = [row[0] for row in self._rows]

    @classmethod
    def from_rows(cls, rows: list) -> FunctionMap:
        """Rows of (start, end, name) with integer guest addresses."""
        return cls(rows)

    @classmethod
    def from_csv(cls, path: Path) -> FunctionMap:
        with Path(path).open(newline="", encoding="utf-8") as handle:
            return cls([(int(row["Start"], 16), int(row["End"], 16), row["Name"]) for row in csv.DictReader(handle)])

    def containing(self, address: int) -> tuple | None:
        index = bisect.bisect_right(self._starts, address) - 1
        if index < 0:
            return None
        start, end, name = self._rows[index]
        return (start, end, name) if start <= address < end else None

    def require_start(self, address: int) -> None:
        if address % 4 != 0:
            raise HookResolutionError(f"0x{address:08x} is not instruction-aligned")
        function = self.containing(address)
        if function is None or function[0] != address:
            raise HookResolutionError(f"0x{address:08x} is not the start of a function in the export")

    def all(self) -> list:
        """All (start, end, name) function rows in start-address order."""
        return list(self._rows)
