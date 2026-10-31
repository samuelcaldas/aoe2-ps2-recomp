#!/usr/bin/env python3
"""Validate the desktop build by running the game on a virtual display.

Runs the diagnostic build (build/docker-desktop-diagnostics/aoe2_game, see
`docker_build.py --diagnostics`) on the owned ELF under Xvfb and captures a
screenshot every interval into .playwright-mcp/pc/ (git-ignored). The full log goes
to build/desktop-validation/run.log.

PASS needs all of these:
- guest execution started and the process was still running at the end;
- the log shows the guest progressing (classify_diagnostics: PROGRESSING);
- at least two screenshots are real frames (not the runtime's magenta fallback)
  and they differ.

Exit codes: 0 pass, 1 fail, 2 preflight failed.
"""
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.classify_diagnostics import RunVerdict, classify_run_log  # noqa: E402
from tools.frame_analysis import FrameSeries, analyze_frames, run_command  # noqa: E402

CONTAINER_ROOT = "/src"
IMAGE = "aoe2-build-desktop"
DOCKER_CONTEXT = "docker-dev"
CONTAINER_NAME = "aoe2-validate-desktop"
DEFAULT_JOBS = 8
MEMORY_LIMIT = "18g"

GAME_BINARY = REPO_ROOT / "build" / "docker-desktop-diagnostics" / "aoe2_game"
OWNED_ELF = REPO_ROOT / "game" / "SLES-50282" / "AOK.ELF"
# Ignored; the game reads by real disc LBN, which only the image resolves.
OWNED_DISC_IMAGE = REPO_ROOT / "game" / "SLES-50282" / "aoe2.iso"
EVIDENCE_DIR = REPO_ROOT / "build" / "desktop-validation"
SCREENSHOT_DIR = REPO_ROOT / ".playwright-mcp" / "pc"
DISPLAY = ":99"
STARTED_MARKER = "Starting execution"
MISSING_TARGET_MARKER = "[guest-branch:missing-target]"
EXIT_PASS, EXIT_FAIL, EXIT_PREFLIGHT = 0, 1, 2


def ensure_container_clean(name: str) -> None:
    """Ensure no stale stopped container with `name` blocks docker run --name."""
    inspect = subprocess.run(
        ["docker", "--context", DOCKER_CONTEXT, "container", "inspect", "-f", "{{.State.Running}}", name],
        capture_output=True,
        text=True,
    )
    if inspect.returncode == 0:
        if inspect.stdout.strip() == "false":
            subprocess.run(
                ["docker", "--context", DOCKER_CONTEXT, "rm", "-f", name],
                capture_output=True,
            )
        else:
            raise RuntimeError(f"container '{name}' is already running")


def container_command(run_seconds: int, interval: int = 15, jobs: int = DEFAULT_JOBS) -> list:
    return [
        "docker", "--context", DOCKER_CONTEXT, "run", "--rm",
        f"--name={CONTAINER_NAME}",
        f"--hostname={CONTAINER_NAME}",
        "--label=aoe2.platform=desktop",
        "--label=aoe2.role=validate",
        f"--label=aoe2.container={CONTAINER_NAME}",
        f"--memory={MEMORY_LIMIT}", f"--memory-swap={MEMORY_LIMIT}", f"--cpus={jobs}",
        "-e", "LC_ALL=C.UTF-8", "-e", "TZ=America/Sao_Paulo",
        "-v", f"{REPO_ROOT}:{CONTAINER_ROOT}", "-w", CONTAINER_ROOT,
        IMAGE, "bash", "-c",
        f"python3 tools/validate_desktop.py --run-seconds {run_seconds} --interval {interval}",
    ]


@dataclass(frozen=True)
class RunSummary:
    started: bool
    missing_branch_targets: int
    verdict: RunVerdict


def game_command(disc_image: Path = OWNED_DISC_IMAGE) -> list:
    """Return the game command line; the owned disc image is passed when it exists."""
    command = [str(GAME_BINARY), str(OWNED_ELF)]
    if disc_image.is_file():
        command.append(str(disc_image))
    return command


def summarize_log(log: str) -> RunSummary:
    return RunSummary(
        started=STARTED_MARKER in log,
        missing_branch_targets=log.count(MISSING_TARGET_MARKER),
        verdict=classify_run_log(log).verdict,
    )


def passed(summary: RunSummary, still_running: bool, frames: FrameSeries) -> bool:
    return (summary.started and still_running and summary.verdict is RunVerdict.PROGRESSING
            and frames.real_frames >= 2 and frames.changed)


