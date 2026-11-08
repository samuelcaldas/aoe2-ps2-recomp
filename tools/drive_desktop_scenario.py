#!/usr/bin/env python3
"""Drive the desktop diagnostic build through a named UI scenario, idempotently.

Every run starts with a fresh Xvfb and game; the memory-card root is reset unless --keep-card.
The post-menu flow is the shared Single Player -> Random Map -> Start Game sequence in
tools/scenarios; this driver supplies the PC boot sequence and the xdotool mouse backend.
Checkpoint screenshots land in .playwright-mcp/pc/ as pc-random-map-<step>.png.
"""

from __future__ import annotations

import argparse
import os
import select
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from tools.scenarios import (  # noqa: E402
    DESKTOP_MENUS,
    CheckpointRecorder,
    LogMarkerWatcher,
    MenuLayout,
    Press,
    ScenarioRunner,
    Step,
    Tap,
    UnresolvedLayoutError,
    Wait,
    XdotoolMouseBackend,
    calibration_steps,
    random_map_steps,
)
from tools.validate_desktop import (  # noqa: E402
    EVIDENCE_DIR,
    OWNED_DISC_IMAGE,
    OWNED_ELF,
    SCREENSHOT_DIR,
    capture_screenshot,
    game_command,
    stop_process,
)

DISPLAY = ""
CARD_ROOT = REPO_ROOT / "game" / "SLES-50282" / "mc0"
BOOT_SECONDS = 13
DEFAULT_WINDOW = (640, 448)

# Removed scenarios: the campaign replays are retired in favour of the shared random-map flow.
REMOVED_SCENARIOS = {
    "campaign": "use random-map-start",
    "campaign-auto": "use random-map-start",
}
MAP_VIEW_SCENARIOS = ("random-map-start", "calibration")

# PC boot: the first-run memory card, profile creation and the on-screen name keyboard.
# (step name, kind, argument, settle seconds). Coordinates are logical-screen pixels.
BOOT_TABLE = [
    ("key-return-1", "key", "Return", 5),
    ("key-return-2", "key", "Return", 5),
    ("osk-letter-a", "click", (176, 64), 4),
    ("osk-enter", "click", (470, 151), 4),
    # The card screen can still be transitioning when the next click arrives,
    # especially while concurrent debug builds compete for disk and CPU.
    ("card-ok", "click", (251, 312), 35),
    ("profile-new-player", "click", (251, 350), 5),
    ("profile-name-field", "click", (335, 237), 3),
    # The PS2 name editor is an on-screen keyboard; xdotool text targets the
    # host window and does not enter characters into the guest dialog.
    ("profile-name-a", "click", (176, 64), 2),
    ("profile-name-o", "click", (296, 95), 2),
    ("profile-name-k", "click", (176, 95), 2),
    ("profile-name-enter", "click", (500, 153), 3),
    ("profile-submit", "click", (245, 315), 5),
    ("profile-confirm", "click", (245, 315), 15),
    ("profile-settle", "wait", None, 15),
]


def boot_steps() -> tuple[Step, ...]:
    """Convert the PC boot table into shared step commands."""
    steps: list[Step] = []
    for name, kind, argument, settle in BOOT_TABLE:
        if kind == "key":
            steps.append(Press(name=name, button=argument, settle_seconds=settle))
        elif kind == "click":
            steps.append(Tap(name=name, point=argument, settle_seconds=settle))
        else:
            steps.append(Wait(name=name, settle_seconds=settle))
    return tuple(steps)


def build_scenario(name: str, menus: MenuLayout = DESKTOP_MENUS) -> tuple[Step, ...]:
    """Return the steps for a scenario; raises ValueError for removed names and
    UnresolvedLayoutError while a required point has not been measured."""
    if name in REMOVED_SCENARIOS:
        raise ValueError(f"scenario {name!r} was removed; {REMOVED_SCENARIOS[name]}")
    if name == "main-menu":
        return boot_steps()
    if name == "single-player":
        return boot_steps() + (Tap(name="single-player", point=menus.single_player, settle_seconds=10),)
    if name == "random-map-start":
        return boot_steps() + random_map_steps(menus)
    if name == "calibration":
        return boot_steps() + random_map_steps(menus) + calibration_steps()
    raise ValueError(f"unknown scenario {name!r}")


SCENARIO_NAMES = ("main-menu", "single-player", "random-map-start", "calibration")


def run(command: list, **options) -> subprocess.CompletedProcess:
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        env={**os.environ, "DISPLAY": DISPLAY},
        **options,
    )


