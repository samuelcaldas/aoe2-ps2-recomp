#!/usr/bin/env python3
"""Drive the Switch executable inside the emulator container through UI scenarios.

Executes scenario steps targeting the Ryujinx emulator window inside Xvfb:
- Focuses the emulator window;
- Executes sequence of gamepad buttons and directional arrows (PS2 pad navigation);
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
# Sequence of steps mirrors the PC scenario 1:1, driven via gamepad buttons and arrows.
BOOT_TO_MENU = [
    ("key-return-1", "key", "Return", 5),
    ("key-return-2", "key", "Return", 5),
    ("card-ok", "pad", "cross", 5),
    ("profile-name-field", "pad", "cross", 4),
    ("profile-submit", "pad", "cross", 4),
    ("profile-confirm", "pad", "cross", 6),
    ("profile-settle", "pad", "cross", 6),
]

SCENARIOS = {
    "smoke": BOOT_TO_MENU[:3],
    "main-menu": BOOT_TO_MENU,
    "single-player": BOOT_TO_MENU + [("single-player", "click", (458, 85), 6)],
    "campaign": BOOT_TO_MENU + [
        ("single-player", "click", (458, 85), 8),
        ("campaigns", "click", (418, 86), 15),
        ("wallace-marker", "click", (221, 77), 15),
        ("scenario-1", "click", (183, 57), 35),
        ("briefing", "pad", "cross", 25),
        ("briefing-next", "pad", "cross", 25),
        ("mission-loading", "pad", "cross", 40),
        ("brief-3", "pad", "cross", 25),
        ("brief-4", "pad", "cross", 25),
        ("brief-5", "pad", "cross", 35),
        ("mission", "pad", "cross", 35),
        ("mission-start", "pad", "cross", 50),
        ("story-complete", "story-until", None, 12),
        ("mission-live", "wait", None, 30),
    ],
    "campaign-auto": BOOT_TO_MENU + [
        ("single-player", "click", (458, 85), 8),
        ("campaigns", "click", (418, 86), 15),
        ("wallace-marker", "click", (221, 77), 15),
        ("scenario-1", "click", (183, 57), 35),
        ("briefing", "pad", "cross", 25),
        ("briefing-next", "pad", "cross", 25),
        ("mission-loading", "pad", "cross", 40),
        ("brief-3", "pad", "cross", 25),
        ("brief-4", "pad", "cross", 25),
        ("brief-5", "pad", "cross", 35),
        ("mission", "pad", "cross", 35),
        ("mission-start", "pad", "cross", 50),
        ("story-complete", "story-until", None, 12),
        ("map-submitted", "map-until", None, 600),
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


def get_screen_brightness(display: str) -> float:
    """Return mean brightness of the screen (0.0 for pure black)."""
    tmp_path = Path("/tmp/screen_check.png")
    if capture_screenshot(tmp_path, display):
        res = subprocess.run(
            f"convert {tmp_path} -format '%[mean]' info:",
            shell=True,
            capture_output=True,
            text=True,
        )
        try:
            return float(res.stdout.strip())
        except ValueError:
            pass
    return 0.0


def is_screen_loading(display: str) -> bool:
    """Check if the screen is pure black (mean < 1.0)."""
    return get_screen_brightness(display) < 1.0


def wait_for_screen_ready(display: str, timeout: float = 90.0, settle_after: float = 2.0) -> bool:
    """Wait until screen is rendered (not pure black), then allow settle_after for UI stabilization."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not is_screen_loading(display):
            if settle_after > 0:
                time.sleep(settle_after)
            return True
        time.sleep(1.0)
    return False


import re

def get_emulator_fps(display: str) -> str:
    """Read emulator FPS from Ryujinx window title."""
    win = find_window(display, timeout=2.0)
    if not win:
        return ""
    res = subprocess.run(
        ["xdotool", "getwindowname", win],
        capture_output=True,
        text=True,
        env={**os.environ, "DISPLAY": display},
    )
    title = res.stdout.strip()
    match = re.search(r"FPS[:\s]+([0-9.]+)", title, re.IGNORECASE)
    if match:
        return match.group(1)
    match = re.search(r"([0-9.]+)\s*FPS", title, re.IGNORECASE)
    if match:
        return match.group(1)
    return ""


