#!/usr/bin/env python3
"""Install the pinned Emotion Engine (R5900) language extension for Ghidra.

Ghidra's stock MIPS languages do not decode R5900-only instructions (EI/DI,
MMI, LQ/SQ, ...), so function bodies stop at the first one and the exported
map has gaps. ghidra-emotionengine-reloaded (Apache-2.0) adds the
`r5900:LE:32:default` language. The release asset is pinned per Ghidra version
and verified by SHA-256 before it is unpacked into the user's Ghidra extension
directory. Idempotent: an installed copy with the same digest is left alone.

Exit codes: 0 installed or already installed, 1 error.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path

EXTENSION_DIR_NAME = "ghidra-emotionengine-reloaded"
EXTENSION_REPOSITORY = "chaoticgd/ghidra-emotionengine-reloaded"
LANGUAGE_ID = "r5900:LE:32:default"
DIGEST_FILE = ".aoe2_installed_sha256"
INSTALLED = "installed"
ALREADY_INSTALLED = "already_installed"


class ExtensionInstallError(RuntimeError):
    """Raised when the extension cannot be installed safely."""


@dataclass(frozen=True)
class ExtensionRelease:
    tag: str
    asset: str
    sha256: str


PINNED_RELEASES = {
    "ghidra_12.1.4_PUBLIC": ExtensionRelease(
        tag="v2.1.38",
        asset="ghidra_12.1.4_PUBLIC_20261004_ghidra-emotionengine-reloaded.zip",
        sha256="e389ff3e921d9f7d4db2ece9906f808545ce4ff519d9e25a019b72ea509f240b",
    ),
}


def release_for(ghidra_version: str) -> ExtensionRelease:
    """Return the pinned release for a Ghidra install directory name."""
    if ghidra_version not in PINNED_RELEASES:
        known = ", ".join(sorted(PINNED_RELEASES))
        raise ExtensionInstallError(f"no pinned EE extension for {ghidra_version}; known: {known}")
    return PINNED_RELEASES[ghidra_version]


def user_extensions_dir(ghidra_version: str) -> Path:
    config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return config_home / "ghidra" / ghidra_version / "Extensions"


def is_installed(extensions_dir: Path) -> bool:
    return (extensions_dir / EXTENSION_DIR_NAME / "extension.properties").is_file()


def installed_digest(extensions_dir: Path) -> str:
    marker = extensions_dir / EXTENSION_DIR_NAME / DIGEST_FILE
    return marker.read_text(encoding="utf-8").strip() if marker.is_file() else ""


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def install_archive(archive: Path, expected_sha256: str, extensions_dir: Path) -> str:
    """Verify and unpack the extension archive; return INSTALLED or ALREADY_INSTALLED.

    Raises ExtensionInstallError on a digest mismatch or a malformed archive.
    """
    if installed_digest(extensions_dir) == expected_sha256 and is_installed(extensions_dir):
        return ALREADY_INSTALLED
    if sha256_of(archive) != expected_sha256:
        raise ExtensionInstallError(f"SHA-256 mismatch for {archive.name}")
    extensions_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=extensions_dir) as staging_name:
        staging = Path(staging_name)
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(staging)
        unpacked = staging / EXTENSION_DIR_NAME
        if not (unpacked / "extension.properties").is_file():
            raise ExtensionInstallError(f"{archive.name} does not contain {EXTENSION_DIR_NAME}/extension.properties")
        (unpacked / DIGEST_FILE).write_text(expected_sha256 + "\n", encoding="utf-8")
        shutil.rmtree(extensions_dir / EXTENSION_DIR_NAME, ignore_errors=True)
        shutil.move(str(unpacked), str(extensions_dir / EXTENSION_DIR_NAME))
    return INSTALLED


def download(release: ExtensionRelease, directory: Path) -> Path:
    command = ["gh", "release", "download", release.tag, "-R", EXTENSION_REPOSITORY,
               "-p", release.asset, "-D", str(directory), "--clobber"]
    if subprocess.run(command).returncode != 0:
        raise ExtensionInstallError(f"failed to download {release.asset}")
    return directory / release.asset


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description="Install the pinned Emotion Engine Ghidra extension.")
    parser.add_argument("--archive", type=Path, help="use a local copy of the pinned release asset")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    ghidra_home = os.environ.get("GHIDRA_HOME")
    try:
        if not ghidra_home:
            raise ExtensionInstallError("GHIDRA_HOME is not set")
        version = Path(ghidra_home).resolve().name
        release = release_for(version)
        extensions_dir = user_extensions_dir(version)
        with tempfile.TemporaryDirectory() as download_dir:
            archive = args.archive or download(release, Path(download_dir))
            status = install_archive(archive.resolve(), release.sha256, extensions_dir)
    except ExtensionInstallError as error:
        sys.stderr.write(f"Error: {error}\n")
        return 1
    print(f"EE extension {status}: {extensions_dir / EXTENSION_DIR_NAME} ({LANGUAGE_ID})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
