#!/usr/bin/env python3
"""Build the recompiled game for one platform inside its Docker image.

Every platform builds in a container on the `docker-dev` context, with the
repository mounted at /src and outputs under the ignored build/docker-<platform>/.
The container is capped in memory (no swap) and CPUs so the generated code
cannot exhaust the host. Images run as the host user, so build outputs stay
owned by that user. Safe to re-run: image builds are cached and CMake/Ninja
resume incrementally.

Exit codes: 0 success, 2 preflight failed, otherwise the failing docker exit code.
"""
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CONTAINER_ROOT = "/src"
DOCKER_CONTEXT = "docker-dev"
GENERATED_DIR = REPO_ROOT / "generated" / "SLES-50282"
MEMORY_LIMIT = "18g"
EXIT_PREFLIGHT = 2
DEFAULT_JOBS = 8

PLATFORM_ALIASES = {
    "pc": "desktop",
}


class PreflightError(RuntimeError):
    """Raised when a prerequisite for a containerized build is missing."""


@dataclass(frozen=True)
class PlatformSpec:
    """How one platform is built inside its image.

    `commands` are shell lines run in order inside the container; `{jobs}` and
    `{build_dir}` (absolute, inside the container) are substituted.
    """

    dockerfile: str
    image: str
    build_dir: str
    commands: tuple
    devices: tuple = ()
    test_commands: tuple = ()


def cmake_commands(*configure_args: str, targets: tuple = ("aoe2_game",)) -> tuple:
    configure = shlex.join(["cmake", "-S", CONTAINER_ROOT, "-B", "{build_dir}", "-G", "Ninja",
                            "-DPS2X_ENABLE_FUNCTION_LOG_TRACKER=OFF", *configure_args])
    build = shlex.join(["cmake", "--build", "{build_dir}", "--target", *targets, "-v"]) + " -j {jobs}"
    return (configure, build)


def hd_stage_command(platform: str) -> str:
    """Stage imported HD assets when a local catalog exists; leave PS2-only builds unchanged."""
    return (
        "if [ -f game/hd-edition/catalog.json ]; then "
        f"python3 tools/stage_hd_assets.py --platform {platform} && "
        f"python3 tools/verify_hd_stage.py --platform {platform}; "
        "else printf '%s\\n' 'HD assets not imported; skipping staging'; fi"
    )


def with_hd_stage(platform: str, commands: tuple) -> tuple:
    return (hd_stage_command(platform), *commands)


def desktop_commands(*extra_configure: str) -> tuple:
    return with_hd_stage("desktop", cmake_commands(
        "-DCMAKE_BUILD_TYPE=Debug", "-DAOE2_BUILD_GAME=ON", "-DAOE2_BUILD_DESKTOP_PROBE=ON", *extra_configure,
        targets=("aoe2_game", "desktop_runtime_probe", "aoe2_input_tests"),
    ))


# Diagnostic variants build into their own directory so the normal artifacts stay untouched.
DIAGNOSTIC_VARIANTS = {
    "desktop": PlatformSpec(
        dockerfile="docker/desktop/Dockerfile",
        image="aoe2-build-desktop",
        build_dir="build/docker-desktop-diagnostics",
        commands=desktop_commands("-DAOE2_RUNTIME_DIAGNOSTICS=ON"),
        test_commands=("ctest --test-dir {build_dir} --output-on-failure -V -R 'desktop_runtime_probe_init|input_tests'",),
    ),
}

PLATFORMS = {
    "desktop": PlatformSpec(
        dockerfile="docker/desktop/Dockerfile",
        image="aoe2-build-desktop",
        build_dir="build/docker-desktop",
        commands=desktop_commands(),
        test_commands=("ctest --test-dir {build_dir} --output-on-failure -V -R 'desktop_runtime_probe_init|input_tests'",),
    ),
    "android": PlatformSpec(
        dockerfile="docker/android/Dockerfile",
        image="aoe2-build-android",
        build_dir="build/docker-android",
        commands=with_hd_stage("android", (
            "export GRADLE_USER_HOME={build_dir}/gradle-home",
            "cd platform/android && gradle --build-cache --no-daemon --console=plain assembleDebug "
            "-Paoe2NativeJobs={jobs} -Paoe2Abis=arm64-v8a,x86_64",
            "mkdir -p {build_dir} && cp app/build/outputs/apk/debug/app-debug.apk {build_dir}/aoe2-recomp-debug.apk",
        )),
        test_commands=(
            "cd platform/android && gradle --build-cache --no-daemon --console=plain testDebugUnitTest",
        ),
    ),
    "switch": PlatformSpec(
        dockerfile="docker/switch/Dockerfile",
        image="aoe2-build-switch",
        build_dir="build/docker-switch",
        commands=with_hd_stage("switch", cmake_commands(
            "-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake", "-DCMAKE_BUILD_TYPE=Debug",
            "-DAOE2_RUNTIME_DIAGNOSTICS=ON",
            "-DAOE2_BUILD_GAME=ON", "-DAOE2_DEFAULT_BOOT_ELF=AOK.ELF",
            "-DPLATFORM=SDL", "-DPS2X_ENABLE_FFMPEG=OFF",
            targets=("aoe2_game_nro",),
        )),
        test_commands=(
            "printf '%s\\n' 'Switch NRO tests run in emulator container; run python3 tools/validate_switch.py'",
        ),
    ),
}


