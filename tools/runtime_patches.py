"""Apply the repository's local patch series to a third-party checkout, idempotently.

Patches are plain `git diff` output with paths relative to the checkout root. A patch
that is already applied is skipped; one that neither applies nor is applied is an
error and nothing is modified.
"""
from __future__ import annotations

import enum
import subprocess
from pathlib import Path


class PatchError(RuntimeError):
    pass


class PatchState(enum.Enum):
    MISSING = "missing"
    APPLIED = "applied"
    CONFLICT = "conflict"


def _git_apply(repo: Path, patch: Path, *flags: str) -> bool:
    result = subprocess.run(["git", "apply", *flags, str(patch.resolve())], cwd=repo, capture_output=True)
    return result.returncode == 0


def patch_state(repo: Path, patch: Path) -> PatchState:
    if _git_apply(repo, patch, "--check"):
        return PatchState.MISSING
    if _git_apply(repo, patch, "--check", "--reverse"):
        return PatchState.APPLIED
    return PatchState.CONFLICT


def series(patch_dir: Path) -> list:
    if not patch_dir.is_dir():
        raise PatchError(f"patch directory not found: {patch_dir}")
    return sorted(patch_dir.glob("*.patch"))


def apply_patches(repo: Path, patch_dir: Path) -> list:
    """Apply every missing patch in name order; return the names applied."""
    applied = []
    for patch in series(patch_dir):
        state = patch_state(repo, patch)
        if state is PatchState.CONFLICT:
            raise PatchError(f"{patch.name} neither applies nor is already applied to {repo}")
        if state is PatchState.MISSING:
            if not _git_apply(repo, patch):
                raise PatchError(f"{patch.name} failed to apply to {repo}")
            applied.append(patch.name)
    return applied


def verify_patches(repo: Path, patch_dir: Path) -> None:
    """Raise unless every patch in the series is already applied."""
    for patch in series(patch_dir):
        if patch_state(repo, patch) is not PatchState.APPLIED:
            raise PatchError(f"{patch.name} is not applied to {repo}; run tools/bootstrap_dependencies.py")
