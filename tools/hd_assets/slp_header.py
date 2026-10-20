"""Reads the frame count and first-frame size from an SLP 2.0N file header."""

import struct
from dataclasses import dataclass
from pathlib import Path

from tools.hd_assets.errors import HdAssetError

FILE_HEADER = struct.Struct("<4si24s")
FRAME_INFO = struct.Struct("<4I4i")
SLP_2_0N_SIGNATURE = b"2.0N"


@dataclass(frozen=True)
class SlpSize:
    frame_count: int
    width: int
    height: int


def read_slp_size(path: Path) -> SlpSize:
    """Return the frame count and the first frame's width and height; raise if the header is not SLP 2.0N."""
    with path.open("rb") as handle:
        header = handle.read(FILE_HEADER.size)
        if len(header) < FILE_HEADER.size or not header.startswith(SLP_2_0N_SIGNATURE):
            raise HdAssetError(f"not an SLP 2.0N file: {path.name}")
        _, frame_count, _ = FILE_HEADER.unpack(header)
        frame = handle.read(FRAME_INFO.size)
    if len(frame) < FRAME_INFO.size or frame_count < 1:
        raise HdAssetError(f"truncated SLP 2.0N file: {path.name}")
    fields = FRAME_INFO.unpack(frame)
    return SlpSize(frame_count=frame_count, width=fields[4], height=fields[5])
