#!/usr/bin/env python3
"""Behavioral tests for the function map guardrail (tools/verify_function_map.py)."""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import verify_function_map as guardrail

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT = REPO_ROOT / "tools" / "verify_function_map.py"
LOAD_ADDRESS = 0x100000
JR_RA = 0x03E00008
NOP = 0x00000000
DATA_WORD = 0x30A5FFFF


def build_elf(words: list) -> bytes:
    """Build a minimal little-endian ELF32 MIPS image with one PT_LOAD segment."""
    header_size, program_header_size = 52, 32
    payload = struct.pack(f"<{len(words)}I", *words)
    payload_offset = header_size + program_header_size
    identity = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    header = identity + struct.pack(
        "<HHIIIIIHHHHHH", 2, 8, 1, LOAD_ADDRESS, header_size, 0, 0,
        header_size, program_header_size, 1, 0, 0, 0,
    )
    program_header = struct.pack(
        "<IIIIIIII", 1, payload_offset, LOAD_ADDRESS, LOAD_ADDRESS,
        len(payload), len(payload), 7, 16,
    )
    return header + program_header + payload


def code_words(function_count: int, function_words: int) -> list:
    """Return words for N functions, each ending in `jr $ra` plus a delay slot."""
    body = [NOP] * (function_words - 2) + [JR_RA, NOP]
    return body * function_count


def write_map(path: Path, rows: list) -> None:
    lines = ["Name,Start,End,Size"]
    lines += [f"{name},{start:#010x},{end:#010x},{end - start}" for name, start, end in rows]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class GuardrailFixture(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary_directory.name)
        self.elf_path = self.directory / "image.elf"
        self.map_path = self.directory / "function_map.csv"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write_image(self, words: list) -> None:
        self.elf_path.write_bytes(build_elf(words))

    def run_cli(self, *extra: str) -> subprocess.CompletedProcess:
        command = [sys.executable, str(SCRIPT), "--map", str(self.map_path), "--elf", str(self.elf_path)]
        return subprocess.run(command + list(extra), capture_output=True, text=True)


class TestViolationDetection(GuardrailFixture):
    def policy(self, max_entry_bytes=4096, max_return_gap_bytes=256):
        return guardrail.GuardrailPolicy(max_entry_bytes, max_return_gap_bytes)

    def violations_for(self, rows: list, policy) -> list:
        write_map(self.map_path, rows)
        entries = guardrail.load_function_map(self.map_path)
        image = guardrail.LoadImage.from_elf(self.elf_path)
        return guardrail.find_violations(entries, image, policy)

    def test_well_formed_functions_pass(self):
        self.write_image(code_words(function_count=3, function_words=8))
        rows = [(f"f{index}", LOAD_ADDRESS + index * 32, LOAD_ADDRESS + (index + 1) * 32) for index in range(3)]
        self.assertEqual(self.violations_for(rows, self.policy()), [])

    def test_oversized_entry_is_rejected(self):
        self.write_image(code_words(function_count=40, function_words=8))
        rows = [("huge", LOAD_ADDRESS, LOAD_ADDRESS + 40 * 32)]
        violations = self.violations_for(rows, self.policy(max_entry_bytes=512))
        self.assertEqual([violation.reason for violation in violations], [guardrail.REASON_OVERSIZED])

    def test_entry_spanning_return_free_data_is_rejected(self):
        self.write_image(code_words(1, 8) + [DATA_WORD] * 128 + code_words(1, 8))
        end = LOAD_ADDRESS + (8 + 128 + 8) * 4
        violations = self.violations_for([("spans_data", LOAD_ADDRESS + 32, end)], self.policy())
        self.assertEqual([violation.reason for violation in violations], [guardrail.REASON_RETURN_GAP])

    def test_small_entry_inside_large_data_region_is_rejected(self):
        self.write_image(code_words(1, 8) + [DATA_WORD] * 512 + code_words(1, 8))
        start = LOAD_ADDRESS + 32 + 200 * 4
        violations = self.violations_for([("pointer_into_data", start, start + 64)], self.policy())
        self.assertEqual([violation.reason for violation in violations], [guardrail.REASON_INSIDE_DATA])

    def test_function_head_before_first_return_after_data_is_allowed(self):
        self.write_image(code_words(1, 8) + [DATA_WORD] * 512 + code_words(1, 8))
        start = LOAD_ADDRESS + 32 + 512 * 4
        self.assertEqual(self.violations_for([("real_function", start, start + 32)], self.policy()), [])

    def test_tail_call_thunk_right_after_data_is_allowed(self):
        self.write_image(code_words(1, 8) + [DATA_WORD] * 512 + [NOP] * 4 + code_words(1, 8))
        start = LOAD_ADDRESS + 32 + 512 * 4
        self.assertEqual(self.violations_for([("thunk", start, start + 16)], self.policy()), [])

    def test_small_entry_without_return_is_allowed(self):
        self.write_image([DATA_WORD] * 16 + code_words(1, 8))
        rows = [("tail_call_stub", LOAD_ADDRESS, LOAD_ADDRESS + 16)]
        self.assertEqual(self.violations_for(rows, self.policy()), [])

    def test_entry_outside_loaded_image_is_rejected(self):
        self.write_image(code_words(1, 8))
        rows = [("outside", LOAD_ADDRESS + 0x1000, LOAD_ADDRESS + 0x1020)]
        violations = self.violations_for(rows, self.policy())
        self.assertEqual([violation.reason for violation in violations], [guardrail.REASON_OUTSIDE_IMAGE])


class TestCommandLine(GuardrailFixture):
    def test_clean_map_exits_zero(self):
        self.write_image(code_words(2, 8))
        write_map(self.map_path, [("a", LOAD_ADDRESS, LOAD_ADDRESS + 32), ("b", LOAD_ADDRESS + 32, LOAD_ADDRESS + 64)])
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 entries", result.stdout)

    def test_violation_exits_one(self):
        self.write_image(code_words(1, 8) + [DATA_WORD] * 4096)
        write_map(self.map_path, [("data_as_code", LOAD_ADDRESS + 32, LOAD_ADDRESS + 32 + 4096 * 4)])
        result = self.run_cli("--max-return-gap-bytes", "1024")
        self.assertEqual(result.returncode, 1)
        self.assertIn(guardrail.REASON_RETURN_GAP, result.stderr)

    def test_empty_map_is_rejected(self):
        self.write_image(code_words(1, 8))
        write_map(self.map_path, [])
        self.assertEqual(self.run_cli().returncode, 2)

    def test_malformed_row_is_rejected(self):
        self.write_image(code_words(1, 8))
        self.map_path.write_text("Name,Start,End,Size\nbroken,0x00100020,0x00100000,0\n", encoding="utf-8")
        self.assertEqual(self.run_cli().returncode, 2)

    def test_missing_elf_is_rejected(self):
        write_map(self.map_path, [("a", LOAD_ADDRESS, LOAD_ADDRESS + 32)])
        self.assertEqual(self.run_cli().returncode, 2)


if __name__ == "__main__":
    unittest.main()
