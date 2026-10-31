#!/usr/bin/env python3
"""Drive the desktop diagnostic build through a named UI scenario, idempotently.

Every run starts with a fresh Xvfb and game; the memory-card root is reset unless --keep-card.
The scenario's steps run at fixed logical-screen points (640x448), mapped to the
window through tools/presentation_geometry.py.
Screenshots land in .playwright-mcp/pc/ as <scenario>-<step>.png.
"""

from __future__ import annotations

import argparse
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from tools.presentation_geometry import to_window  # noqa: E402
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
WINDOW_SIZE = "640x448"
CAMPAIGN_ROW_OFFSET = 38  # 0 = Campaigns, 38 = Random Map (row pitch)

# (step name, action, argument, settle seconds). Coordinates are logical-screen pixels.
BOOT_TO_MENU = [
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
SCENARIOS = {
    "main-menu": BOOT_TO_MENU,
    "single-player": BOOT_TO_MENU + [("single-player", "click", (458, 85), 10)],
    "campaign": BOOT_TO_MENU
    + [
        ("single-player", "click", (458, 85), 10),
        ("campaigns", "click", (418, 86), 12),
        ("wallace-marker", "click", (221, 77), 12),
        ("scenario-1", "click", (183, 57), 40),
        ("briefing", "key", "shift", 30),
        ("briefing-next", "key", "z", 30),
        ("mission-loading", "key", "z", 45),
        ("brief-3", "key", "z", 25),
        ("brief-4", "key", "z", 25),
        ("brief-5", "key", "z", 40),
        ("mission", "key", "shift", 40),
        ("mission-start", "key", "z", 60),
        ("story-advance-1", "click", (320, 224), 20),
        ("story-advance-2", "click", (320, 224), 20),
        ("story-advance-3", "click", (320, 224), 12),
        ("story-advance-4", "click", (320, 224), 12),
        ("story-advance-5", "click", (320, 224), 12),
        ("story-advance-6", "click", (320, 224), 12),
        ("story-advance-7", "click", (320, 224), 12),
        ("story-advance-8", "click", (320, 224), 12),
        ("mission-live", "wait", None, 180),
    ],
}
# Logical points checked by tools/check_pointer_calibration.py (run with AOE2_LOG_POINTER=1).
CALIBRATION_POINTS = [(100, 120), (520, 380)]
SCENARIOS["calibration"] = SCENARIOS["campaign"] + [
    (f"calib-{x}-{y}", "move", (x, y), 4) for x, y in CALIBRATION_POINTS
]
# This diagnostic variant waits for guest events instead of assuming a fixed
# number of clicks or treating a loading screen as a completed mission.
SCENARIOS["campaign-auto"] = SCENARIOS["campaign"][
    : next(
        i + 1
        for i, step in enumerate(SCENARIOS["campaign"])
        if step[0] == "mission-start"
    )
] + [
    ("story-complete", "story-until", (320, 224), 12),
    ("map-submitted", "map-until", (320, 224), 1200),
    # Keep the guest alive after the first frame so delayed surface overruns,
    # stale snapshots, and compositor failures are observable in the same run.
    ("map-stability", "wait", None, 180),
]


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


def do_step(window: str, action: str, argument, settle: int) -> None:
    if action == "key":
        run(["xdotool", "keydown", argument])
        time.sleep(0.3)
        run(["xdotool", "keyup", argument])
    elif action == "click":
        run(
            [
                "xdotool",
                "mousemove",
                "--window",
                window,
                str(argument[0]),
                str(argument[1]),
            ]
        )
        time.sleep(1.5)
        run(["xdotool", "mousedown", "1"])
        time.sleep(0.8)
        run(["xdotool", "mouseup", "1"])
    elif action == "move":
        run(
            [
                "xdotool",
                "mousemove",
                "--window",
                window,
                str(argument[0]),
                str(argument[1]),
            ]
        )
    elif action == "wait":
        pass
    elif action == "text":
        run(["xdotool", "type", "--clearmodifiers", "--delay", "100", argument])
    else:
        raise ValueError(f"unknown action {action!r}")
    time.sleep(settle)


def follow_guest_event(
    log_path: Path,
    marker: bytes,
    game: subprocess.Popen,
    timeout: float,
    on_wait=None,
    max_interval_seconds: float = 12.0,
) -> bool:
    """Follow a growing diagnostic log, including markers split across reads."""
    deadline = time.monotonic() + timeout
    remainder = b""
    last_action = time.monotonic()
    with log_path.open("rb") as source:
        while True:
            chunk = source.read(64 * 1024)
            if chunk:
                data = remainder + chunk
                if marker in data:
                    return True
                remainder = data[-(len(marker) - 1) :] if len(marker) > 1 else b""
            if game.poll() is not None or time.monotonic() >= deadline:
                return False
            now = time.monotonic()
            if on_wait is not None:
                if not chunk or (now - last_action >= max_interval_seconds):
                    on_wait()
                    last_action = time.monotonic()
                else:
                    time.sleep(0.05)
            else:
                if not chunk:
                    time.sleep(min(1.0, max(0.0, deadline - now)))


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


def main(argv: list | None = None) -> int:
    global DISPLAY
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", choices=sorted(SCENARIOS))
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
    parser.add_argument("--label", help="distinct evidence label for comparing runs")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    if args.label and (
        not args.label.isascii()
        or not all(c.isalnum() or c in "-_" for c in args.label)
    ):
        parser.error("--label must contain only ASCII letters, digits, '-' or '_'")
    if args.binary and not args.binary.is_file():
        parser.error(f"game executable not found: {args.binary}")
    if args.scenario == "campaign-auto" and os.environ.get("AOE2_MAP_VIEW") not in (
        "native",
        "relocation",
    ):
        parser.error(
            "campaign-auto requires AOE2_MAP_VIEW=native or relocation and diagnostic guest TTY"
        )
    evidence_name = f"{args.scenario}-{args.label}" if args.label else args.scenario
    for tool in ("Xvfb", "xdotool", "xwd", "convert"):
        if shutil.which(tool) is None:
            sys.stderr.write(f"PREFLIGHT FAILED: {tool} missing\n")
            return 2
    global CARD_ROOT
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

    win_w, win_h = (640, 448)
    if "x" in args.resolution:
        parts = args.resolution.split("x")
        win_w, win_h = int(parts[0]), int(parts[1])

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
        time.sleep(BOOT_SECONDS)
        window = find_game_window(args.resolution)
        run(["xdotool", "windowfocus", window])
        for name, action, argument, settle in SCENARIOS[args.scenario]:
            scaled_arg = argument
            if action in ("click", "move", "story-until", "map-until") and isinstance(
                argument, tuple
            ):
                window_x, window_y = to_window(argument, (win_w, win_h))
                scaled_arg = (int(window_x), int(window_y))
            if action == "story-until":
                clicks = 0

                def advance_story() -> None:
                    nonlocal clicks
                    clicks += 1
                    do_step(window, "click", scaled_arg, settle)
                    print(
                        capture_screenshot(
                            f"{evidence_name}-story-{clicks}.png", DISPLAY
                        ),
                        flush=True,
                    )

                matched = follow_guest_event(
                    log_path, b"[game-tty] 4 e-txt", game, 300, advance_story
                )
            elif action == "map-until":
                clicks = 0

                def advance_map() -> None:
                    nonlocal clicks
                    clicks += 1
                    do_step(window, "click", scaled_arg, 12)
                    print(
                        capture_screenshot(
                            f"{evidence_name}-map-wait-{clicks}.png", DISPLAY
                        ),
                        flush=True,
                    )

                # Narrative pages after the fourth text event still need input.
                # Keep advancing until the renderer observes an actual map frame.
                matched = follow_guest_event(
                    log_path, b"[map-submit]", game, settle, advance_map
                )
            else:
                do_step(window, action, scaled_arg, settle)
                matched = True
            print(capture_screenshot(f"{evidence_name}-{name}.png", DISPLAY))
            if not matched:
                sys.stderr.write(
                    f"guest event not observed during step {name}; see {log_path}\n"
                )
                if args.stacks and game.poll() is None:
                    save_stacks(game.pid, evidence_name)
                return 1
            if game.poll() is not None:
                sys.stderr.write(f"game exited during step {name}\n")
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
