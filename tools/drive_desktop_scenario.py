#!/usr/bin/env python3
"""Drive the desktop diagnostic build through a named UI scenario, idempotently.

Every run starts from the same state: any previous game/Xvfb started by this tool is
stopped, the memory-card root is reset (unless --keep-card), a fresh Xvfb and game are
launched, and the scenario's steps run at fixed host-window coordinates (640x448 window).
Screenshots land in .playwright-mcp/ as <scenario>-<step>.png.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from tools.validate_desktop import (  # noqa: E402
    EVIDENCE_DIR, SCREENSHOT_DIR, capture_screenshot, game_command, stop_process,
)

DISPLAY = ":99"  # capture_screenshot reads this display
CARD_ROOT = REPO_ROOT / "game" / "SLES-50282" / "mc0"
BOOT_SECONDS = 13
WINDOW_SIZE = "640x448"
CAMPAIGN_ROW_OFFSET = 38  # 0 = Campaigns, 38 = Random Map (row pitch)

# (step name, action, argument, settle seconds). Coordinates are host-window pixels.
BOOT_TO_MENU = [
    ("key-return-1", "key", "Return", 5),
    ("key-return-2", "key", "Return", 5),
    ("osk-letter-a", "click", (176, 64), 4),
    ("osk-enter", "click", (470, 151), 4),
    ("card-ok", "click", (251, 312), 10),
]
SCENARIOS = {
    "main-menu": BOOT_TO_MENU,
    "single-player": BOOT_TO_MENU + [("single-player", "click", (458, 85), 10)],
    "campaign": BOOT_TO_MENU + [
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
    ],
}


def run(command: list, **options) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, text=True,
                          env={**os.environ, "DISPLAY": DISPLAY}, **options)


def find_game_window() -> str:
    """The game window is the only visible 640x448 one; fail fast if it is missing."""
    for window in run(["xdotool", "search", "--onlyvisible", "--name", "."]).stdout.split():
        if WINDOW_SIZE in run(["xdotool", "getwindowgeometry", window]).stdout:
            return window
    raise RuntimeError(f"no visible {WINDOW_SIZE} game window on {DISPLAY}")


def reset_card() -> None:
    shutil.rmtree(CARD_ROOT, ignore_errors=True)
    CARD_ROOT.mkdir(parents=True)


def kill_leftovers() -> None:
    """Stop earlier runs of this tool only (matched by our private display)."""
    subprocess.run(["pkill", "-f", f"Xvfb {DISPLAY}"], capture_output=True)
    time.sleep(1)


def do_step(window: str, action: str, argument, settle: int) -> None:
    if action == "key":
        run(["xdotool", "keydown", argument]); time.sleep(0.3)
        run(["xdotool", "keyup", argument])
    elif action == "click":
        run(["xdotool", "mousemove", "--window", window, str(argument[0]), str(argument[1])])
        time.sleep(1.5)
        run(["xdotool", "mousedown", "1"]); time.sleep(0.8)
        run(["xdotool", "mouseup", "1"])
    else:
        raise ValueError(f"unknown action {action!r}")
    time.sleep(settle)


def allow_ptrace() -> None:
    """Child hook: let gdb attach (Yama ptrace_scope=1 only allows ancestors)."""
    import ctypes
    ctypes.CDLL(None).prctl(0x59616d61, ctypes.c_ulong(-1))  # PR_SET_PTRACER, PR_SET_PTRACER_ANY


def save_stacks(pid: int, scenario: str) -> None:
    """Backtraces of every host thread; recompiled functions appear as func_<guest address>."""
    result = subprocess.run(["gdb", "-p", str(pid), "-batch", "-ex", "thread apply all bt 12"],
                            capture_output=True, text=True)
    samples = [result.stdout]
    for _ in range(5):
        again = subprocess.run(["gdb", "-p", str(pid), "-batch", "-ex", "thread 2", "-ex", "bt 60"],
                               capture_output=True, text=True)
        samples.append(again.stdout)
        time.sleep(0.7)
    (EVIDENCE_DIR / f"scenario-{scenario}-stacks.txt").write_text("\n=====\n".join(samples))


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("scenario", choices=sorted(SCENARIOS))
    parser.add_argument("--stacks", action="store_true", help="save host thread backtraces (gdb) after the last step")
    parser.add_argument("--keep-card", action="store_true", help="do not reset the memory-card root")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    for tool in ("Xvfb", "xdotool", "xwd", "convert"):
        if shutil.which(tool) is None:
            sys.stderr.write(f"PREFLIGHT FAILED: {tool} missing\n"); return 2
    kill_leftovers()
    if not args.keep_card:
        reset_card()
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
    display = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24", "-nolisten", "tcp"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    log = (EVIDENCE_DIR / f"scenario-{args.scenario}.log").open("w")
    game = None
    try:
        time.sleep(2)
        game = subprocess.Popen(game_command(), stdout=log, stderr=subprocess.STDOUT,
                                env={**os.environ, "DISPLAY": DISPLAY}, preexec_fn=allow_ptrace)
        time.sleep(BOOT_SECONDS)
        window = find_game_window()
        run(["xdotool", "windowfocus", window])
        for name, action, argument, settle in SCENARIOS[args.scenario]:
            do_step(window, action, argument, settle)
            print(capture_screenshot(f"{args.scenario}-{name}.png"))
            if game.poll() is not None:
                sys.stderr.write(f"game exited during step {name}\n"); return 1
        if args.stacks:
            save_stacks(game.pid, args.scenario)
    finally:
        if game is not None:
            stop_process(game)
        stop_process(display)
        log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
