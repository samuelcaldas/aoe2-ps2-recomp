"""Target specification for Age of Empires II PS2 recompilation.

Strictly defines the single supported target:
SLES-50282 PAL (AOK.ELF).
"""
from dataclasses import dataclass
from pathlib import Path

EXPECTED_SERIAL = "SLES-50282"
EXPECTED_FILENAME = "AOK.ELF"
EXPECTED_SHA1 = "f478e19f2059d52899b691483ef5c7522e30e17c"
EXPECTED_REGION = "PAL"
EXPECTED_TITLE = "Age of Empires II: The Age of Kings (PS2 PAL)"

@dataclass(frozen=True)
class TargetMetadata:
    serial: str = EXPECTED_SERIAL
    filename: str = EXPECTED_FILENAME
    expected_sha1: str = EXPECTED_SHA1
    region: str = EXPECTED_REGION
    title: str = EXPECTED_TITLE

    @property
    def relative_dest_path(self) -> Path:
        return Path("game") / self.serial / self.filename

SINGLE_TARGET = TargetMetadata()
