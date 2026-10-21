"""Reads the JASC-PAL palettes supplied with AoE II HD Edition."""

from tools.hd_assets.errors import HdAssetError

SIGNATURE = "JASC-PAL"
VERSION = "0100"
COLOR_COUNT = 256
OPAQUE_ALPHA = 255
CHANNEL_COUNT = 3


def read_jasc_palette(data: bytes) -> tuple[tuple[int, int, int, int], ...]:
    """Parse a 256-color JASC-PAL byte stream into opaque RGBA entries."""
    lines = _decode_lines(data)
    if len(lines) != COLOR_COUNT + 3:
        raise HdAssetError("JASC-PAL must contain exactly 256 RGB entries")
    if lines[0] != SIGNATURE or lines[1] != VERSION:
        raise HdAssetError("unsupported JASC-PAL signature or version")
    if _parse_count(lines[2]) != COLOR_COUNT:
        raise HdAssetError("JASC-PAL must declare exactly 256 colors")
    return tuple(_parse_color(line) for line in lines[3:])


def _decode_lines(data: bytes) -> list[str]:
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as error:
        raise HdAssetError("JASC-PAL must contain ASCII text") from error
    return [line.strip() for line in text.splitlines()]


def _parse_count(value: str) -> int:
    try:
        return int(value)
    except ValueError as error:
        raise HdAssetError("JASC-PAL color count is not an integer") from error


def _parse_color(line: str) -> tuple[int, int, int, int]:
    fields = line.split()
    if len(fields) != CHANNEL_COUNT:
        raise HdAssetError("JASC-PAL color entries must contain three channels")
    channels = tuple(_parse_channel(field) for field in fields)
    return (channels[0], channels[1], channels[2], OPAQUE_ALPHA)


def _parse_channel(value: str) -> int:
    try:
        channel = int(value)
    except ValueError as error:
        raise HdAssetError("JASC-PAL color channel is not an integer") from error
    if not 0 <= channel <= OPAQUE_ALPHA:
        raise HdAssetError("JASC-PAL color channel is outside the byte range")
    return channel
