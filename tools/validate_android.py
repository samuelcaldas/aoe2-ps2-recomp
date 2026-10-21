#!/usr/bin/env python3
"""Validate the Android build in a headless x86_64 emulator inside Docker.

Boots an API 34 x86_64 emulator (KVM-accelerated) in the aoe2-build-android
image, installs build/docker-android/aoe2-recomp-debug.apk, pushes the owned
game files (ELF, extracted disc, KCET hooks file) into the app's external files
directory, launches the NativeActivity, and after a fixed run collects:
build/android-validation/{logcat.txt,screen.png,result.txt}. The result is
PASS when the app process is still alive at the end of the run.

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
CONTAINER_ROOT = "/src"
IMAGE = "aoe2-build-android"
DOCKER_CONTEXT = "docker-dev"
PACKAGE = "com.aoe2.recomp"
DEVICE_GAME_DIR = f"/storage/emulated/0/Android/data/{PACKAGE}/files"
APK = "build/docker-android/aoe2-recomp-debug.apk"
GAME_DIR = "game/SLES-50282"
EVIDENCE_DIR = "build/android-validation"
SYSTEM_IMAGE = "system-images;android-34;default;x86_64"
EXIT_PASS, EXIT_FAIL, EXIT_PREFLIGHT = 0, 1, 2


def emulator_script(run_seconds: int) -> str:
    """Shell script run inside the container; writes evidence and prints PASS/FAIL."""
    evidence = f"{CONTAINER_ROOT}/{EVIDENCE_DIR}"
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
        f"sleep {run_seconds}",
        f"adb exec-out screencap -p > {evidence}/screen.png",
        f"adb logcat -d > {evidence}/logcat.txt",
        f"if adb shell pidof {PACKAGE} > /dev/null; then echo PASS > {evidence}/result.txt; "
        f"else echo FAIL > {evidence}/result.txt; fi",
        f"cat {evidence}/result.txt",
    ]) + "\n"


def container_command(run_seconds: int) -> list:
    return [
        "docker", "--context", DOCKER_CONTEXT, "run", "--rm",
        "--device", "/dev/kvm", "--group-add", str(os.stat("/dev/kvm").st_gid),
        "--memory=10g", "--cpus=6",
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
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_preflight()
    except FileNotFoundError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT
    subprocess.run(container_command(args.run_seconds))
    result_file = REPO_ROOT / EVIDENCE_DIR / "result.txt"
    passed = result_file.is_file() and result_file.read_text().strip() == "PASS"
    print(f"Android validation: {'PASS' if passed else 'FAIL'} (evidence in {EVIDENCE_DIR}/)")
    return EXIT_PASS if passed else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
