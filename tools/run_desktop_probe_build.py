#!/usr/bin/env python3
"""Idempotent, resource-guarded runner for the desktop probe ninja build.

Runs `ninja` for build/desktop-probe inside a systemd user scope with a hard
memory cap and no swap, so an out-of-memory event kills only the build (not the
desktop session). A watchdog also stops the build when system-wide available
memory drops below a floor. Safe to re-run: ninja resumes incrementally, a lock
prevents concurrent runs, and only ignored files under build/ are written.

Exit codes: 0 build ok, 2 preflight failed, 3 memory guard aborted the build,
4 build failed (classifier report written), 5 classifier failed.
"""
from __future__ import annotations

import argparse
import fcntl
import os
import re
import subprocess
import sys
import threading
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build" / "desktop-probe"
LOG_PATH = BUILD_DIR / "raw_compile_j3.log"
REPORT_PATH = BUILD_DIR / "classified_report_j3.json"
LOCK_PATH = BUILD_DIR / ".probe_build.lock"
PROBE_BINARY = BUILD_DIR / "desktop_runtime_probe"
UNIT_NAME = "aoe2-probe-build.scope"
GIB = 1024**3
PROGRESS_PATTERN = re.compile(r"^\[(\d+)/(\d+)\]")
PROGRESS_PRINT_EVERY = 100
WATCHDOG_POLL_SECONDS = 2.0
RELEVANT_PROCESS_NAMES = ("ninja", "cc1plus")

EXIT_OK, EXIT_PREFLIGHT, EXIT_MEMORY_GUARD, EXIT_BUILD_FAILED, EXIT_CLASSIFIER = 0, 2, 3, 4, 5


class PreflightError(RuntimeError):
    """Raised when a prerequisite for a safe build is missing."""


def read_available_bytes() -> int:
    """Return MemAvailable from /proc/meminfo in bytes."""
    for line in Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) * 1024
    raise PreflightError("MemAvailable not found in /proc/meminfo")


def running_build_processes() -> list[str]:
    """Return names of ninja/cc1plus processes already running."""
    found = []
    for name in RELEVANT_PROCESS_NAMES:
        result = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True)
        found.extend([name] * len(result.stdout.split()))
    return found


def build_command(args: argparse.Namespace) -> list[str]:
    """Build the full guarded command line (systemd scope + nice + ninja)."""
    limits = [
        f"MemoryHigh={args.memory_high_gib}G",
        f"MemoryMax={args.memory_max_gib}G",
        "MemorySwapMax=0",
    ]
    scope = ["systemd-run", "--user", "--scope", "--quiet", "--collect", f"--unit={UNIT_NAME}"]
    for limit in limits:
        scope += ["-p", limit]
    jobs = str(args.jobs)
    ninja = ["ninja", "-C", str(BUILD_DIR), f"-j{jobs}", f"-l{jobs}"]
    return scope + ["--", "nice", "-n", "10", "ionice", "-c3"] + ninja


def build_environment() -> dict:
    """Return the build environment with the C locale, so diagnostics match the classifier's English patterns."""
    return {**os.environ, "LC_ALL": "C"}


def check_preflight(args: argparse.Namespace) -> None:
    """Fail fast before any heavy work; raises PreflightError."""
    for tool in ("systemd-run", "ninja", "pgrep", "nice", "ionice"):
        if subprocess.run(["which", tool], capture_output=True).returncode != 0:
            raise PreflightError(f"required tool not found: {tool}")
    if not (BUILD_DIR / "build.ninja").is_file():
        raise PreflightError(f"{BUILD_DIR}/build.ninja missing: configure the probe with cmake first")
    if running_build_processes():
        raise PreflightError("ninja/cc1plus already running; stop them before starting")
    available_gib = read_available_bytes() / GIB
    if available_gib < args.min_available_gib:
        raise PreflightError(f"only {available_gib:.1f} GiB available, need {args.min_available_gib}")


