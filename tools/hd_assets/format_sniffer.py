"""Identifies asset formats from their leading bytes, never from file extensions."""

from typing import Final

UNKNOWN_FORMAT: Final = "unknown"
SLP_VERSIONS: Final = (b"2.0N", b"3.0", b"4.0X", b"4.1X", b"4.2P")
PREFIX_FORMATS: Final = (
    (b"JASC-PAL", "jasc-pal"),
    (b"BM", "bmp"),
    (b"SLDX", "sld"),
    (b"SMPX", "smpx"),
    (b"DDS ", "dds"),
    (b"\x89PNG", "png"),
    (b"RIFF", "riff"),
)


def sniff_format(header: bytes) -> str:
    """Return a format label for the leading bytes of an asset, or 'unknown'."""
    for signature, label in PREFIX_FORMATS:
        if header.startswith(signature):
            return label
    version = header[:4].rstrip(b"\x00 ")
    if version in SLP_VERSIONS:
        return f"slp-{version.decode('ascii')}"
    return UNKNOWN_FORMAT