def capture_screenshot(name: str, display: str = DISPLAY) -> Path:
    from datetime import datetime
    import re
    import shutil

    p = Path(name)
    stem = p.stem
    ext = p.suffix or ".png"

    # Add date fingerprint if not already present
    if not re.search(r"-\d{8}-\d{6}(?:-\d+)?$", stem):
        now = datetime.now()
        ts = now.strftime("%Y%m%d-%H%M%S")
        target_name = f"{stem}-{ts}{ext}"
        destination = SCREENSHOT_DIR / p.parent / target_name
        if destination.exists():
            ts = now.strftime("%Y%m%d-%H%M%S-%f")[:22]
            target_name = f"{stem}-{ts}{ext}"
            destination = SCREENSHOT_DIR / p.parent / target_name
    else:
        destination = SCREENSHOT_DIR / p.parent / f"{stem}{ext}"

    destination.parent.mkdir(parents=True, exist_ok=True)
    xwd = subprocess.run(["xwd", "-root", "-display", display], capture_output=True, check=True)
    subprocess.run(["convert", "xwd:-", str(destination)], input=xwd.stdout, capture_output=True, check=True)

    # Maintain canonical symlink (or copy) to latest un-fingerprinted filename for convenience
    canonical_stem = re.sub(r"-\d{8}-\d{6}(?:-\d+)?$", "", stem)
    canonical = SCREENSHOT_DIR / p.parent / f"{canonical_stem}{ext}"
    try:
        if canonical.is_symlink() or canonical.exists():
            canonical.unlink(missing_ok=True)
        canonical.symlink_to(destination.name)
    except OSError:
        try:
            shutil.copyfile(destination, canonical)
        except Exception:
            pass

    return destination


def stop_process(process: subprocess.Popen) -> None:
    """Terminate and reap a child, escalating to kill with bounded waits."""
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def run_game(run_seconds: int, interval: int) -> tuple:
    """Run the game, capturing screenshots; return (still running at the end, screenshot paths)."""
    environment = {**os.environ, "DISPLAY": DISPLAY}
    log_path = EVIDENCE_DIR / "run.log"
    shots = []
    with log_path.open("w") as log:
        process = subprocess.Popen(game_command(), stdout=log, stderr=subprocess.STDOUT, env=environment)
        try:
            for elapsed in range(interval, run_seconds + 1, interval):
                time.sleep(interval)
                shots.append(str(capture_screenshot(f"desktop-validation-{elapsed:03d}s.png")))
                if process.poll() is not None:
                    return False, shots
        finally:
            stop_process(process)
    return True, shots


def check_preflight() -> None:
    for tool in ("Xvfb", "xwd", "convert", "magick"):
        if shutil.which(tool) is None:
            raise FileNotFoundError(f"{tool} is required")
    for required in (GAME_BINARY, OWNED_ELF):
        if not required.exists():
            raise FileNotFoundError(f"missing {required} (diagnostic build: tools/docker_build.py --platform desktop --diagnostics)")


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the desktop diagnostic build on a virtual display and validate it.")
    parser.add_argument("--run-seconds", type=int, default=90)
    parser.add_argument("--interval", type=int, default=15)
    parser.add_argument("--docker", action="store_true", help="run validation inside aoe2-build-desktop container")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS, help=f"CPU quota for container (default: {DEFAULT_JOBS})")
    parser.add_argument("--dry-run", action="store_true", help="print docker command without running")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    if args.docker:
        cmd = container_command(args.run_seconds, args.interval, jobs=args.jobs)
        if args.dry_run:
            print("+ " + shlex.join(cmd))
            return EXIT_PASS
        ensure_container_clean(CONTAINER_NAME)
        return subprocess.run(cmd).returncode
    try:
        check_preflight()
    except FileNotFoundError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
    display = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1280x720x24", "-nolisten", "tcp"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(2)
        still_running, shots = run_game(args.run_seconds, args.interval)
    finally:
        stop_process(display)
    summary = summarize_log((EVIDENCE_DIR / "run.log").read_text(errors="replace"))
    frames = analyze_frames(shots, run_command)
    verdict = passed(summary, still_running, frames)
    print(f"Desktop validation: {'PASS' if verdict else 'FAIL'} (started={summary.started}, "
          f"running_at_end={still_running}, guest={summary.verdict.value}, "
          f"real_frames={frames.real_frames}, frames_changed={frames.changed}, "
          f"missing_branch_targets={summary.missing_branch_targets})")
    return EXIT_PASS if verdict else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
