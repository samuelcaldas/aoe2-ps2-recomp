#!/usr/bin/env python3
"""Run the Nintendo Switch port in Ryujinx headless inside Docker with noVNC.

Exposes port 6080 for web browser viewing (Google Chrome) and port 5900 for VNC.
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

from tools import validate_switch as validation  # noqa: E402

CONTAINER_ROOT = "/src"
IMAGE = "aoe2-switch-emulator:latest"
DOCKER_CONTEXT = "docker-dev"
NRO_PATH = "build/docker-switch/aoe2_game.nro"
EVIDENCE_DIR = "build/switch-validation"
DISPLAY = ":99"
MEMORY_LIMIT = "12g"
CONTAINER_NAME = "aoe2-view-switch"
DEFAULT_JOBS = 8


def ensure_container_clean(name: str) -> None:
    """Ensure no stale stopped container with `name` blocks docker run --name."""
    inspect = subprocess.run(
        [
            "docker",
            "--context",
            DOCKER_CONTEXT,
            "container",
            "inspect",
            "-f",
            "{{.State.Running}}",
            name,
        ],
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


def novnc_runner_script(nro: str = NRO_PATH) -> str:
    evidence = f"{CONTAINER_ROOT}/{EVIDENCE_DIR}"
    config_dir = f"{evidence}/ryujinx-data"
    system_dir = f"{config_dir}/system"
    sdcard_dir = f"{config_dir}/sdcard/switch/aoe2"

    return (
        "\n".join(
            [
                "set -euo pipefail",
                f"mkdir -p {evidence} {system_dir}",
                f"if [ -f {CONTAINER_ROOT}/{validation.PROD_KEYS_DEFAULT} ]; then cp -f {CONTAINER_ROOT}/{validation.PROD_KEYS_DEFAULT} {system_dir}/prod.keys; fi",
                *validation.persistent_sdcard_setup(config_dir),
                f"echo 'mode=legacy' > {sdcard_dir}/map_view.ini",
                "export AOE2_MAP_VIEW=legacy",
                "export AOE2_LOG_POINTER=1",
                f"Xvfb {DISPLAY} -screen 0 1280x760x24 > /dev/null 2>&1 &",
                "sleep 1",
                f"export DISPLAY={DISPLAY}",
                "RYUJINX_BIN=$(which Ryujinx.Headless.SDL2 2>/dev/null || which Ryujinx 2>/dev/null || true)",
                'if [ -z "$RYUJINX_BIN" ]; then echo "FATAL: Ryujinx executable not found in PATH" >&2; exit 1; fi',
                f'timeout 2 "$RYUJINX_BIN" --root-data-dir {config_dir} > /dev/null 2>&1 || true',
                *validation.ryujinx_config_setup(config_dir),
                f"x11vnc -display {DISPLAY} -nopw -forever -shared -rfbport 5900 > /dev/null 2>&1 &",
                "websockify --web /usr/share/novnc 6080 localhost:5900 > /dev/null 2>&1 &",
                "echo '================================================================='",
                "echo 'noVNC ready! Open Google Chrome at: http://localhost:6080/vnc.html'",
                "echo '================================================================='",
                f'"$RYUJINX_BIN" --root-data-dir {config_dir} {CONTAINER_ROOT}/{nro} 2>&1 | tee {evidence}/manual-run.log',
            ]
        )
        + "\n"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--port",
        type=int,
        default=6080,
        help="host HTTP/web port for noVNC (default: 6080)",
    )
    parser.add_argument(
        "--vnc-port", type=int, default=5900, help="host VNC port (default: 5900)"
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=DEFAULT_JOBS,
        help=f"CPU quota (default: {DEFAULT_JOBS})",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print commands without executing them"
    )
    args = parser.parse_args()

    nro = REPO_ROOT / NRO_PATH
    if not nro.exists():
        sys.stderr.write(
            f"Missing Switch binary: {nro}\nRun: python3 tools/docker_build.py --platform switch\n"
        )
        return 1

    cmd = [
        "docker",
        "--context",
        DOCKER_CONTEXT,
        "run",
        "--rm",
        "-it",
        f"--name={CONTAINER_NAME}",
        f"--hostname={CONTAINER_NAME}",
        "--label=aoe2.platform=switch",
        "--label=aoe2.role=view",
        f"--label=aoe2.container={CONTAINER_NAME}",
        f"--memory={MEMORY_LIMIT}",
        f"--memory-swap={MEMORY_LIMIT}",
        f"--cpus={args.jobs}",
        "-p",
        f"{args.port}:6080",
        "-p",
        f"{args.vnc_port}:5900",
        "-e",
        "LC_ALL=C.UTF-8",
        "-e",
        "TZ=America/Sao_Paulo",
        "-v",
        f"{REPO_ROOT}:{CONTAINER_ROOT}",
        "-w",
        CONTAINER_ROOT,
        IMAGE,
        "bash",
        "-c",
        novnc_runner_script(NRO_PATH),
    ]

    print("Starting Nintendo Switch Ryujinx emulator with noVNC...")
    print(f"👉 Chrome URL: http://localhost:{args.port}/vnc.html")
    if args.dry_run:
        print("+ " + shlex.join(cmd))
        return 0
    ensure_container_clean(CONTAINER_NAME)
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
