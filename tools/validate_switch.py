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
SCREENSHOT_DIR = ".playwright-mcp/switch"
SCREENSHOT_INTERVAL = 10
DISPLAY = ":99"
MEMORY_LIMIT = "12g"
CONTAINER_NAME = "aoe2-validate-switch"
DEFAULT_JOBS = 8
EXIT_PASS, EXIT_FAIL, EXIT_PREFLIGHT = 0, 1, 2


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
            raise PreflightError(f"container '{name}' is already running")


def persistent_sdcard_setup(config_dir: str) -> list[str]:
    """Seed game files once and keep save/card data in Ryujinx's persistent SD tree."""
    sdcard_dir = f"{config_dir}/sdcard/switch/aoe2"
    return [
        f"mkdir -p {sdcard_dir}",
        f"if [ -d {CONTAINER_ROOT}/game/SLES-50282 ]; then",
        f"    for item in {CONTAINER_ROOT}/game/SLES-50282/*; do",
        "        name=$(basename \"$item\")",
        '        [ "$name" = aoe2.iso ] && continue',  # devices never receive the disc image
        f'        if [ ! -e "{sdcard_dir}/$name" ]; then',
        f'            cp -r "$item" "{sdcard_dir}/$name"',
        "        fi",
        "    done",
        "fi",
        f"mkdir -p {sdcard_dir}/mc0 {sdcard_dir}/mc1",
        f"echo '[switch-storage] persistent memory cards: {sdcard_dir}/mc0 and mc1'",
    ]


def clean_ryujinx_transient_data(ryu_data: Path) -> None:
    """Reset emulator config/cache while preserving the full SD card and game saves."""
    if not ryu_data.exists():
        return
    for item in ryu_data.iterdir():
        if item.name == "sdcard":
            continue
        if item.is_dir():
            shutil.rmtree(item, ignore_errors=True)
        else:
            item.unlink(missing_ok=True)


def ryujinx_config_setup(config_dir: str, width: int = 1280, height: int = 760) -> list[str]:
    """Configure visible Ryujinx window, keyboard/mouse and the testable pad mapping."""
    return [
        "python3 -c \"",
        "import json, os",
        f"cfg_path = '{config_dir}/Config.json'",
        "cfg = {}",
        "if os.path.isfile(cfg_path):",
        "    try:",
        "        with open(cfg_path) as f: cfg = json.load(f)",
        "    except Exception: pass",
        "cfg['memory_manager_mode'] = 'SoftwarePageTable'",
        "cfg['check_updates_on_start'] = False",
        "cfg['update_checker_type'] = 'Off'",
        "cfg['start_fullscreen'] = False",
        "cfg['start_no_ui'] = False",
        "cfg['show_title_bar'] = True",
        "cfg['show_console'] = True",
        "cfg['enable_file_log'] = True",
        "cfg['enable_keyboard'] = True",
        "cfg['enable_mouse'] = True",
        f"cfg['window_startup'] = {{'window_size_width': {width}, 'window_size_height': {height}, 'window_position_x': 0, 'window_position_y': 0, 'window_maximized': False}}",
        "for inp in cfg.get('input_config', []):",
        "    if inp.get('player_index') == 'Player1':",
        "        rj = inp.setdefault('right_joycon', {})",
        "        rj['button_plus'] = 'Enter'",
        "        rj['button_b'] = 'Z'",
        "        rj['button_a'] = 'X'",
        "        rj['button_x'] = 'C'",
        "        rj['button_y'] = 'V'",
        "        lj = inp.setdefault('left_joycon', {})",
        "        lj['dpad_up'] = 'Up'",
        "        lj['dpad_down'] = 'Down'",
        "        lj['dpad_left'] = 'Left'",
        "        lj['dpad_right'] = 'Right'",
        "        lj['button_minus'] = 'Minus'",
        "with open(cfg_path, 'w') as f:",
        "    json.dump(cfg, f, indent=2)",
        "\"",
    ]


class PreflightError(RuntimeError):
    """Raised when a prerequisite for the Switch validation is missing."""


