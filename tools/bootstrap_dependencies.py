#!/usr/bin/env python3
"""Bootstrap and check third-party dependencies.

Manages external repositories:
- PS2Recomp: https://github.com/samuelcaldas/PS2Recomp.git (GPL-3.0)
- libnx:     https://github.com/switchbrew/libnx.git (ISC)
- deko3d:    https://github.com/devkitPro/deko3d.git (Zlib / MIT)

Clones recursively into third_party/ (ignored by git). The game-specific runtime
changes live directly in the PS2Recomp fork; no patch series is applied to it.
Use --record-current NAME to update only that dependency's pin after a local commit.
The tracked lock/provenance file is third_party_provenance.lock.json.
"""
import argparse
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.runtime_patches import PatchError, apply_patches, verify_patches  # noqa: E402

PATCH_DIRECTORIES = {}

DEPENDENCIES = {
    "PS2Recomp": {
        "url": "https://github.com/samuelcaldas/PS2Recomp.git",
        "license": "GPL-3.0",
        "intended_phase": "Desktop recompilation proof of concept",
        "notes": "Static recompiler for MIPS R5900/VU with native AoE2 runtime patches incorporated.",
    },
    "libnx": {
        "url": "https://github.com/switchbrew/libnx.git",
        "license": "ISC",
        "intended_phase": "Later Switch/homebrew integration only",
        "notes": "Nintendo Switch homebrew library. Not linked during desktop phase.",
    },
    "deko3d": {
        "url": "https://github.com/devkitPro/deko3d.git",
        "license": "Zlib",
        "intended_phase": "Later Switch/homebrew integration only",
        "notes": "Low-level graphics API for Nintendo Switch Maxwell GPU.",
    },
}

def get_git_head_sha(repo_dir: Path) -> str:
    """Get the full SHA of HEAD in a git repository."""
    res = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repo_dir,
        capture_output=True,
        text=True,
        check=True,
    )
    return res.stdout.strip()

def parse_args():
    parser = argparse.ArgumentParser(description="Bootstrap or verify third-party repositories.")
    parser.add_argument("--record-current", choices=DEPENDENCIES,
                        help="Record one installed dependency's HEAD without changing other pins")
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check dependency status without cloning or modifying files",
    )
    parser.add_argument(
        "--lock-file",
        type=Path,
        default=None,
        help="Path to lock file (default: <repo_root>/third_party_provenance.lock.json)",
    )
    parser.add_argument(
        "--dest-dir",
        type=Path,
        default=None,
        help="Directory to place third_party repos (default: <repo_root>/third_party)",
    )
    return parser.parse_args()

def record_current(lock_file: Path, dest_dir: Path, name: str):
    """Record an installed repository's commit while preserving unrelated lock entries."""
    data = json.loads(lock_file.read_text(encoding="utf-8"))
    entry = data["dependencies"][name]
    entry["commit_sha"] = get_git_head_sha(dest_dir / name)
    data["generated_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    lock_file.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def verify_locked_commits(lock_file: Path, dest_dir: Path):
    """Reject installed dependency commits that differ from their recorded pins."""
    data = json.loads(lock_file.read_text(encoding="utf-8"))
    for name in DEPENDENCIES:
        expected = data["dependencies"][name]["commit_sha"]
        actual = get_git_head_sha(dest_dir / name)
        if actual != expected:
            raise ValueError(f"{name} commit mismatch: lockfile {expected}, installed {actual}")


def main():
    args = parse_args()
    lock_file = (args.lock_file or (REPO_ROOT / "third_party_provenance.lock.json")).resolve()
    dest_dir = (args.dest_dir or (REPO_ROOT / "third_party")).resolve()

    if args.record_current:
        if args.check:
            raise SystemExit("--check and --record-current are mutually exclusive")
        record_current(lock_file, dest_dir, args.record_current)
        return

    if args.check:
        missing = []
        for name in DEPENDENCIES:
            repo_path = dest_dir / name
            if not repo_path.exists() or not (repo_path / ".git").exists():
                missing.append(name)
        if missing:
            sys.stderr.write(f"Error: Missing dependencies in {dest_dir}: {', '.join(missing)}\n")
            sys.exit(1)
        if not lock_file.exists():
            sys.stderr.write(f"Error: Lock file missing: {lock_file}\n")
            sys.exit(1)
        try:
            verify_locked_commits(lock_file, dest_dir)
            for name, patch_dir in PATCH_DIRECTORIES.items():
                verify_patches(dest_dir / name, patch_dir)
        except (PatchError, ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
            sys.stderr.write(f"Error: {error}\n")
            sys.exit(1)
        print("All installed dependency commits and configured patch series verified against the lock file.")
        sys.exit(0)

    dest_dir.mkdir(parents=True, exist_ok=True)
    lock_data = {
        "version": 1,
        "generated_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "dependencies": {},
    }

    for name, meta in DEPENDENCIES.items():
        repo_path = dest_dir / name
        if not repo_path.exists():
            print(f"Cloning {name} from {meta['url']} (recursively)...")
            subprocess.run(
                ["git", "clone", "--recursive", meta["url"], str(repo_path)],
                check=True,
            )
        else:
            print(f"Repository {name} already present at {repo_path}")
            # Ensure submodules are initialized if present
            subprocess.run(
                ["git", "submodule", "update", "--init", "--recursive"],
                cwd=repo_path,
                check=True,
            )

        if name in PATCH_DIRECTORIES:
            try:
                for applied in apply_patches(repo_path, PATCH_DIRECTORIES[name]):
                    print(f"Applied local patch {applied} to {name}")
            except PatchError as error:
                sys.stderr.write(f"Error: {error}\n")
                sys.exit(1)

        sha = get_git_head_sha(repo_path)
        lock_data["dependencies"][name] = {
            "url": meta["url"],
            "commit_sha": sha,
            "license": meta["license"],
            "intended_phase": meta["intended_phase"],
            "notes": meta["notes"],
            "local_path": str(repo_path.relative_to(REPO_ROOT)) if repo_path.is_relative_to(REPO_ROOT) else str(repo_path),
        }

    lock_file.write_text(json.dumps(lock_data, indent=2) + "\n", encoding="utf-8")
    print(f"Dependencies locked successfully in {lock_file}")
    sys.exit(0)

if __name__ == "__main__":
    main()
