"""Read the file-backed parts of the owned ELF (ELF32, little-endian, MIPS) by guest address.

Only PT_LOAD segments with file bytes are readable. The game's code overlays are
PT_LOAD segments with filesz 0; they load from disc at run time and are not
present in the ELF, so they cannot be scanned here.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path

ELF_MAGIC = b"\x7fELF"
ELFCLASS32 = 1
ELFDATA2LSB = 1
EM_MIPS = 8
PT_LOAD = 1
PF_X = 1
# e_ident, e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum, ...
ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIIIIIII")


class ElfFormatError(ValueError):
    """Raised when the input is not a usable ELF32 MIPS image, or an address is not file-backed."""


@dataclass(frozen=True)
class Segment:
    vaddr: int
    offset: int
    filesz: int
    memsz: int
    flags: int

    def covers(self, address: int, length: int) -> bool:
        return self.filesz > 0 and self.vaddr <= address and address + length <= self.vaddr + self.filesz

    @property
    def executable(self) -> bool:
        return bool(self.flags & PF_X)


class ElfImage:
    """File-backed view of an ELF32 MIPS little-endian executable."""

    def __init__(self, data: bytes):
        if len(data) < ELF_HEADER.size or data[:4] != ELF_MAGIC:
            raise ElfFormatError("input is not an ELF file")
        if data[4] != ELFCLASS32:
            raise ElfFormatError("only ELF32 is supported")
        if data[5] != ELFDATA2LSB:
            raise ElfFormatError("only little-endian ELF is supported")
        header = ELF_HEADER.unpack_from(data, 0)
        machine, phoff, phentsize, phnum = header[2], header[5], header[9], header[10]
        if machine != EM_MIPS:
            raise ElfFormatError(f"unexpected machine type {machine}; expected MIPS ({EM_MIPS})")
        if phoff + phnum * phentsize > len(data):
            raise ElfFormatError("program header table points past the end of the file")
        self._data = data
        self._segments = tuple(
            Segment(vaddr, offset, filesz, memsz, flags)
            for p_type, offset, vaddr, _, filesz, memsz, flags, _ in (
                PROGRAM_HEADER.unpack_from(data, phoff + index * phentsize) for index in range(phnum))
            if p_type == PT_LOAD
        )

    def is_file_backed(self, address: int, length: int) -> bool:
        return any(segment.covers(address, length) for segment in self._segments)

    def read(self, address: int, length: int) -> bytes:
        for segment in self._segments:
            if segment.covers(address, length):
                start = segment.offset + address - segment.vaddr
                return self._data[start:start + length]
        raise ElfFormatError(f"0x{address:08x}+{length} is not file-backed")

    def find_bytes(self, needle: bytes) -> list:
        """Guest addresses where `needle` occurs inside file-backed segments."""
        found = []
        for segment in self._segments:
            if segment.filesz == 0:
                continue
            region = self._data[segment.offset:segment.offset + segment.filesz]
            position = region.find(needle)
            while position != -1:
                found.append(segment.vaddr + position)
                position = region.find(needle, position + 1)
        return found

    def code_segments(self) -> list:
        """(guest address, bytes) of every executable file-backed segment."""
        return [(segment.vaddr, self._data[segment.offset:segment.offset + segment.filesz])
                for segment in self._segments if segment.executable and segment.filesz > 0]

    def file_segments(self) -> list:
        """(guest address, bytes) of every file-backed segment, executable or not."""
        return [(segment.vaddr, self._data[segment.offset:segment.offset + segment.filesz])
                for segment in self._segments if segment.filesz > 0]


def load_elf(path: Path) -> ElfImage:
    return ElfImage(Path(path).read_bytes())
