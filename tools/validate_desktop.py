#!/usr/bin/env python3
"""Validate the desktop build by running the game on a virtual display.

Runs build/docker-desktop/aoe2_game on the owned ELF under Xvfb, captures
screenshots every interval into .playwright-mcp/ (git-ignored), and writes the
full log to build/desktop-validation/run.log. PASS means guest execution
started and the process was still running at the end of the run. Counts of
unresolved guest branches are reported as the next things to fix.

Exit codes: 0 pass, 1 fail, 2 preflight failed.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_BINARY = REPO_ROOT / "build" / "docker-desktop" / "aoe2_game"
OWNED_ELF = REPO_ROOT / "game" / "SLES-50282" / "AOK.ELF"
EVIDENCE_DIR = REPO_ROOT / "build" / "desktop-validation"
SCREENSHOT_DIR = REPO_ROOT / ".playwright-mcp"
DISPLAY = ":99"
STARTED_MARKER = "Starting execution"
MISSING_TARGET_MARKER = "[guest-branch:missing-target]"
EXIT_PASS, EXIT_FAIL, EXIT_PREFLIGHT = 0, 1, 2


@dataclass(frozen=True)
class RunSummary:
    started: bool
    missing_branch_targets: int


def game_command() -> list:
    return [str(GAME_BINARY), str(OWNED_ELF)]


def summarize_log(log: str) -> RunSummary:
    return RunSummary(STARTED_MARKER in log, log.count(MISSING_TARGET_MARKER))


def passed(summary: RunSummary, still_running: bool) -> bool:
    return summary.started and still_running


def capture_screenshot(name: str) -> None:
    xwd = subprocess.run(["xwd", "-root", "-display", DISPLAY], capture_output=True)
    subprocess.run(["convert", "xwd:-", str(SCREENSHOT_DIR / name)], input=xwd.stdout, capture_output=True)


def run_game(run_seconds: int, interval: int) -> bool:
    """Run the game, capturing screenshots; return whether it was still running at the end."""
    environment = {**os.environ, "DISPLAY": DISPLAY}
    log_path = EVIDENCE_DIR / "run.log"
    with log_path.open("w") as log:
        process = subprocess.Popen(game_command(), stdout=log, stderr=subprocess.STDOUT, env=environment)
        for elapsed in range(interval, run_seconds + 1, interval):
            time.sleep(interval)
            capture_screenshot(f"desktop-validation-{elapsed:03d}s.png")
            if process.poll() is not None:
                return False
        process.terminate()
        process.wait(timeout=10)
    return True


def check_preflight() -> None:
    for tool in ("Xvfb", "xwd", "convert"):
        if shutil.which(tool) is None:
            raise FileNotFoundError(f"{tool} is required")
    for required in (GAME_BINARY, OWNED_ELF):
        if not required.exists():
            raise FileNotFoundError(f"missing {required}")


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run the desktop build on a virtual display and validate it.")
    parser.add_argument("--run-seconds", type=int, default=90)
    parser.add_argument("--interval", type=int, default=15)
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_preflight()
    except FileNotFoundError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    SCREENSHOT_DIR.mkdir(exist_ok=True)
    display = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1280x720x24", "-nolisten", "tcp"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(2)
        still_running = run_game(args.run_seconds, args.interval)
    finally:
        display.terminate()
    summary = summarize_log((EVIDENCE_DIR / "run.log").read_text(errors="replace"))
    verdict = passed(summary, still_running)
    print(f"Desktop validation: {'PASS' if verdict else 'FAIL'} (started={summary.started}, "
          f"running_at_end={still_running}, missing_branch_targets={summary.missing_branch_targets})")
    return EXIT_PASS if verdict else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