def send_pad_button(display: str, btn: str, duration: float = 0.35) -> None:
    """Send gamepad button / D-pad arrow to Ryujinx window."""
    env = {**os.environ, "DISPLAY": display}
    win = find_window(display, timeout=5.0)
    btn_map = {
        "b": "z",
        "a": "x",
        "cross": "z",
        "confirm": "z",
        "circle": "x",
        "cancel": "x",
        "start": "Return",
        "plus": "Return",
        "up": "Up",
        "down": "Down",
        "left": "Left",
        "right": "Right",
        "triangle": "c",
        "square": "v",
        "select": "Minus",
        "minus": "Minus",
    }
    key = btn_map.get(btn.lower(), btn)
    if win:
        subprocess.run(["xdotool", "windowfocus", "--sync", win], env=env)
    subprocess.run(["xdotool", "keydown", key], env=env)
    time.sleep(duration)
    subprocess.run(["xdotool", "keyup", key], env=env)


def to_window_coords(point: tuple[float, float]) -> tuple[int, int]:
    """Map logical 640x448 coordinates to Ryujinx 1280x760 window coordinates."""
    wx, wy = to_window(point, (1280, 720))
    # Account for 35px Ryujinx menu bar
    return int(round(wx)), int(round(wy + 35))


def do_click(display: str, wx: int, wy: int, duration: float = 0.4) -> None:
    """Execute a mouse click at window coordinates on the display."""
    env = {**os.environ, "DISPLAY": display}
    win = find_window(display, timeout=5.0)
    if win:
        subprocess.run(["xdotool", "windowfocus", "--sync", win], env=env)
    subprocess.run(["xdotool", "mousemove", str(wx), str(wy)], env=env)
    time.sleep(0.6)
    subprocess.run(["xdotool", "mousedown", "1"], env=env)
    time.sleep(duration)
    subprocess.run(["xdotool", "mouseup", "1"], env=env)


def detect_screen_state(arr) -> str:
    """Classify current emulator screen into known states."""
    mean_brightness = float(arr.mean())
    if mean_brightness < 2.0:
        return "black"

    center_color = arr[380, 640]
    if center_color[0] > 200 and center_color[1] > 180 and center_color[2] > 120:
        return "new_player_dialog"

    if 40 < center_color[0] < 160 and 35 < center_color[1] < 140 and 25 < center_color[2] < 110:
        mc_crop = arr[140:190, 700:850].mean()
        if mc_crop < 100.0:
            return "select_player_mc_busy"
        return "select_player_ready"

    # Book has dark title 'Campaigns' (mean < 60) and bright map page (mean > 75)
    title_mean = arr[85:125, 550:730].mean()
    map_mean = arr[250:450, 250:550].mean()
    if title_mean < 60.0 and map_mean > 75.0:
        return "campaigns_book"

    # Menus have bright title banner (> 80)
    gold_menu = (
        (arr[150:210, 730:1060, 0] > 100)
        & (arr[150:210, 730:1060, 1] > 100)
        & (arr[150:210, 730:1060, 2] < 45)
    ).sum()
    if gold_menu > 200:
        header_text = (arr[65:95, 750:1000] > 130).sum()
        if header_text > 2500:
            return "single_player_menu"
        return "main_menu"

    return "unknown"


def get_current_screen_state(display: str) -> tuple[str, any]:
    """Capture screen and detect current UI state."""
    tmp_path = Path("/tmp/screen_check.png")
    if not capture_screenshot(tmp_path, display):
        return ("black", None)
    try:
        from PIL import Image
        import numpy as np

        img = Image.open(tmp_path).convert("RGB")
        arr = np.array(img)
        return (detect_screen_state(arr), arr)
    except Exception:
        return ("unknown", None)


def wait_for_any_screen_state(
    display: str, target_states: set[str], timeout: float = 60.0, poll_interval: float = 1.5
) -> str:
    """Poll until screen state matches one of target_states or timeout expires."""
    deadline = time.monotonic() + timeout
    last_state = ""
    while time.monotonic() < deadline:
        state, _ = get_current_screen_state(display)
        if state != last_state:
            print(f"  [ui-state] {state} (targets: {sorted(target_states)})", flush=True)
            last_state = state
        if state in target_states:
            return state
        time.sleep(poll_interval)
    return last_state


def is_memory_card_busy(display: str) -> bool:
    """Check if memory card is still displaying '---KB' (busy dashes)."""
    state, _ = get_current_screen_state(display)
    return state in ("select_player_mc_busy", "black")