def canonical_platform_name(platform: str) -> str:
    return PLATFORM_ALIASES.get(platform, platform)


def container_name_for(args: argparse.Namespace) -> str:
    """Distinct container identifier per platform/variant so parallel runs do not collide."""
    platform = canonical_platform_name(args.platform)
    suffix = "-diagnostics" if getattr(args, "diagnostics", False) else ""
    return f"aoe2-build-{platform}{suffix}"


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
            raise PreflightError(f"container '{name}' is already running")


def generated_source_count() -> int:
    return sum(1 for _ in GENERATED_DIR.glob("*.cpp")) if GENERATED_DIR.is_dir() else 0


def check_preflight(args: argparse.Namespace) -> None:
    """Fail fast before any heavy work; raises PreflightError."""
    if shutil.which("docker") is None:
        raise PreflightError("docker CLI not found")
    if generated_source_count() == 0:
        raise PreflightError(f"no generated sources in {GENERATED_DIR}; run tools/run_ps2recomp_pipeline.py")
    contexts = subprocess.run(["docker", "context", "ls", "--format", "{{.Name}}"], capture_output=True, text=True)
    if DOCKER_CONTEXT not in contexts.stdout.split():
        raise PreflightError(f"docker context '{DOCKER_CONTEXT}' not found")
    if args.jobs < 1:
        raise PreflightError("--jobs must be at least 1")
    if not getattr(args, "dry_run", False):
        ensure_container_clean(container_name_for(args))


def select_spec(platform: str, diagnostics: bool) -> PlatformSpec:
    """Return the build spec for a platform, or its diagnostic variant; raises PreflightError if none exists."""
    canonical = canonical_platform_name(platform)
    if not diagnostics:
        return PLATFORMS[canonical]
    if canonical not in DIAGNOSTIC_VARIANTS:
        raise PreflightError(f"--diagnostics is only available for: {', '.join(sorted(DIAGNOSTIC_VARIANTS))}")
    return DIAGNOSTIC_VARIANTS[canonical]


def image_build_command(spec: PlatformSpec, uid: int, gid: int) -> list:
    return [
        "docker", "--context", DOCKER_CONTEXT, "build",
        "-t", spec.image,
        "-f", str(REPO_ROOT / spec.dockerfile),
        "--build-arg", f"BUILD_UID={uid}",
        "--build-arg", f"BUILD_GID={gid}",
        str((REPO_ROOT / spec.dockerfile).parent),
    ]


def build_script(spec: PlatformSpec, jobs: int, test: bool = False) -> str:
    """Shell script run inside the container, failing on the first error."""
    build_dir = f"{CONTAINER_ROOT}/{spec.build_dir}"
    commands = list(spec.commands)
    if test:
        commands.extend(spec.test_commands)
    steps = [command.format(jobs=jobs, build_dir=build_dir) for command in commands]
    return "set -euo pipefail\n" + "\n".join(steps) + "\n"


def container_run_command(spec: PlatformSpec, args: argparse.Namespace) -> list:
    name = container_name_for(args)
    platform = canonical_platform_name(args.platform)
    return [
        "docker", "--context", DOCKER_CONTEXT, "run", "--rm",
        f"--name={name}",
        f"--hostname={name}",
        f"--label=aoe2.platform={platform}",
        "--label=aoe2.role=build",
        f"--label=aoe2.container={name}",
        f"--memory={MEMORY_LIMIT}", f"--memory-swap={MEMORY_LIMIT}", f"--cpus={args.jobs}",
        "-e", "LC_ALL=C", "-e", "TZ=America/Sao_Paulo",
        "-v", f"{REPO_ROOT}:{CONTAINER_ROOT}",
        *[flag for device in spec.devices for flag in ("--device", device)],
        "-w", CONTAINER_ROOT,
        spec.image, "bash", "-c", build_script(spec, args.jobs, getattr(args, "test", False)),
    ]


def parse_args(argv: list) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    supported_platforms = sorted(set(PLATFORMS) | set(PLATFORM_ALIASES))
    parser.add_argument("--platform", required=True, choices=supported_platforms,
                        help=f"target platform ({', '.join(supported_platforms)})")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS,
                        help=f"number of parallel build jobs (default: {DEFAULT_JOBS})")
    parser.add_argument("--diagnostics", action="store_true",
                        help="build with verbose runtime traces into build/docker-<platform>-diagnostics")
    parser.add_argument("--test", action="store_true",
                        help="run test suite inside the container after build")
    parser.add_argument("--dry-run", action="store_true", help="print the docker commands without running them")
    return parser.parse_args(argv)


def main(argv: list | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_preflight(args)
        spec = select_spec(args.platform, args.diagnostics)
    except PreflightError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT
    commands = [image_build_command(spec, os.getuid(), os.getgid()), container_run_command(spec, args)]
    for command in commands:
        print("+ " + shlex.join(command[:12]) + (" ..." if len(command) > 12 else ""), flush=True)
        if args.dry_run:
            continue
        result = subprocess.run(command)
        if result.returncode != 0:
            return result.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