def emulator_script(
    run_seconds: int,
    interval: int = SCREENSHOT_INTERVAL,
    nro: str = NRO_PATH,
    mode: str = "native",
    scenario: str = "campaign-auto",
) -> str:
    """Shell script run inside the container; writes raw evidence for the host to judge."""
    evidence = f"{CONTAINER_ROOT}/{EVIDENCE_DIR}"
    shots_dir = f"{CONTAINER_ROOT}/{SCREENSHOT_DIR}"
    shots = max(1, run_seconds // interval)
    config_dir = f"{evidence}/ryujinx-data"
    system_dir = f"{config_dir}/system"
    sdcard_dir = f"{config_dir}/sdcard/switch/aoe2"
    ini_mode = "native" if mode == "native" else "legacy"

    return (
        "\n".join(
            [
                "set -euo pipefail",
                f"mkdir -p {evidence} {shots_dir} {system_dir}",
                f"if [ -f {CONTAINER_ROOT}/{PROD_KEYS_DEFAULT} ]; then cp -f {CONTAINER_ROOT}/{PROD_KEYS_DEFAULT} {system_dir}/prod.keys; fi",
                *persistent_sdcard_setup(config_dir),
                f"sdcard_dir={config_dir}/sdcard/switch/aoe2",
                f"echo 'mode={ini_mode}' > {sdcard_dir}/map_view.ini",
                f"export AOE2_MAP_VIEW={ini_mode}",
                f"Xvfb {DISPLAY} -screen 0 1280x760x24 > /dev/null 2>&1 &",
                "XVFB_PID=$!",
                "sleep 1",
                f"export DISPLAY={DISPLAY}",
                "RYUJINX_BIN=$(which Ryujinx.Headless.SDL2 2>/dev/null || which Ryujinx 2>/dev/null || true)",
                'if [ -z "$RYUJINX_BIN" ]; then echo "FATAL: Ryujinx executable not found in PATH" >&2; kill $XVFB_PID; exit 1; fi',
                f'timeout 2 "$RYUJINX_BIN" --root-data-dir {config_dir} > /dev/null 2>&1 || true',
                *ryujinx_config_setup(config_dir),
                f"x11vnc -display {DISPLAY} -nopw -forever -shared -rfbport 5900 > /dev/null 2>&1 &",
                "VNC_PID=$!",
                "websockify --web /usr/share/novnc 6080 localhost:5900 > /dev/null 2>&1 &",
                "WEBSOCK_PID=$!",
                "echo '================================================================='",
                "echo 'Validation live view: http://localhost:6080/vnc.html'",
                "echo '================================================================='",
                f'"$RYUJINX_BIN" --root-data-dir {config_dir} {CONTAINER_ROOT}/{nro} > {evidence}/run.log 2>&1 &',
                "EMU_PID=$!",
                f"python3 -u {CONTAINER_ROOT}/tools/drive_switch_scenario.py --mode {mode} --scenario {scenario} --evidence {evidence} --screenshots {shots_dir} --display {DISPLAY} --run-seconds {run_seconds} --speed-factor 1.3 > {evidence}/scenario.log 2>&1 &",
                "DRIVER_PID=$!",
                f"for shot in $(seq 1 {shots}); do sleep {interval}; ",
                f"ts=$(date +%Y%m%d-%H%M%S); shot_num=$(printf '%03d' \"$shot\"); ",
                f"xwd -root -display {DISPLAY} 2>/dev/null | convert xwd:- {shots_dir}/screen-{mode}-${{ts}}-${{shot_num}}.png 2>/dev/null || true; ",
                'if ! kill -0 $DRIVER_PID 2>/dev/null; then echo "Driver process finished; ending capture loop."; sleep 2; break; fi; ',
                "done",
                f"if kill -0 $EMU_PID 2>/dev/null; then echo yes > {evidence}/alive.txt; ",
                f"else echo no > {evidence}/alive.txt; fi",
                "kill $DRIVER_PID 2>/dev/null || true",
                "kill $EMU_PID 2>/dev/null || true",
                "kill $WEBSOCK_PID 2>/dev/null || true",
                "kill $VNC_PID 2>/dev/null || true",
                "kill $XVFB_PID 2>/dev/null || true",
            ]
        )
        + "\n"
    )


def evidence_passed(
    alive: bool,
    log: str,
    screenshots: list,
    executor=run_command,
    mode: str = "native",
    scenario: str = "smoke",
) -> bool:
    """Same verdict rules as desktop/Android validators, plus mode and map checks."""
    if not alive:
        return False
    if mode == "native":
        if ("fallback to GS is prohibited in native mode" in log
                or "presentation: guest framebuffer fallback" in log
                or "map-only: menus and HUD are not implemented" in log
                or "GS::latchHostPresentationFrame" in log):
            return False
    if classify_run_log(log).verdict is not RunVerdict.PROGRESSING:
        return False
    if screenshots and not any(f"-{mode}-" in s for s in screenshots):
        return False
    frames = analyze_frames(screenshots, executor)
    if frames.real_frames < 2 or not frames.changed:
        return False
    if scenario in ("campaign", "campaign-auto"):
        # A log marker, a filename, and changing pixels cannot establish that
        # the actual mission map is visible. Campaign acceptance requires
        # visual inspection; do not report an automated PASS on that basis.
        return False
    return True


def container_command(
    run_seconds: int,
    jobs: int = DEFAULT_JOBS,
    nro: str = NRO_PATH,
    port: int = 6080,
    mode: str = "native",
    scenario: str = "campaign-auto",
) -> list:
    return [
        "docker",
        "--context",
        DOCKER_CONTEXT,
        "run",
        "--rm",
        f"--name={CONTAINER_NAME}",
        f"--hostname={CONTAINER_NAME}",
        "--label=aoe2.platform=switch",
        "--label=aoe2.role=validate",
        f"--label=aoe2.container={CONTAINER_NAME}",
        f"--memory={MEMORY_LIMIT}",
        f"--memory-swap={MEMORY_LIMIT}",
        f"--cpus={jobs}",
        "-p",
        f"{port}:6080",
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
        emulator_script(run_seconds, nro=nro, mode=mode, scenario=scenario),
    ]


def check_preflight(
    keys_path: Path = REPO_ROOT / PROD_KEYS_DEFAULT,
    nro_path: Path | None = None,
) -> None:
    if shutil.which("docker") is None:
        raise PreflightError("docker CLI not found")
    contexts = subprocess.run(
        ["docker", "context", "ls", "--format", "{{.Name}}"],
        capture_output=True,
        text=True,
    )
    if DOCKER_CONTEXT not in contexts.stdout.split():
        raise PreflightError(f"docker context '{DOCKER_CONTEXT}' not found")
    if nro_path is None:
        nro_path = REPO_ROOT / NRO_PATH
    elif not nro_path.is_absolute():
        nro_path = REPO_ROOT / nro_path
    if not nro_path.exists():
        raise PreflightError(
            f"missing Switch binary {nro_path}; build the requested configuration first"
        )
    if keys_path is not None and not keys_path.exists():
        print(
            f"INFO: Switch prod.keys not found at {keys_path.relative_to(REPO_ROOT) if keys_path.is_relative_to(REPO_ROOT) else keys_path}; proceeding with homebrew NRO without keys",
            flush=True,
        )


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--run-seconds",
        type=int,
        default=1800,
        help="how long to run the emulator under test (seconds)",
    )
    parser.add_argument(
        "--mode",
        choices=["native", "gs"],
        default="native",
        help="rendering mode: native or gs (default: native)",
    )
    parser.add_argument(
        "--scenario",
        choices=["campaign", "campaign-auto", "smoke"],
        default="campaign-auto",
        help="scenario to drive (default: campaign-auto)",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=DEFAULT_JOBS,
        help="CPU quota allocated to emulator container (default: 8)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=6080,
        help="host port for noVNC web view (default: 6080)",
    )
    parser.add_argument(
        "--keys",
        type=Path,
        default=REPO_ROOT / PROD_KEYS_DEFAULT,
        help="path to Switch prod.keys",
    )
    parser.add_argument(
        "--nro",
        type=Path,
        default=Path(NRO_PATH),
        help="NRO path relative to the repository root (default: Debug build)",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="print commands without executing them"
    )
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)

    try:
        check_preflight(args.keys, args.nro)
    except PreflightError as error:
        sys.stderr.write(f"PREFLIGHT FAILED: {error}\n")
        return EXIT_PREFLIGHT

    evidence_path = REPO_ROOT / EVIDENCE_DIR
    screenshots_path = REPO_ROOT / SCREENSHOT_DIR
    screenshots_path.mkdir(parents=True, exist_ok=True)
    for pattern in ("screen-*.png", "switch-*.png"):
        for old_shot in screenshots_path.glob(pattern):
            try:
                old_shot.unlink()
            except OSError:
                pass

    ryu_data = evidence_path / "ryujinx-data"
    clean_ryujinx_transient_data(ryu_data)

    if not args.dry_run:
        ensure_container_clean(CONTAINER_NAME)

    cmd = container_command(
        args.run_seconds,
        jobs=args.jobs,
        nro=(args.nro if not args.nro.is_absolute() else args.nro.relative_to(REPO_ROOT)).as_posix(),
        port=args.port,
        mode=args.mode,
        scenario=args.scenario,
    )
    print("+ " + shlex.join(cmd[:10]) + " ...", flush=True)
    print(
        f"👉 Visualização no Chrome durante a validação: http://localhost:{args.port}/vnc.html",
        flush=True,
    )
    if args.dry_run:
        return EXIT_PASS

    result = subprocess.run(cmd)
    if result.returncode != 0:
        return result.returncode

    evidence_path = REPO_ROOT / EVIDENCE_DIR
    screenshots_path = REPO_ROOT / SCREENSHOT_DIR
    screenshots_path.mkdir(parents=True, exist_ok=True)
    alive_file = evidence_path / "alive.txt"
    log_file = evidence_path / "run.log"
    alive = alive_file.read_text().strip() == "yes" if alive_file.exists() else False
    log = log_file.read_text(errors="replace") if log_file.exists() else ""
    screenshots = sorted(str(p) for p in screenshots_path.glob("*.png"))
    if not screenshots:
        screenshots = sorted(str(p) for p in evidence_path.glob("*.png"))

    pass_verdict = evidence_passed(
        alive, log, screenshots, mode=args.mode, scenario=args.scenario
    )
    verdict_text = "PASS\n" if pass_verdict else "FAIL\n"
    (evidence_path / "result.txt").write_text(verdict_text)
    print(f"Switch validation verdict: {verdict_text.strip()}", flush=True)

    return EXIT_PASS if pass_verdict else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