def describe_pending_steps() -> str:
    """Summarize a ninja dry run; the true total is unknown while CMake must regenerate."""
    result = subprocess.run(["ninja", "-C", str(BUILD_DIR), "-n"], capture_output=True, text=True)
    if "Re-running CMake" in result.stdout:
        return "unknown (CMake will regenerate the build graph first)"
    steps = sum(1 for line in result.stdout.splitlines() if PROGRESS_PATTERN.match(line))
    return str(steps)


class MemoryWatchdog(threading.Thread):
    """Stops the build scope when available system memory falls under a floor."""

    def __init__(self, floor_bytes: int):
        super().__init__(daemon=True)
        self._floor_bytes = floor_bytes
        self._halt = threading.Event()
        self.tripped = False

    def run(self) -> None:
        while not self._halt.wait(WATCHDOG_POLL_SECONDS):
            if read_available_bytes() < self._floor_bytes:
                self.tripped = True
                subprocess.run(["systemctl", "--user", "stop", UNIT_NAME], capture_output=True)
                return

    def stop(self) -> None:
        self._halt.set()


def stream_build(command: list[str]) -> tuple[int, str]:
    """Run the build, tee full output to the ignored log, print a compact digest."""
    last_progress = ""
    with LOG_PATH.open("w", buffering=1) as log, subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
        env=build_environment(),
    ) as process:
        for line in process.stdout:
            log.write(line)
            last_progress = digest_line(line, last_progress)
        return process.wait(), last_progress


def digest_line(line: str, last_progress: str) -> str:
    """Print progress every N steps and any failure line; return latest progress."""
    match = PROGRESS_PATTERN.match(line)
    if match and int(match.group(1)) % PROGRESS_PRINT_EVERY == 0:
        print(line.rstrip(), flush=True)
    elif not match and ("FAILED:" in line or "error:" in line):
        print(line.rstrip()[:300], flush=True)
    return line.rstrip() if match else last_progress


def run_classifier() -> int:
    """Classify the raw log into a sanitized report; return its exit code."""
    classifier = REPO_ROOT / "tools" / "classify_diagnostics.py"
    command = [sys.executable, str(classifier), "--log", str(LOG_PATH), "--out", str(REPORT_PATH)]
    return subprocess.run(command).returncode


def execute(args: argparse.Namespace) -> int:
    """Run the guarded build and map the outcome to an exit code."""
    watchdog = MemoryWatchdog(int(args.watchdog_floor_gib * GIB))
    watchdog.start()
    returncode, last_progress = stream_build(build_command(args))
    watchdog.stop()
    print(f"ninja exit={returncode}; last progress: {last_progress or 'n/a'}")
    if watchdog.tripped:
        print("MEMORY GUARD: build stopped to protect the system. Re-run with --jobs 2.")
        return EXIT_MEMORY_GUARD
    if returncode == 0:
        print(f"BUILD OK; probe binary present: {PROBE_BINARY.is_file()}")
        return EXIT_OK
    return EXIT_BUILD_FAILED if run_classifier() == 0 else EXIT_CLASSIFIER


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--memory-high-gib", type=int, default=15, help="throttle point for the build cgroup")
    parser.add_argument("--memory-max-gib", type=int, default=18, help="hard cap; OOM kills only the build")
    parser.add_argument("--min-available-gib", type=float, default=12.0, help="required free memory to start")
    parser.add_argument("--watchdog-floor-gib", type=float, default=3.0, help="abort if system memory drops below")
    parser.add_argument("--dry-run", action="store_true", help="run preflight and print the command only")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_preflight(args)
    except PreflightError as error:
        print(f"PREFLIGHT FAILED: {error}", file=sys.stderr)
        return EXIT_PREFLIGHT
    print(f"pending ninja steps: {describe_pending_steps()}")
    if args.dry_run:
        print("DRY RUN command: " + " ".join(build_command(args)))
        return EXIT_OK
    with LOCK_PATH.open("w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print("PREFLIGHT FAILED: another run holds the lock", file=sys.stderr)
            return EXIT_PREFLIGHT
        return execute(args)


if __name__ == "__main__":
    sys.exit(main())
