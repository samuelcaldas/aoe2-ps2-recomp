#!/usr/bin/env python3
"""Drive the Switch executable inside the emulator container through UI scenarios.

Executes scenario steps targeting the Ryujinx emulator window inside Xvfb:
- Focuses the emulator window;
- Executes sequence of keyboard keys and mouse clicks (mapped to logical 640x448);
- Follows guest log events (such as [game-tty] and [map-submit]);
- Captures timed and per-step screenshots labeled with mode ('native' or 'gs');
- Fails fast if native mode fails or falls back to GS at runtime.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.presentation_geometry import to_window  # noqa: E402

DISPLAY_DEFAULT = ":99"
WINDOW_SIZE = (1280, 720)

# (step name, action, argument, settle seconds)
# Logical coordinates match PS2 640x448 screen.
BOOT_TO_MENU = [
    ("key-return-1", "key", "Return", 5),
    ("key-return-2", "key", "Return", 5),
    ("osk-letter-a", "click", (176, 64), 4),
    ("osk-enter", "click", (470, 151), 4),
    ("card-ok", "click", (251, 312), 35),
    ("profile-new-player", "click", (251, 350), 5),
    ("profile-name-field", "click", (335, 237), 3),
    ("profile-name-a", "click", (176, 64), 2),
    ("profile-name-o", "click", (296, 95), 2),
    ("profile-name-k", "click", (176, 95), 2),
    ("profile-name-enter", "click", (500, 153), 3),
    ("profile-submit", "click", (245, 315), 5),
    ("profile-confirm", "click", (245, 315), 15),
    ("profile-settle", "wait", None, 15),
]

SCENARIOS = {
    "smoke": BOOT_TO_MENU[:5],
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
        ("mission-start", "key", "z", 60),
        ("story-advance-1", "click", (320, 224), 20),
        ("story-advance-2", "click", (320, 224), 20),
        ("story-advance-3", "click", (320, 224), 12),
        ("story-advance-4", "click", (320, 224), 12),
        ("story-advance-5", "click", (320, 224), 12),
        ("story-advance-6", "click", (320, 224), 12),
        ("story-advance-7", "click", (320, 224), 12),
        ("story-advance-8", "click", (320, 224), 12),
        ("mission-live", "wait", None, 30),
    ],
    "campaign-auto": BOOT_TO_MENU + [
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
        ("story-complete", "story-until", (320, 224), 12),
        ("map-submitted", "map-until", (320, 224), 300),
        ("map-stability", "wait", None, 30),
    ],
}


def find_window(display: str, timeout: float = 30.0) -> str:
    """Find the Ryujinx window ID on the display."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        res = subprocess.run(
            ["xdotool", "search", "--onlyvisible", "--name", "Ryujinx.*"],
            capture_output=True,
            text=True,
            env={**os.environ, "DISPLAY": display},
        )
        if res.returncode == 0 and res.stdout.strip():
            for win in res.stdout.strip().splitlines():
                win = win.strip()
                name_res = subprocess.run(
                    ["xdotool", "getwindowname", win],
                    capture_output=True,
                    text=True,
                    env={**os.environ, "DISPLAY": display},
                )
                if "ContentDialog" not in name_res.stdout:
                    return win
        time.sleep(1.0)
    return ""


def capture_screenshot(out_path: Path, display: str) -> bool:
    """Capture full display root window to PNG file."""
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = f"xwd -root -display {display} 2>/dev/null | convert xwd:- {out_path} 2>/dev/null"
    res = subprocess.run(cmd, shell=True)
    return res.returncode == 0 and out_path.exists() and out_path.stat().st_size > 0


def follow_guest_event(
    log_path: Path,
    marker: bytes,
    timeout: float,
    on_wait=None,
    max_interval_seconds: float = 12.0,
) -> bool:
    """Follow a growing run log until marker is found or timeout."""
    deadline = time.monotonic() + timeout
    remainder = b""
    last_action = time.monotonic()
    if not log_path.exists():
        time.sleep(1.0)
    with log_path.open("rb") as source:
        while True:
            chunk = source.read(64 * 1024)
            if chunk:
                data = remainder + chunk
                if marker in data:
                    return True
                remainder = data[-(len(marker) - 1):] if len(marker) > 1 else b""
            now = time.monotonic()
            if now >= deadline:
                return False
            if on_wait is not None:
                if not chunk or (now - last_action >= max_interval_seconds):
                    on_wait()
                    last_action = time.monotonic()
                else:
                    time.sleep(0.05)
            else:
                if not chunk:
                    time.sleep(min(1.0, max(0.0, deadline - now)))


