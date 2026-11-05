#!/usr/bin/env python3
"""Extract the owned ISO, build, and stage ready-to-copy dist/<device>/ folders.

dist/pc/                 game folder + desktop binary
dist/switch/switch/aoe2/ copy the inner "switch" folder to the SD card root
dist/android/            APK + data folder (push data to the app's files dir)
dist/ is git-ignored and contains the user's owned disc image by default.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools import extract_disc

CD_ROOT = REPO_ROOT / "game" / "SLES-50282"
DIST = REPO_ROOT / "dist"
EXCLUDED_NAMES = {"aoe2.iso", "trace_addrs.txt", "trace_addrs.input.txt"}
PLATFORMS = ("pc", "switch", "android")


def is_excluded(name: str) -> bool:
    return name.lower() in EXCLUDED_NAMES or name.lower().endswith(".iso")


def copy_game_folder(destination: Path) -> int:
    """Copy the CD root (without any ISO) into destination; return the entry count."""
    destination.mkdir(parents=True, exist_ok=True)
    entries = [entry for entry in CD_ROOT.iterdir() if not is_excluded(entry.name)]
    for entry in entries:
        target = destination / entry.name
        if entry.is_dir():
            shutil.copytree(entry, target, dirs_exist_ok=True, ignore=lambda _d, names: [n for n in names if is_excluded(n)])
        else:
            shutil.copy2(entry, target)
    return len(entries)


def build_output(platform: str, build_type: str) -> Path:
    suffix = build_type.lower()
    if platform == "pc":
        return REPO_ROOT / "build" / f"docker-desktop-{suffix}"
    if platform == "switch":
        return REPO_ROOT / "build" / f"docker-switch-{suffix}"
    return REPO_ROOT / "build" / "docker-android"


def stage_platform(platform: str, build_type: str, iso: Path | None = None) -> Path:
    output = build_output(platform, build_type)
    data_target = (DIST / "pc" if platform == "pc" else
                   DIST / "switch" / "switch" / "aoe2" if platform == "switch" else
                   DIST / "android" / "data")
    if iso is None and (data_target / "aoe2.iso").exists():
        raise FileExistsError(f"remove the existing ISO before staging extracted-only files: {data_target / 'aoe2.iso'}")
    if platform == "pc":
        target = DIST / "pc"
        copy_game_folder(target)
        if iso is not None:
            shutil.copy2(iso, target / "aoe2.iso")
        shutil.copy2(output / "aoe2_game", target / "aoe2_game")
        return target
    if platform == "switch":
        target = DIST / "switch" / "switch" / "aoe2"
        copy_game_folder(target)
        if iso is not None:
            shutil.copy2(iso, target / "aoe2.iso")
        for nro in output.glob("*.nro"):
            shutil.copy2(nro, target / nro.name)
        return DIST / "switch"
    target = DIST / "android"
    copy_game_folder(target / "data")
    if iso is not None:
        shutil.copy2(iso, target / "data" / "aoe2.iso")
    for apk in output.glob(f"*{build_type.lower()}.apk"):
        shutil.copy2(apk, target / apk.name)
    return target


def run_build(platform: str, args: argparse.Namespace) -> None:
    command = [sys.executable, str(REPO_ROOT / "tools" / "docker_build.py"), "--platform", platform,
               "--jobs", str(args.jobs), "--build-type", args.build_type, "--skip-extract"]
    subprocess.run(command, check=True)


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--platform", choices=(*PLATFORMS, "all"), default="all")
    parser.add_argument("--docker", action="store_true", help="build in Docker (docker-dev context)")
    parser.add_argument("--skip-extract", action="store_true")
    parser.add_argument("--iso", type=Path)
    parser.add_argument("--experimental-extracted-disc", action="store_true",
                        help="stage extracted files without an ISO (experimental; sector reads are unsupported)")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--build-type", choices=("Debug", "Release", "RelWithDebInfo"), default="Release")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    platforms = PLATFORMS if args.platform == "all" else (args.platform,)
    try:
        iso = extract_disc.find_iso(args.iso)
        if not args.experimental_extracted_disc and (iso is None or not iso.is_file()):
            raise extract_disc.DiscExtractError("no ISO found: put it in game/iso/ or pass --iso <path>")
        if not args.skip_extract:
            print(f"[disc] {extract_disc.ensure_extracted(CD_ROOT, args.iso)}", flush=True)
        for platform in platforms:
            if args.docker:
                run_build(platform, args)
            print(f"[dist] {platform}: {stage_platform(platform, args.build_type, None if args.experimental_extracted_disc else iso)}", flush=True)
    except (extract_disc.DiscExtractError, subprocess.CalledProcessError, FileNotFoundError, FileExistsError) as error:
        sys.stderr.write(f"Error: {error}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