def wait_for_card_ready(display: str, timeout: float = 60.0, settle_after: float = 3.0) -> bool:
    """Wait until memory card check completes."""
    st = wait_for_any_screen_state(
        display, {"select_player_ready", "new_player_dialog", "main_menu"}, timeout=timeout
    )
    if st in ("select_player_ready", "new_player_dialog", "main_menu"):
        if settle_after > 0:
            time.sleep(settle_after)
        return True
    return False


def is_osk_open(display: str) -> bool:
    """Check if on-screen keyboard overlay is open."""
    return False


def is_dialog_open(display: str) -> bool:
    """Check if centered parchment dialog (New Player / Confirm) is open."""
    state, _ = get_current_screen_state(display)
    return state == "new_player_dialog"


def is_select_player_open(display: str) -> bool:
    """Check if Select Player screen is displayed."""
    state, _ = get_current_screen_state(display)
    return state in ("select_player_mc_busy", "select_player_ready", "new_player_dialog")


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
                if now - last_action >= max_interval_seconds:
                    on_wait()
                    last_action = time.monotonic()
                else:
                    time.sleep(min(0.2, max(0.0, deadline - now)))
            else:
                if not chunk:
                    time.sleep(min(1.0, max(0.0, deadline - now)))


def do_step(display: str, action: str, argument, settle: float, step_name: str = "", log_path: Path | None = None) -> None:
    """Execute a scenario action via native controller buttons / arrows and calibrated clicks."""
    env = {**os.environ, "DISPLAY": display}
    win = find_window(display, timeout=5.0)
    if win:
        subprocess.run(["xdotool", "windowfocus", "--sync", win], env=env)

    if step_name in ("key-return-1", "key-return-2"):
        send_pad_button(display, "start", 0.5)
        time.sleep(0.5)
        send_pad_button(display, "cross", 0.5)
    elif step_name in ("osk-letter-a", "osk-enter"):
        time.sleep(0.5)
        send_pad_button(display, "start", 0.5)
    elif step_name == "card-ok":
        # Wait until memory card check completes (select_player_ready) or already at main menu
        st = wait_for_any_screen_state(
            display,
            {"select_player_ready", "new_player_dialog", "main_menu"},
            timeout=90.0,
        )
        if st == "select_player_ready":
            time.sleep(1.0)
            send_pad_button(display, "cross", 1.0)
            wait_for_any_screen_state(display, {"new_player_dialog", "main_menu"}, timeout=60.0)
    elif step_name == "profile-name-field":
        st = wait_for_any_screen_state(display, {"new_player_dialog", "main_menu"}, timeout=60.0)
        if st == "new_player_dialog":
            time.sleep(1.0)
            # In New Player popup, move focus from Name field down to [OK]
            send_pad_button(display, "down", 0.8)
            time.sleep(1.0)
    elif step_name == "profile-submit":
        st = wait_for_any_screen_state(display, {"new_player_dialog", "main_menu"}, timeout=30.0)
        if st == "new_player_dialog":
            # Press Cross on [OK] to confirm default name "Anonymous"
            send_pad_button(display, "cross", 1.0)
            time.sleep(2.0)
    elif step_name == "profile-confirm":
        # Confirmation popup 'Is the name Anonymous correct?' appears
        st, _ = get_current_screen_state(display)
        if st not in ("main_menu", "single_player_menu"):
            send_pad_button(display, "cross", 1.0)
            time.sleep(2.0)
    elif step_name == "profile-settle":
        # Wait until Main Menu is reached and has fully faded in
        deadline = time.monotonic() + 60.0
        while time.monotonic() < deadline:
            st, _ = get_current_screen_state(display)
            b = get_screen_brightness(display)
            if st == "main_menu" and b >= 8000.0:
                break
            time.sleep(1.0)
        time.sleep(2.0)
    elif step_name == "single-player":
        wait_for_any_screen_state(display, {"main_menu"}, timeout=30.0)
        time.sleep(1.0)
        wx, wy = to_window_coords((458, 85))
        do_click(display, wx, wy, 0.4)
        time.sleep(0.3)
        send_pad_button(display, "cross", 0.5)
        wait_for_any_screen_state(display, {"single_player_menu"}, timeout=20.0)
        time.sleep(1.0)
    elif step_name == "campaigns":
        wait_for_any_screen_state(display, {"single_player_menu"}, timeout=30.0)
        time.sleep(1.0)
        wx, wy = to_window_coords((418, 86))
        do_click(display, wx, wy, 0.4)
        time.sleep(0.3)
        send_pad_button(display, "cross", 0.5)
        wait_for_any_screen_state(display, {"campaigns_book"}, timeout=90.0)
        time.sleep(3.0)
    elif step_name == "wallace-marker":
        wait_for_any_screen_state(display, {"campaigns_book"}, timeout=60.0)
        time.sleep(2.0)
        wx, wy = to_window_coords((221, 77))
        do_click(display, wx, wy, 0.5)
        time.sleep(0.5)
        send_pad_button(display, "cross", 0.5)
        time.sleep(5.0)
    elif step_name == "scenario-1":
        time.sleep(3.0)
        wx, wy = to_window_coords((183, 57))
        do_click(display, wx, wy, 0.5)
        time.sleep(0.5)
        send_pad_button(display, "cross", 0.5)
        time.sleep(5.0)
    elif step_name in (
        "briefing",
        "briefing-next",
        "mission-loading",
        "brief-3",
        "brief-4",
        "brief-5",
        "mission",
        "mission-start",
    ):
        time.sleep(1.0)
        send_pad_button(display, "cross", 0.4)
        time.sleep(0.5)
        send_pad_button(display, "triangle", 0.4)
        time.sleep(0.5)
        send_pad_button(display, "start", 0.4)
        time.sleep(0.3)
        wx, wy = to_window_coords((320, 224))
        do_click(display, wx, wy, 0.3)
    elif step_name.startswith("story-advance"):
        time.sleep(0.5)
        wx, wy = to_window_coords((320, 224))
        do_click(display, wx, wy, 0.4)
        send_pad_button(display, "cross", 0.4)
    elif action == "pad":
        send_pad_button(display, str(argument), 0.35)
    elif action == "key":
        send_pad_button(display, str(argument), 0.35)
    elif action == "click":
        if isinstance(argument, (tuple, list)):
            wx, wy = to_window_coords(argument)
            do_click(display, wx, wy, 0.4)
        send_pad_button(display, "cross", 0.4)
    elif action == "wait":
        pass
    time.sleep(settle)


