#!/usr/bin/env python3
"""Validate the Android build in a headless x86_64 emulator inside Docker.

Boots an API 34 x86_64 emulator (KVM-accelerated) in the aoe2-build-android
image, installs build/docker-android/aoe2-recomp-debug.apk, pushes the owned
game files (ELF, extracted disc, KCET hooks file) into the app's external files
directory, launches the NativeActivity, and takes a screenshot every interval.
The container collects build/android-validation/{logcat.txt,screen-NNN.png,alive.txt}.
The host then judges that evidence with the same rules as the desktop validator
(tools/validate_desktop.py): the process is alive, the log shows PROGRESSING,
and at least two screenshots are real frames that differ. The verdict is
written to build/android-validation/result.txt.

Exit codes: 0 pass, 1 fail, 2 preflight failed.
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

from tools.classify_diagnostics import RunVerdict, classify_run_log  # noqa: E402
from tools.frame_analysis import analyze_frames, run_command  # noqa: E402

CONTAINER_ROOT = "/src"
IMAGE = "aoe2-build-android"
DOCKER_CONTEXT = "docker-dev"
PACKAGE = "com.aoe2.recomp"
DEVICE_GAME_DIR = f"/storage/emulated/0/Android/data/{PACKAGE}/files"
APK = "build/docker-android/aoe2-recomp-debug.apk"
GAME_DIR = "game/SLES-50282"
EVIDENCE_DIR = "build/android-validation"
SYSTEM_IMAGE = "system-images;android-34;default;x86_64"
SCREENSHOT_INTERVAL = 15
CONTAINER_NAME = "aoe2-validate-android"
DEFAULT_JOBS = 8
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


def emulator_script(run_seconds: int, interval: int = SCREENSHOT_INTERVAL) -> str:
    """Shell script run inside the container; writes raw evidence for the host to judge."""
    evidence = f"{CONTAINER_ROOT}/{EVIDENCE_DIR}"
    shots = max(1, run_seconds // interval)
    return "\n".join([
        "set -euo pipefail",
        f"mkdir -p {evidence}",
        f"echo no | avdmanager create avd -f -n aoe2 -k {shlex.quote(SYSTEM_IMAGE)} > /dev/null",
        "emulator -avd aoe2 -no-window -no-audio -no-boot-anim -no-snapshot -no-metrics -gpu swiftshader_indirect "
        "-memory 4096 -cores 4 > /tmp/emulator.log 2>&1 &",
        "adb wait-for-device",
        "until [ \"$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\\r')\" = 1 ]; do sleep 2; done",
        "adb root || true",
        "adb wait-for-device",
        f"adb install -r {CONTAINER_ROOT}/{APK}",
        f"adb shell mkdir -p {DEVICE_GAME_DIR}",
        f"adb push {CONTAINER_ROOT}/{GAME_DIR}/. {DEVICE_GAME_DIR}/",
        f"adb shell ls {DEVICE_GAME_DIR}/kcet_hooks.ini",
        "adb logcat -c",
        f"adb shell am start -n {PACKAGE}/android.app.NativeActivity",
        f"for shot in $(seq 1 {shots}); do sleep {interval}; "
        f"adb exec-out screencap -p > {evidence}/screen-$(printf '%03d' \"$shot\").png; done",
        f"adb logcat -d > {evidence}/logcat.txt",
        f"if adb shell pidof {PACKAGE} > /dev/null; then echo yes > {evidence}/alive.txt; "
        f"else echo no > {evidence}/alive.txt; fi",
    ]) + "\n"


def evidence_passed(alive: bool, logcat: str, screenshots: list, executor=run_command) -> bool:
    """Same verdict rules as the desktop validator: alive, progressing guest, changing real frames."""
    progressing = classify_run_log(logcat).verdict is RunVerdict.PROGRESSING
    frames = analyze_frames(screenshots, executor)
    return alive and progressing and frames.real_frames >= 2 and frames.changed


def container_command(run_seconds: int, jobs: int = DEFAULT_JOBS) -> list:
    return [
        "docker", "--context", DOCKER_CONTEXT, "run", "--rm",
        f"--name={CONTAINER_NAME}",
        f"--hostname={CONTAINER_NAME}",
        "--label=aoe2.platform=android",
        "--label=aoe2.role=validate",
        f"--label=aoe2.container={CONTAINER_NAME}",
        "--device", "/dev/kvm", "--group-add", str(os.stat("/dev/kvm").st_gid),
        "--memory=10g", f"--cpus={jobs}",
        "-v", f"{REPO_ROOT}:{CONTAINER_ROOT}", "-w", CONTAINER_ROOT,
        IMAGE, "bash", "-c", emulator_script(run_seconds),
    ]


def check_preflight() -> None:
    for required in (REPO_ROOT / APK, REPO_ROOT / GAME_DIR / "AOK.ELF", REPO_ROOT / GAME_DIR / "kcet_hooks.ini"):
        if not required.exists():
            raise FileNotFoundError(f"missing {required.relative_to(REPO_ROOT)}")
    if not Path("/dev/kvm").exists():
        raise FileNotFoundError("/dev/kvm is not available for the emulator")


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate the Android APK in a headless emulator.")
    parser.add_argument("--run-seconds", type=int, default=90)
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS, help="CPU quota allocated to emulator container (default: 8)")
    parser.add_argument("--dry-run", action="store_true", help="print container command without running")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_preflight()
    except FileNotFoundError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT
    cmd = container_command(args.run_seconds, jobs=args.jobs)
    if args.dry_run:
        print("+ " + shlex.join(cmd))
        return EXIT_PASS
    ensure_container_clean(CONTAINER_NAME)
    subprocess.run(cmd)
    evidence = REPO_ROOT / EVIDENCE_DIR
    alive_file = evidence / "alive.txt"
    logcat_file = evidence / "logcat.txt"
    alive = alive_file.is_file() and alive_file.read_text().strip() == "yes"
    logcat = logcat_file.read_text(errors="replace") if logcat_file.is_file() else ""
    screenshots = sorted(str(path) for path in evidence.glob("screen-*.png"))
    passed = evidence_passed(alive, logcat, screenshots)
    (evidence / "result.txt").write_text("PASS\n" if passed else "FAIL\n", encoding="utf-8")
    print(f"Android validation: {'PASS' if passed else 'FAIL'} (evidence in {EVIDENCE_DIR}/)")
    return EXIT_PASS if passed else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
