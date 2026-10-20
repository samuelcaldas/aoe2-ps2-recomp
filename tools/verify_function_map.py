#!/usr/bin/env python3
"""Guardrail that rejects function map entries which are data, not code.

A stripped PS2 ELF merges code and data in one executable segment, so an entry
can silently span data and make PS2Recomp emit enormous, meaningless C++.
Every entry is checked against the owned ELF:
- its range must lie inside a loaded segment;
- it must not exceed a maximum size;
- it must not contain a stretch without any `jr $ra` return longer than a
  maximum (real code returns far more often than that);
- an entry without any return must reach one within that maximum (catches
  small entries created from data words that look like pointers, while
  allowing tail-call thunks right before real code).

Exit codes: 0 map ok, 1 violations found, 2 invalid input.
Output reports counts and entry names only; it never prints code bytes.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.target import SINGLE_TARGET

DEFAULT_MAP_PATH = REPO_ROOT / "analysis" / "generated" / "function_map.csv"
DEFAULT_MAX_ENTRY_BYTES = 128 * 1024
DEFAULT_MAX_RETURN_GAP_BYTES = 64 * 1024
JR_RA_WORD = 0x03E00008
INSTRUCTION_BYTES = 4
RETURN_SEQUENCE_BYTES = 8
PT_LOAD = 1
REQUIRED_COLUMNS = ("Name", "Start", "End")

REASON_OVERSIZED = "oversized_entry"
REASON_RETURN_GAP = "return_free_span"
REASON_OUTSIDE_IMAGE = "outside_loaded_image"
REASON_INSIDE_DATA = "inside_return_free_region"

EXIT_OK, EXIT_VIOLATIONS, EXIT_INVALID_INPUT = 0, 1, 2


class GuardrailInputError(ValueError):
    """Raised when the function map or ELF cannot be used for verification."""


@dataclass(frozen=True)
class GuardrailPolicy:
    """Limits an entry must respect to be accepted as code."""

    max_entry_bytes: int
    max_return_gap_bytes: int


@dataclass(frozen=True)
class FunctionMapEntry:
    """One exported function map row as a half-open address range."""

    name: str
    start: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.start


@dataclass(frozen=True)
class Violation:
    """An entry that failed a guardrail rule."""

    entry: FunctionMapEntry
    reason: str
    measured_bytes: int


@dataclass(frozen=True)
class LoadedSegment:
    start: int
    data: bytes

    @property
    def end(self) -> int:
        return self.start + len(self.data)


class LoadImage:
    """File-backed PT_LOAD segments of an ELF32 little-endian image, plus return sites."""

    def __init__(self, segments: list):
        self._segments = segments
        self._return_addresses = sorted(self._scan_returns(segments))

    @classmethod
    def from_elf(cls, path: Path) -> "LoadImage":
        """Parse the loadable, file-backed segments of an ELF32 LE file.

        Raises GuardrailInputError if the file is missing or not ELF32 LE.
        """
        if not path.is_file():
            raise GuardrailInputError(f"ELF not found: {path}")
        raw = path.read_bytes()
        if raw[:4] != b"\x7fELF" or raw[4] != 1 or raw[5] != 1:
            raise GuardrailInputError(f"not a little-endian ELF32 file: {path}")
        return cls(_read_load_segments(raw))

    def contains(self, start: int, end: int) -> bool:
        return any(segment.start <= start and end <= segment.end for segment in self._segments)

    def longest_return_free_span(self, start: int, end: int) -> int:
        """Return the longest byte stretch in [start, end) not interrupted by a `jr $ra` sequence."""
        first = bisect.bisect_left(self._return_addresses, start)
        last = bisect.bisect_left(self._return_addresses, end)
        cursor, longest = start, 0
        for return_address in self._return_addresses[first:last]:
            longest = max(longest, return_address - cursor)
            cursor = return_address + RETURN_SEQUENCE_BYTES
        return max(longest, end - cursor)

    def distance_to_next_return(self, start: int, end: int) -> int:
        """Return how far code starting at `start` runs before a return, or 0 if [start, end) holds one.

        Data rarely contains the `jr $ra` word, so an entry that is far from any
        return is data; a tail-call thunk just before real code is close to one.
        """
        first = bisect.bisect_left(self._return_addresses, start)
        if first < len(self._return_addresses) and self._return_addresses[first] < end:
            return 0
        segment = next(segment for segment in self._segments if segment.start <= start < segment.end)
        next_return = self._return_addresses[first] if first < len(self._return_addresses) else segment.end
        return min(next_return, segment.end) - start

    @staticmethod
    def _scan_returns(segments: list) -> list:
        addresses = []
        for segment in segments:
            word_count = len(segment.data) // INSTRUCTION_BYTES
            words = struct.unpack_from(f"<{word_count}I", segment.data)
            addresses += [segment.start + index * INSTRUCTION_BYTES for index, word in enumerate(words) if word == JR_RA_WORD]
        return addresses


def _read_load_segments(raw: bytes) -> list:
    program_header_offset = struct.unpack_from("<I", raw, 28)[0]
    entry_size, entry_count = struct.unpack_from("<HH", raw, 42)
    segments = []
    for index in range(entry_count):
        fields = struct.unpack_from("<IIIIII", raw, program_header_offset + index * entry_size)
        segment_type, file_offset, virtual_address, _, file_size, _ = fields
        if segment_type == PT_LOAD and file_size > 0:
            segments.append(LoadedSegment(virtual_address, raw[file_offset:file_offset + file_size]))
    if not segments:
        raise GuardrailInputError("ELF has no file-backed PT_LOAD segment")
    return segments


def load_function_map(path: Path) -> list:
    """Read and validate the exported function map CSV.

    Raises GuardrailInputError on a missing file, missing columns, malformed
    rows, or an empty map.
    """
    if not path.is_file():
        raise GuardrailInputError(f"function map not found: {path}")
    with path.open(encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        missing = [column for column in REQUIRED_COLUMNS if column not in (reader.fieldnames or [])]
        if missing:
            raise GuardrailInputError(f"function map missing columns: {', '.join(missing)}")
        entries = [_parse_row(row, line_number) for line_number, row in enumerate(reader, start=2)]
    if not entries:
        raise GuardrailInputError("function map has no entries")
    return entries


def _parse_row(row: dict, line_number: int) -> FunctionMapEntry:
    try:
        entry = FunctionMapEntry(row["Name"].strip(), int(row["Start"], 16), int(row["End"], 16))
    except (TypeError, ValueError) as error:
        raise GuardrailInputError(f"malformed function map row at line {line_number}: {error}") from error
    if not entry.name or entry.end <= entry.start:
        raise GuardrailInputError(f"invalid function map range at line {line_number}")
    return entry


def find_violations(entries: list, image: LoadImage, policy: GuardrailPolicy) -> list:
    """Return every rule violation, at most one per entry, in map order."""
    violations = []
    for entry in entries:
        violation = _check_entry(entry, image, policy)
        if violation:
            violations.append(violation)
    return violations


def _check_entry(entry: FunctionMapEntry, image: LoadImage, policy: GuardrailPolicy):
    if not image.contains(entry.start, entry.end):
        return Violation(entry, REASON_OUTSIDE_IMAGE, entry.size)
    if entry.size > policy.max_entry_bytes:
        return Violation(entry, REASON_OVERSIZED, entry.size)
    return_free_span = image.longest_return_free_span(entry.start, entry.end)
    if return_free_span > policy.max_return_gap_bytes:
        return Violation(entry, REASON_RETURN_GAP, return_free_span)
    run_to_return = image.distance_to_next_return(entry.start, entry.end)
    if run_to_return > policy.max_return_gap_bytes:
        return Violation(entry, REASON_INSIDE_DATA, run_to_return)
    return None


def parse_args(argv: list) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Reject function map entries that span data instead of code.")
    parser.add_argument("--map", type=Path, default=DEFAULT_MAP_PATH)
    parser.add_argument("--elf", type=Path, default=REPO_ROOT / SINGLE_TARGET.relative_dest_path)
    parser.add_argument("--max-entry-bytes", type=int, default=DEFAULT_MAX_ENTRY_BYTES)
    parser.add_argument("--max-return-gap-bytes", type=int, default=DEFAULT_MAX_RETURN_GAP_BYTES)
    return parser.parse_args(argv)


def report(entries: list, violations: list) -> int:
    print(f"Checked {len(entries)} entries; {len(violations)} violation(s).")
    for violation in violations:
        sys.stderr.write(f"VIOLATION {violation.reason}: {violation.entry.name} ({violation.measured_bytes} bytes)\n")
    return EXIT_VIOLATIONS if violations else EXIT_OK


def main(argv: list | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        policy = GuardrailPolicy(args.max_entry_bytes, args.max_return_gap_bytes)
        entries = load_function_map(args.map)
        image = LoadImage.from_elf(args.elf)
    except GuardrailInputError as error:
        sys.stderr.write(f"Error: {error}\n")
        return EXIT_INVALID_INPUT
    return report(entries, find_violations(entries, image, policy))


if __name__ == "__main__":
    sys.exit(main())
