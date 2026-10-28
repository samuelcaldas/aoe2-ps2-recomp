#!/usr/bin/env python3
"""Validate the Nintendo Switch build in a headless emulator inside Docker.

Runs the Switch executable (build/docker-switch/aoe2_game.nro) inside the
aoe2-switch-emulator container under Xvfb using Ryujinx.Headless.SDL2.
The emulator captures real-time debug console output (svcOutputDebugString and
libnx logging) into build/switch-validation/run.log, takes timed screenshots
into build/switch-validation/screen-NNN.png, and checks process liveness
in build/switch-validation/alive.txt.

The host judges that evidence with the same rules as the desktop and Android
validators:
- the process is alive;
- the log shows the guest progressing (classify_diagnostics: PROGRESSING);
- at least two screenshots are real frames (not fallback) and they differ.

Exit codes: 0 pass, 1 fail, 2 preflight failed.
"""
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.classify_diagnostics import RunVerdict, classify_run_log  # noqa: E402
from tools.frame_analysis import analyze_frames, run_command  # noqa: E402

CONTAINER_ROOT = "/src"
IMAGE = "aoe2-switch-emulator"
DOCKER_CONTEXT = "docker-dev"
NRO_PATH = "build/docker-switch/aoe2_game.nro"
PROD_KEYS_DEFAULT = "game/switch/prod.keys"
EVIDENCE_DIR = "build/switch-validation"
SCREENSHOT_INTERVAL = 15
DISPLAY = ":99"
MEMORY_LIMIT = "12g"
EXIT_PASS, EXIT_FAIL, EXIT_PREFLIGHT = 0, 1, 2


class PreflightError(RuntimeError):
    """Raised when a prerequisite for the Switch validation is missing."""


def emulator_script(run_seconds: int, interval: int = SCREENSHOT_INTERVAL, nro: str = NRO_PATH) -> str:
    """Shell script run inside the container; writes raw evidence for the host to judge."""
    evidence = f"{CONTAINER_ROOT}/{EVIDENCE_DIR}"
    shots = max(1, run_seconds // interval)
    config_dir = f"{evidence}/ryujinx-data"
    system_dir = f"{config_dir}/system"

    return "\n".join([
        "set -euo pipefail",
        f"mkdir -p {evidence} {system_dir}",
        f"if [ -f {CONTAINER_ROOT}/{PROD_KEYS_DEFAULT} ]; then cp -f {CONTAINER_ROOT}/{PROD_KEYS_DEFAULT} {system_dir}/prod.keys; fi",
        f"Xvfb {DISPLAY} -screen 0 1280x720x24 > /dev/null 2>&1 &",
        "XVFB_PID=$!",
        "sleep 2",
        f"export DISPLAY={DISPLAY}",
        "RYUJINX_BIN=$(which Ryujinx.Headless.SDL2 2>/dev/null || which Ryujinx 2>/dev/null || true)",
        "if [ -z \"$RYUJINX_BIN\" ]; then echo 'FATAL: Ryujinx executable not found in PATH' >&2; kill $XVFB_PID; exit 1; fi",
        f"\"$RYUJINX_BIN\" --root-data-dir {config_dir} {CONTAINER_ROOT}/{nro} > {evidence}/run.log 2>&1 &",
        "EMU_PID=$!",
        f"for shot in $(seq 1 {shots}); do sleep {interval}; "
        f"xwd -root -display {DISPLAY} 2>/dev/null | convert xwd:- {evidence}/screen-$(printf '%03d' \"$shot\").png 2>/dev/null || true; done",
        f"if kill -0 $EMU_PID 2>/dev/null; then echo yes > {evidence}/alive.txt; "
        f"else echo no > {evidence}/alive.txt; fi",
        "kill $EMU_PID 2>/dev/null || true",
        "kill $XVFB_PID 2>/dev/null || true",
    ]) + "\n"


def evidence_passed(alive: bool, log: str, screenshots: list, executor=run_command) -> bool:
    """Same verdict rules as the desktop and Android validators: alive, progressing guest, changing real frames."""
    progressing = classify_run_log(log).verdict is RunVerdict.PROGRESSING
    frames = analyze_frames(screenshots, executor)
    return alive and progressing and frames.real_frames >= 2 and frames.changed


def container_command(run_seconds: int, jobs: int = 4, nro: str = NRO_PATH) -> list:
    return [
        "docker", "--context", DOCKER_CONTEXT, "run", "--rm",
        f"--memory={MEMORY_LIMIT}", f"--memory-swap={MEMORY_LIMIT}", f"--cpus={jobs}",
        "-e", "LC_ALL=C.UTF-8", "-e", "TZ=America/Sao_Paulo",
        "-v", f"{REPO_ROOT}:{CONTAINER_ROOT}", "-w", CONTAINER_ROOT,
        IMAGE, "bash", "-c", emulator_script(run_seconds, nro=nro),
    ]


def check_preflight(keys_path: Path = REPO_ROOT / PROD_KEYS_DEFAULT) -> None:
    if shutil.which("docker") is None:
        raise PreflightError("docker CLI not found")
    contexts = subprocess.run(["docker", "context", "ls", "--format", "{{.Name}}"], capture_output=True, text=True)
    if DOCKER_CONTEXT not in contexts.stdout.split():
        raise PreflightError(f"docker context '{DOCKER_CONTEXT}' not found")
    nro = REPO_ROOT / NRO_PATH
    if not nro.exists():
        raise PreflightError(f"missing Switch binary {nro.relative_to(REPO_ROOT)}; run python3 tools/docker_build.py --platform switch")
    if keys_path is not None and not keys_path.exists():
        print(f"INFO: Switch prod.keys not found at {keys_path.relative_to(REPO_ROOT) if keys_path.is_relative_to(REPO_ROOT) else keys_path}; proceeding with homebrew NRO without keys", flush=True)


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--run-seconds", type=int, default=60, help="how long to run the emulator under test (seconds)")
    parser.add_argument("--jobs", type=int, default=4, help="CPU quota allocated to emulator container")
    parser.add_argument("--keys", type=Path, default=REPO_ROOT / PROD_KEYS_DEFAULT, help="path to Switch prod.keys")
    parser.add_argument("--dry-run", action="store_true", help="print commands without executing them")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)

    try:
        check_preflight(args.keys)
    except PreflightError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT

    cmd = container_command(args.run_seconds, jobs=args.jobs)
    print("+ " + shlex.join(cmd[:10]) + " ...", flush=True)
    if args.dry_run:
        return EXIT_PASS

    result = subprocess.run(cmd)
    if result.returncode != 0:
        return result.returncode

    evidence_path = REPO_ROOT / EVIDENCE_DIR
    alive_file = evidence_path / "alive.txt"
    log_file = evidence_path / "run.log"
    alive = alive_file.read_text().strip() == "yes" if alive_file.exists() else False
    log = log_file.read_text(errors="replace") if log_file.exists() else ""
    screenshots = sorted(str(p) for p in evidence_path.glob("screen-*.png"))

    pass_verdict = evidence_passed(alive, log, screenshots)
    verdict_text = "PASS\n" if pass_verdict else "FAIL\n"
    (evidence_path / "result.txt").write_text(verdict_text)
    print(f"Switch validation verdict: {verdict_text.strip()}", flush=True)

    return EXIT_PASS if pass_verdict else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