def find_game_window(window_size: str = "640x448") -> str:
    """Find the game window matching the requested geometry, or the main top window."""
    for window in run(
        ["xdotool", "search", "--onlyvisible", "--name", "."]
    ).stdout.split():
        if window_size in run(["xdotool", "getwindowgeometry", window]).stdout:
            return window
    candidates = run(
        ["xdotool", "search", "--onlyvisible", "--name", "."]
    ).stdout.split()
    if candidates:
        return candidates[-1]
    raise RuntimeError(f"no visible {window_size} game window on {DISPLAY}")


def reset_card() -> None:
    shutil.rmtree(CARD_ROOT, ignore_errors=True)
    CARD_ROOT.mkdir(parents=True)


def start_display(width: int, height: int) -> tuple[subprocess.Popen, str]:
    """Let Xvfb reserve a free display, without disturbing other sessions."""
    read_fd, write_fd = os.pipe()
    try:
        process = subprocess.Popen(
            [
                "Xvfb",
                "-displayfd",
                str(write_fd),
                "-screen",
                "0",
                f"{width}x{height}x24",
                "-nolisten",
                "tcp",
            ],
            pass_fds=(write_fd,),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    finally:
        os.close(write_fd)
    try:
        ready, _, _ = select.select([read_fd], [], [], 10)
        number = os.read(read_fd, 32).strip() if ready else b""
        if not number.isdigit() or process.poll() is not None:
            stop_process(process)
            raise RuntimeError("Xvfb failed to allocate a display")
        return process, f":{number.decode('ascii')}"
    finally:
        os.close(read_fd)


def allow_ptrace() -> None:
    """Child hook: let gdb attach (Yama ptrace_scope=1 only allows ancestors)."""
    import ctypes

    ctypes.CDLL(None).prctl(
        0x59616D61, ctypes.c_ulong(-1)
    )  # PR_SET_PTRACER, PR_SET_PTRACER_ANY


def save_stacks(pid: int, scenario: str) -> None:
    """Backtraces of every host thread; recompiled functions appear as func_<guest address>."""
    result = subprocess.run(
        ["gdb", "-p", str(pid), "-batch", "-ex", "thread apply all bt 12"],
        capture_output=True,
        text=True,
    )
    samples = [result.stdout]
    for _ in range(5):
        again = subprocess.run(
            ["gdb", "-p", str(pid), "-batch", "-ex", "thread 2", "-ex", "bt 60"],
            capture_output=True,
            text=True,
        )
        samples.append(again.stdout)
        time.sleep(0.7)
    (EVIDENCE_DIR / f"scenario-{scenario}-stacks.txt").write_text(
        "\n=====\n".join(samples)
    )


def parse_resolution(value: str) -> tuple[int, int]:
    width, height = value.split("x")
    return int(width), int(height)


def screenshot_to_stdout(name: str) -> Path:
    """Checkpoint writer: capture the display and print where the image landed."""
    path = capture_screenshot(name, DISPLAY)
    print(path, flush=True)
    return path


def drive(
    steps: tuple[Step, ...],
    *,
    game,
    log_path: Path,
    window: str,
    window_size: tuple[int, int],
    factor: int,
    recorder: CheckpointRecorder,
):
    """Run the steps against the live game window and return the RunResult."""
    backend = XdotoolMouseBackend(run, window, window_size)
    watcher = LogMarkerWatcher(log_path, lambda: game.poll() is None)
    runner = ScenarioRunner(
        backend=backend,
        await_marker=watcher.await_marker,
        is_alive=lambda: game.poll() is None,
        observer=recorder,
        settle_scale=factor,
        sleep=time.sleep,
    )
    return runner.run(steps)


def check_scenario_preconditions(parser, args) -> None:
    """Fail fast on flags a scenario cannot use, before any display or game is started."""
    if args.scenario in MAP_VIEW_SCENARIOS and os.environ.get("AOE2_MAP_VIEW") not in (
        "native",
        "relocation",
    ):
        parser.error(
            f"{args.scenario} requires AOE2_MAP_VIEW=native or relocation and diagnostic guest TTY"
        )


def exit_on_terminate(signum, frame) -> None:
    """Turn SIGTERM into a normal exit so the game and display are stopped in main's finally block."""
    raise SystemExit(128 + signum)


def main(argv: list | None = None) -> int:
    global DISPLAY, CARD_ROOT
    signal.signal(signal.SIGTERM, exit_on_terminate)
    parser = argparse.ArgumentParser(description=__doc__)
    # Removed names stay accepted by argparse so build_scenario can explain the replacement.
    parser.add_argument("scenario", choices=(*SCENARIO_NAMES, *REMOVED_SCENARIOS))
    parser.add_argument(
        "--resolution",
        default="640x448",
        help="display/window resolution (e.g. 1920x1080, 1280x720, 640x448)",
    )
    parser.add_argument(
        "--stacks",
        action="store_true",
        help="save host thread backtraces (gdb) after the last step",
    )
    parser.add_argument(
        "--keep-card", action="store_true", help="do not reset the memory-card root"
    )
    parser.add_argument(
        "--card-root",
        type=Path,
        help="memory-card directory to use (defaults to the owned game card)",
    )
    parser.add_argument(
        "--game-root",
        type=Path,
        help="isolated runtime root containing AOK.ELF and aoe2.iso",
    )
    parser.add_argument(
        "--binary",
        type=Path,
        help="game executable to run instead of the diagnostic build",
    )
    parser.add_argument("--build-type", choices=("release", "debug"), default="release")
    parser.add_argument("--label", help="distinct evidence label for comparing runs")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    factor = 10 if args.build_type == "debug" else 1
    os.environ["AOE2_VALIDATE_BUILD_TYPE"] = args.build_type
    if args.label and (
        not args.label.isascii()
        or not all(c.isalnum() or c in "-_" for c in args.label)
    ):
        parser.error("--label must contain only ASCII letters, digits, '-' or '_'")
    if args.binary and not args.binary.is_file():
        parser.error(f"game executable not found: {args.binary}")
    check_scenario_preconditions(parser, args)
    try:
        steps = build_scenario(args.scenario)
    except UnresolvedLayoutError as error:
        parser.error(f"{error}; capture the Game Settings screen and measure it in tools/scenarios/layout.py")
    except ValueError as error:
        parser.error(str(error))

    evidence_name = f"{args.scenario}-{args.label}" if args.label else args.scenario
    for tool in ("Xvfb", "xdotool", "xwd", "convert"):
        if shutil.which(tool) is None:
            sys.stderr.write(f"PREFLIGHT FAILED: {tool} missing\n")
            return 2
    game_root = args.game_root.resolve() if args.game_root else OWNED_ELF.parent
    elf_path = game_root / OWNED_ELF.name
    disc_path = game_root / OWNED_DISC_IMAGE.name
    if not elf_path.is_file():
        parser.error(f"game ELF not found: {elf_path}")
    if args.game_root and not disc_path.is_file():
        parser.error(f"isolated game root is missing disc image: {disc_path}")
    CARD_ROOT = args.card_root.resolve() if args.card_root else game_root / "mc0"
    if not args.keep_card:
        reset_card()
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)

    win_w, win_h = parse_resolution(args.resolution) if args.resolution else DEFAULT_WINDOW
    display, DISPLAY = start_display(win_w, win_h)
    log_path = EVIDENCE_DIR / f"scenario-{evidence_name}.log"
    log = log_path.open("w")
    game = None
    try:
        time.sleep(2)
        env = {
            **os.environ,
            "DISPLAY": DISPLAY,
            "AOE2_WINDOW_WIDTH": str(win_w),
            "AOE2_WINDOW_HEIGHT": str(win_h),
        }
        command = game_command()
        command[1] = str(elf_path)
        if len(command) > 2:
            command[2] = str(disc_path if disc_path.is_file() else OWNED_DISC_IMAGE)
        if args.binary:
            command[0] = str(args.binary.resolve())
        game = subprocess.Popen(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            cwd=game_root,
            preexec_fn=allow_ptrace,
        )
        time.sleep(BOOT_SECONDS * factor)
        window = find_game_window(args.resolution)
        run(["xdotool", "windowfocus", window])
        recorder = CheckpointRecorder("pc", screenshot_to_stdout)
        result = drive(
            steps,
            game=game,
            log_path=log_path,
            window=window,
            window_size=(win_w, win_h),
            factor=factor,
            recorder=recorder,
        )
        if not result.passed:
            sys.stderr.write(
                f"scenario {args.scenario} stopped at step {result.failed_step}; see {log_path}\n"
            )
            if args.stacks and game.poll() is None:
                save_stacks(game.pid, evidence_name)
            return 1
        if args.stacks:
            save_stacks(game.pid, evidence_name)
    finally:
        if game is not None:
            stop_process(game)
        stop_process(display)
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