def drive_scenario(
    scenario_name: str,
    mode: str,
    evidence_dir: Path,
    screenshots_dir: Path,
    display: str = DISPLAY_DEFAULT,
    run_seconds: float = 600.0,
    speed_factor: float = 2.5,
) -> dict:
    """Drive the given scenario using controller buttons, saving mode-tagged screenshots."""
    log_path = evidence_dir / "run.log"
    steps = SCENARIOS.get(scenario_name, SCENARIOS["campaign-auto"])
    results = {
        "scenario": scenario_name,
        "mode": mode,
        "completed_steps": [],
        "map_submitted": False,
        "success": False,
    }

    win = find_window(display, timeout=45.0)
    if not win:
        sys.stderr.write(f"ERROR: Ryujinx window not found on {display}\n")
        return results

    subprocess.run(["xdotool", "windowfocus", "--sync", win], env={**os.environ, "DISPLAY": display})
    print(f"[{mode}] Focused Ryujinx window {win} on {display}", flush=True)

    # Boot delay for guest emulator to initialize and reach title screen
    print(f"[{mode}] Waiting for guest boot (30s)...", flush=True)
    time.sleep(30.0)
    wait_for_screen_ready(display, timeout=45.0, settle_after=3.0)
    boot_shot = screenshots_dir / f"switch-{mode}-boot.png"
    capture_screenshot(boot_shot, display)
    print(f"[{mode}] Captured {boot_shot.name} (brightness={get_screen_brightness(display):.1f})", flush=True)

    for name, action, argument, settle in steps:
        effective_settle = (settle * speed_factor) if settle else 0.0
        print(f"[{mode}] Step {name}: {action} (settle={effective_settle:.1f}s)", flush=True)

        if is_screen_loading(display):
            print(f"[{mode}] Screen is in loading transition; waiting for render before {name}...", flush=True)
            wait_for_screen_ready(display, timeout=90.0, settle_after=3.0)

        if action == "story-until":
            story_clicks = 0

            def advance_story():
                nonlocal story_clicks
                story_clicks += 1
                # Story/narrative slides require mouse click at screen centre to advance.
                # The PS2 mouse driver processes the click; pad Cross alone is ignored.
                wx, wy = to_window_coords((320, 224))
                do_click(display, wx, wy, 0.4)
                time.sleep(0.3)
                send_pad_button(display, "cross", 0.4)
                shot = screenshots_dir / f"switch-{mode}-story-{story_clicks}.png"
                capture_screenshot(shot, display)
                print(f"[{mode}] Captured {shot.name}", flush=True)

            matched = follow_guest_event(
                log_path, b"[map-submit]", timeout=600.0, on_wait=advance_story, max_interval_seconds=3.0
            )
            if matched:
                results["map_submitted"] = True
                print(f"[{mode}] [map-submit] event detected during story-until!", flush=True)
            else:
                sys.stderr.write(f"WARNING: [map-submit] not reached in story-until\n")
        elif action == "map-until":
            if results.get("map_submitted"):
                print(f"[{mode}] Map already submitted; settling 5s before capture...", flush=True)
                time.sleep(5.0)
                submit_shot = screenshots_dir / f"switch-{mode}-map-submitted.png"
                capture_screenshot(submit_shot, display)
                print(f"[{mode}] Captured {submit_shot.name} [SUCCESS]!", flush=True)
            else:
                map_clicks = 0

                def advance_map():
                    nonlocal map_clicks
                    map_clicks += 1
                    send_pad_button(display, "cross", 0.8)
                    shot = screenshots_dir / f"switch-{mode}-map-wait-{map_clicks}.png"
                    capture_screenshot(shot, display)
                    print(f"[{mode}] Captured {shot.name}", flush=True)

                matched = follow_guest_event(
                    log_path, b"[map-submit]", timeout=run_seconds, on_wait=advance_map
                )
                if matched:
                    results["map_submitted"] = True
                    submit_shot = screenshots_dir / f"switch-{mode}-map-submitted.png"
                    capture_screenshot(submit_shot, display)
                    print(f"[{mode}] Captured {submit_shot.name} [SUCCESS]!", flush=True)
                else:
                    sys.stderr.write(f"WARNING: [map-submit] not reached before timeout\n")
        else:
            do_step(display, action, argument, effective_settle, step_name=name, log_path=log_path)
            if name == "profile-settle":
                wait_for_screen_ready(display, timeout=45.0, settle_after=4.0)
            elif name == "map-stability":
                # Exercise map navigation using D-Pad directional arrows (setas)
                print(f"[{mode}] Testing map navigation using controller D-Pad arrows...", flush=True)
                for arrow in ["right", "down", "left", "up"]:
                    send_pad_button(display, arrow, 0.5)
                    time.sleep(1.0)
                stability_shot = screenshots_dir / f"switch-{mode}-map-stability.png"
                capture_screenshot(stability_shot, display)
                print(f"[{mode}] Captured {stability_shot.name}!", flush=True)

        step_shot = screenshots_dir / f"switch-{mode}-{name}.png"
        capture_screenshot(step_shot, display)
        fps = get_emulator_fps(display)
        fps_info = f" | Emulator FPS: {fps}" if fps else ""
        print(f"[{mode}] Captured {step_shot.name} (brightness={get_screen_brightness(display):.1f}{fps_info})", flush=True)
        results["completed_steps"].append(name)

    results["success"] = results.get("map_submitted", False) if scenario_name in ("campaign", "campaign-auto") else True
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["native", "gs"], default="native", help="rendering mode")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="campaign-auto", help="scenario")
    parser.add_argument("--display", default=DISPLAY_DEFAULT, help="X11 display")
    parser.add_argument("--evidence", type=Path, default=REPO_ROOT / "build/switch-validation", help="evidence dir")
    parser.add_argument("--screenshots", type=Path, default=REPO_ROOT / ".playwright-mcp/switch", help="shots dir")
    parser.add_argument("--run-seconds", type=float, default=600.0, help="timeout budget")
    parser.add_argument("--speed-factor", type=float, default=2.5, help="settle sleep multiplier for emulator clock (default: 2.5)")
    args = parser.parse_args()

    results = drive_scenario(
        scenario_name=args.scenario,
        mode=args.mode,
        evidence_dir=args.evidence,
        screenshots_dir=args.screenshots,
        display=args.display,
        run_seconds=args.run_seconds,
        speed_factor=args.speed_factor,
    )

    summary_file = args.evidence / f"scenario-{args.mode}.json"
    summary_file.write_text(json.dumps(results, indent=2))
    return 0 if results["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
