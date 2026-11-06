#!/usr/bin/env python3
"""Run the Android port inside an emulator in Docker with noVNC.

Exposes port 6082 for web browser viewing (Google Chrome) and port 5902 for VNC.
"""

from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools import validate_android as validation  # noqa: E402

CONTAINER_ROOT = "/src"
IMAGE = "aoe2-build-android"
DOCKER_CONTEXT = "docker-dev"
DEFAULT_APK = validation.default_apk_path()
EVIDENCE_DIR = "build/android-validation"
SCREENSHOT_DIR = ".playwright-mcp/android"
SYSTEM_IMAGE = "system-images;android-34;default;x86_64"
DISPLAY = ":99"
MEMORY_LIMIT = "12g"
CONTAINER_NAME = "aoe2-view-android"
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


def novnc_runner_script(apk: str = DEFAULT_APK) -> str:
    evidence = f"{CONTAINER_ROOT}/{EVIDENCE_DIR}"
    shots_dir = f"{CONTAINER_ROOT}/{SCREENSHOT_DIR}"
    novnc_webroot = f"{evidence}/novnc-webroot"

    return (
        "\n".join(
            [
                "set -euo pipefail",
                f"mkdir -p {evidence} {shots_dir} {novnc_webroot}",
                f"echo no | avdmanager create avd -f -n aoe2 -k {shlex.quote(SYSTEM_IMAGE)} > /dev/null",
                f"Xvfb {DISPLAY} -screen 0 1280x760x24 > /dev/null 2>&1 &",
                "sleep 1",
                f"export DISPLAY={DISPLAY}",
                "emulator -avd aoe2 -gpu swiftshader_indirect -memory 4096 -cores 4 -skin 1280x720 "
                f"> {evidence}/emulator.log 2>&1 &",
                "adb wait-for-device",
                "until [ \"$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\\r')\" = 1 ]; do sleep 2; done",
                "adb root || true",
                "adb wait-for-device",
                f"adb install -r {CONTAINER_ROOT}/{apk}",
                f"adb shell mkdir -p {validation.DEVICE_GAME_DIR}",
                f"for item in {CONTAINER_ROOT}/{validation.GAME_DIR}/*; do "
                f'[ "$(basename "$item")" = aoe2.iso ] || adb push "$item" {validation.DEVICE_GAME_DIR}/; done',
                f"adb shell ls {validation.DEVICE_GAME_DIR}/kcet_hooks.ini",
                f"x11vnc -display {DISPLAY} -nopw -forever -shared -rfbport 5900 > /dev/null 2>&1 &",
                f"cp -R /usr/share/novnc/. {novnc_webroot}/",
                f"mkdir -p {novnc_webroot}/mobile",
                f"cp -f {CONTAINER_ROOT}/tools/novnc_mobile/index.html {CONTAINER_ROOT}/tools/novnc_mobile/mobile.css {CONTAINER_ROOT}/tools/novnc_mobile/mobile.js {novnc_webroot}/mobile/",
                f"websockify --web {novnc_webroot} 6082 localhost:5900 > /dev/null 2>&1 &",
                "echo '================================================================='",
                "echo 'Android View noVNC: http://localhost:6082/vnc.html'",
                "echo 'Mobile View noVNC:  http://localhost:6082/mobile/'",
                "echo '================================================================='",
                "adb logcat -c",
                f"adb shell am start -n {validation.PACKAGE}/android.app.NativeActivity",
                f"adb logcat 2>&1 | tee {evidence}/manual-run.log",
            ]
        )
        + "\n"
    )


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--port",
        type=int,
        default=6082,
        help="host HTTP/web port for noVNC (default: 6082)",
    )
    parser.add_argument(
        "--vnc-port", type=int, default=5902, help="host VNC port (default: 5902)"
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=DEFAULT_JOBS,
        help=f"CPU quota (default: {DEFAULT_JOBS})",
    )
    parser.add_argument(
        "--apk",
        help="relative path to APK (default: release if present, else debug)",
    )
    parser.add_argument(
        "--container-name",
        default=CONTAINER_NAME,
        help=f"Docker container name (default: {CONTAINER_NAME})",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print commands without executing them"
    )
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)

    apk = args.apk or validation.default_apk_path()
    try:
        validation.check_preflight(apk)
    except FileNotFoundError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return validation.EXIT_PREFLIGHT

    group_gid = str(os.stat("/dev/kvm").st_gid) if Path("/dev/kvm").exists() else "0"
    tty_flags = ["-it"] if sys.stdin.isatty() else ["-i"]
    cmd = [
        "docker",
        "--context",
        DOCKER_CONTEXT,
        "run",
        "--rm",
        *tty_flags,
        f"--name={args.container_name}",
        f"--hostname={args.container_name}",
        "--label=aoe2.platform=android",
        "--label=aoe2.role=view",
        f"--label=aoe2.container={args.container_name}",
        "--device",
        "/dev/kvm",
        "--group-add",
        group_gid,
        f"--memory={MEMORY_LIMIT}",
        f"--memory-swap={MEMORY_LIMIT}",
        f"--cpus={args.jobs}",
        "-p",
        f"{args.port}:6082",
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
        novnc_runner_script(apk),
    ]

    print("Starting Android Emulator with noVNC...")
    print(f"👉 Chrome URL: http://localhost:{args.port}/vnc.html")
    if args.dry_run:
        print("+ " + shlex.join(cmd))
        return 0
    ensure_container_clean(args.container_name)
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
