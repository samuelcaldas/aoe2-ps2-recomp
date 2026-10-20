"""SHA-256 helpers shared by the catalog, importer, and verifier."""

import hashlib
from collections.abc import Iterable
from pathlib import Path

CHUNK_SIZE = 65536


def sha256_file(path: Path) -> str:
    """Return the lowercase hex SHA-256 of a file, read in streaming chunks."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while chunk := handle.read(CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_of_digests(digests: Iterable[str]) -> str:
    """Return one SHA-256 over a sorted set of digests, so order never changes the result."""
    return hashlib.sha256("\n".join(sorted(digests)).encode("ascii")).hexdigest()