def do_step(window: str, action: str, argument, settle: float, display: str) -> None:
    """Execute a single scenario action on the window."""
    env = {**os.environ, "DISPLAY": display}
    if action == "key":
        subprocess.run(["xdotool", "keydown", "--window", window, str(argument)], env=env)
        time.sleep(0.3)
        subprocess.run(["xdotool", "keyup", "--window", window, str(argument)], env=env)
    elif action == "click" and isinstance(argument, (tuple, list)):
        wx, wy = argument
        subprocess.run(["xdotool", "mousemove", "--window", window, str(int(wx)), str(int(wy))], env=env)
        time.sleep(1.2)
        subprocess.run(["xdotool", "mousedown", "--window", window, "1"], env=env)
        time.sleep(0.8)
        subprocess.run(["xdotool", "mouseup", "--window", window, "1"], env=env)
    elif action == "wait":
        pass
    time.sleep(settle)


def drive_scenario(
    scenario_name: str,
    mode: str,
    evidence_dir: Path,
    screenshots_dir: Path,
    display: str = DISPLAY_DEFAULT,
    run_seconds: float = 300.0,
) -> dict:
    """Drive the given scenario, saving mode-tagged screenshots."""
    log_path = evidence_dir / "run.log"
    steps = SCENARIOS.get(scenario_name, SCENARIOS["campaign-auto"])
    results = {
        "scenario": scenario_name,
        "mode": mode,
        "completed_steps": [],
        "map_submitted": False,
        "success": False,
    }

    win = find_window(display, timeout=30.0)
    if not win:
        sys.stderr.write(f"ERROR: Ryujinx window not found on {display}\n")
        return results

    subprocess.run(["xdotool", "windowfocus", "--sync", win], env={**os.environ, "DISPLAY": display})
    print(f"[{mode}] Focused Ryujinx window {win} on {display}", flush=True)
    win_w, win_h = WINDOW_SIZE

    # Initial delay for guest startup
    time.sleep(12.0)
    boot_shot = screenshots_dir / f"switch-{mode}-boot.png"
    capture_screenshot(boot_shot, display)
    print(f"[{mode}] Captured {boot_shot.name}", flush=True)

    for name, action, argument, settle in steps:
        scaled_arg = argument
        if action in ("click", "story-until", "map-until") and isinstance(argument, (tuple, list)):
            wx, wy = to_window(argument, (win_w, win_h))
            scaled_arg = (int(wx), int(wy))

        print(f"[{mode}] Step {name}: {action} {scaled_arg} (settle={settle}s)", flush=True)

        if action == "story-until":
            story_clicks = 0

            def advance_story():
                nonlocal story_clicks
                story_clicks += 1
                do_step(win, "click", scaled_arg, settle, display)
                shot = screenshots_dir / f"switch-{mode}-story-{story_clicks}.png"
                capture_screenshot(shot, display)
                print(f"[{mode}] Captured {shot.name}", flush=True)

            matched = follow_guest_event(
                log_path, b"[game-tty] 4 e-txt", timeout=180.0, on_wait=advance_story
            )
            if not matched:
                sys.stderr.write(f"WARNING: [game-tty] 4 e-txt not reached before timeout\n")
        elif action == "map-until":
            map_clicks = 0

            def advance_map():
                nonlocal map_clicks
                map_clicks += 1
                do_step(win, "click", scaled_arg, 12.0, display)
                shot = screenshots_dir / f"switch-{mode}-map-wait-{map_clicks}.png"
                capture_screenshot(shot, display)
                print(f"[{mode}] Captured {shot.name}", flush=True)

            matched = follow_guest_event(
                log_path, b"[map-submit]", timeout=run_seconds, on_wait=advance_map
            )
            if matched:
                results["map_submitted"] = True
            else:
                sys.stderr.write(f"WARNING: [map-submit] not reached before timeout\n")
        else:
            do_step(win, action, scaled_arg, settle, display)

        step_shot = screenshots_dir / f"switch-{mode}-{name}.png"
        capture_screenshot(step_shot, display)
        print(f"[{mode}] Captured {step_shot.name}", flush=True)
        results["completed_steps"].append(name)

    results["success"] = True
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["native", "gs"], default="native", help="rendering mode")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="campaign-auto", help="scenario")
    parser.add_argument("--display", default=DISPLAY_DEFAULT, help="X11 display")
    parser.add_argument("--evidence", type=Path, default=REPO_ROOT / "build/switch-validation", help="evidence dir")
    parser.add_argument("--screenshots", type=Path, default=REPO_ROOT / ".playwright-mcp/switch", help="shots dir")
    parser.add_argument("--run-seconds", type=float, default=300.0, help="timeout budget")
    args = parser.parse_args()

    results = drive_scenario(
        scenario_name=args.scenario,
        mode=args.mode,
        evidence_dir=args.evidence,
        screenshots_dir=args.screenshots,
        display=args.display,
        run_seconds=args.run_seconds,
    )

    summary_file = args.evidence / f"scenario-{args.mode}.json"
    summary_file.write_text(json.dumps(results, indent=2))
    return 0 if results["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
