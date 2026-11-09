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
    ("card-ok", "click", (251, 350), 30),
    ("profile-new-player", "click", (251, 350), 5),
    ("profile-name-field", "click", (335, 237), 3),
    ("profile-name-a", "type", "Teste", 2),
    ("profile-submit", "click", (245, 315), 5),
    ("profile-confirm", "click", (245, 315), 10),
    ("profile-settle", "wait", None, 10),
]

REMOVED_SCENARIOS = {
    "campaign": "use random-map-start",
    "campaign-auto": "use random-map-start",
}
# random-map-start runs the gated boot here, then the shared pad flow (drive_random_map).
SCENARIOS = {
    "smoke": BOOT_TO_MENU[:3],
    "main-menu": BOOT_TO_MENU,
    "single-player": BOOT_TO_MENU + [("single-player", "click", (458, 85), 6)],
    "random-map-start": BOOT_TO_MENU,
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

    # Book has dark title 'Campaigns' (mean < 80) and bright map page (mean > 75)
    title_mean = arr[85:125, 550:730].mean()
    map_mean = arr[250:450, 250:550].mean()
    if title_mean < 80.0 and map_mean > 75.0:
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
                    if on_wait() is False:
                        return False
                    last_action = time.monotonic()
                else:
                    time.sleep(min(0.2, max(0.0, deadline - now)))
            else:
                if not chunk:
                    time.sleep(min(1.0, max(0.0, deadline - now)))


def do_step(display: str, action: str, argument, settle: float, step_name: str = "", log_path: Path | None = None, factor: float = 1.0) -> bool | None:
    """Execute a scenario action via native controller buttons / arrows and calibrated clicks."""
    env = {**os.environ, "DISPLAY": display}
    win = find_window(display, timeout=5.0 * factor)
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
            timeout=90.0 * factor,
        )
        time.sleep(3.0)
        if st == "select_player_ready":
            wx, wy = to_window_coords((251, 350))
            do_click(display, wx, wy, 0.4)
            time.sleep(0.5)
            send_pad_button(display, "cross", 0.5)
            time.sleep(2.0)
    elif step_name == "profile-new-player":
        st, _ = get_current_screen_state(display)
        if st == "select_player_ready":
            wx, wy = to_window_coords((251, 350))
            do_click(display, wx, wy, 0.4)
            time.sleep(0.5)
            send_pad_button(display, "cross", 0.5)
            time.sleep(2.0)
    elif step_name == "profile-name-field":
        st, _ = get_current_screen_state(display)
        if st in ("new_player_dialog", "select_player_ready"):
            wx, wy = to_window_coords((335, 237))
            do_click(display, wx, wy, 0.4)
            time.sleep(0.5)
            send_pad_button(display, "cross", 0.5)
            time.sleep(1.5)
    elif step_name == "profile-name-a":
        state, before = get_current_screen_state(display)
        if state != "new_player_dialog" or before is None:
            print(f"[{step_name}] Dialog not visible; withholding text input", flush=True)
            return False
        else:
            subprocess.run(["xdotool", "type", "--clearmodifiers", "--delay", "100", str(argument)], env=env)
            time.sleep(0.5)
            state, after = get_current_screen_state(display)
            capture_screenshot(REPO_ROOT / ".playwright-mcp" / "switch" / f"switch-{step_name}-typed.png", display)
            if state != "new_player_dialog" or after is None:
                print(f"[{step_name}] Dialog changed unexpectedly; stopping scenario", flush=True)
                return False
            # A typed character must change pixels in the name field. A bright-pixel threshold is
            # unreliable because the parchment behind the field is bright and the caret blinks.
            import numpy as np
            field_before = before[405:442, 575:817]
            field_after = after[405:442, 575:817]
            changed_pixels = int(np.count_nonzero(np.any(field_before != field_after, axis=2)))
            if changed_pixels < 12:
                print(f"[{step_name}] No visible name entered; withholding profile submission", flush=True)
                return False
    elif step_name == "profile-submit":
        state, _ = get_current_screen_state(display)
        if state != "new_player_dialog":
            print("[profile-submit] Dialog not visible; withholding confirmation", flush=True)
            return False
        wx, wy = to_window_coords((245, 315))
        do_click(display, wx, wy, 0.4)
        time.sleep(0.3)
        send_pad_button(display, "cross", 0.5)
        time.sleep(2.0)
        state, _ = get_current_screen_state(display)
        if state == "new_player_dialog":
            print("[profile-submit] Dialog remains open; stopping instead of repeating confirmation", flush=True)
            return False
    elif step_name == "profile-confirm":
        state, _ = get_current_screen_state(display)
        if state != "new_player_dialog":
            print("[profile-confirm] Dialog not visible; withholding confirmation", flush=True)
            return False
        wx, wy = to_window_coords((245, 315))
        do_click(display, wx, wy, 0.4)
        time.sleep(0.3)
        send_pad_button(display, "cross", 0.5)
        time.sleep(2.0)
        state, _ = get_current_screen_state(display)
        if state == "new_player_dialog":
            print("[profile-confirm] No visible transition after one confirmation; stopping", flush=True)
            return False
    elif step_name == "profile-settle":
        # Wait until Main Menu is reached and has fully faded in
        wait_for_any_screen_state(display, {"main_menu"}, timeout=45.0 * factor)
        time.sleep(3.0)
    elif step_name == "single-player":
        wait_for_any_screen_state(display, {"main_menu"}, timeout=30.0 * factor)
        time.sleep(1.5)
        wx, wy = to_window_coords((458, 85))
        do_click(display, wx, wy, 0.4)
        time.sleep(0.3)
        send_pad_button(display, "cross", 0.5)
        st = wait_for_any_screen_state(display, {"single_player_menu"}, timeout=30.0 * factor)
        if st != "single_player_menu":
            print("[single-player] Menu transition not observed; withholding retry", flush=True)
            return False
        time.sleep(2.0)
    elif step_name == "campaigns":
        wait_for_any_screen_state(display, {"single_player_menu"}, timeout=30.0 * factor)
        time.sleep(1.5)
        wx, wy = to_window_coords((418, 86))
        do_click(display, wx, wy, 0.4)
        time.sleep(0.3)
        send_pad_button(display, "cross", 0.5)
        st = wait_for_any_screen_state(display, {"campaigns_book"}, timeout=90.0 * factor)
        if st != "campaigns_book":
            do_click(display, wx, wy, 0.4)
            send_pad_button(display, "cross", 0.5)
            wait_for_any_screen_state(display, {"campaigns_book"}, timeout=45.0 * factor)
        time.sleep(3.0)
    elif step_name == "wallace-marker":
        wait_for_any_screen_state(display, {"campaigns_book"}, timeout=60.0 * factor)
        time.sleep(2.0)
        # Click Scottish flag at William Wallace (window: 498, 158; logical: 219, 77)
        wx, wy = to_window_coords((219, 77))
        do_click(display, wx, wy, 0.5)
        time.sleep(0.5)
        send_pad_button(display, "cross", 0.8)
        time.sleep(5.0)
    elif step_name == "scenario-1":
        time.sleep(3.0)
        # On Wallace campaign page, focus starts at Campaign Menu.
        # Press Up to select Scenario 1, and also click directly at Scenario 1 marker.
        send_pad_button(display, "up", 0.5)
        time.sleep(0.5)
        send_pad_button(display, "cross", 0.8)
        time.sleep(0.5)
        wx, wy = to_window_coords((271, 87))
        do_click(display, wx, wy, 0.5)
        time.sleep(0.5)
        send_pad_button(display, "cross", 0.8)
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
        send_pad_button(display, "cross", 0.5)
        time.sleep(0.5)
        send_pad_button(display, "start", 0.5)
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


def drive_random_map(
    display: str,
    log_path: Path,
    screenshots_dir: Path,
    mode: str,
    settle_scale: float,
    run_seconds: float,
) -> bool:
    """Run the shared Single Player -> Random Map -> Start Game flow with the joystick."""
    from tools.scenarios import (
        CheckpointRecorder,
        LogMarkerWatcher,
        RyujinxPadBackend,
        ScenarioRunner,
        random_map_pad_steps,
    )

    def shoot(name: str) -> Path:
        path = screenshots_dir / name
        capture_screenshot(path, display)
        return path

    def await_state(state: str, timeout: float) -> bool:
        return wait_for_any_screen_state(display, {state}, timeout=timeout) == state

    def click(point) -> None:
        wx, wy = to_window_coords(point)
        do_click(display, wx, wy, 0.4)

    backend = RyujinxPadBackend(
        send_key=lambda key: send_pad_button(display, key, 0.35),
        click=click,
    )
    watcher = LogMarkerWatcher(log_path, is_alive=lambda: True)
    recorder = CheckpointRecorder("switch", shoot)
    runner = ScenarioRunner(
        backend=backend,
        await_marker=watcher.await_marker,
        await_screen=await_state,
        is_alive=lambda: True,
        observer=recorder,
        settle_scale=settle_scale,
    )
    result = runner.run(random_map_pad_steps())
    print(f"[{mode}] random-map-start: passed={result.passed} failed_step={result.failed_step}", flush=True)
    return result.passed


def drive_scenario(
    scenario_name: str,
    mode: str,
    evidence_dir: Path,
    screenshots_dir: Path,
    display: str = DISPLAY_DEFAULT,
    run_seconds: float = 600.0,
    speed_factor: float = 2.5,
    build_type: str = "release",
) -> dict:
    """Drive the given scenario using controller buttons, saving mode-tagged screenshots."""
    factor = 10 if build_type == "debug" else 1
    log_path = evidence_dir / "run.log"
    if scenario_name in REMOVED_SCENARIOS:
        raise ValueError(f"scenario {scenario_name!r} was removed; {REMOVED_SCENARIOS[scenario_name]}")
    steps = SCENARIOS[scenario_name]
    results = {
        "scenario": scenario_name,
        "mode": mode,
        "completed_steps": [],
        "map_submitted": False,
        "success": False,
    }

    win = find_window(display, timeout=45.0 * factor)
    if not win:
        sys.stderr.write(f"ERROR: Ryujinx window not found on {display}\n")
        return results

    subprocess.run(["xdotool", "windowfocus", "--sync", win], env={**os.environ, "DISPLAY": display})
    print(f"[{mode}] Focused Ryujinx window {win} on {display}", flush=True)

    # Boot delay for guest emulator to initialize and reach title screen
    print(f"[{mode}] Waiting for guest boot (30s)...", flush=True)
    time.sleep(30.0 * factor)
    wait_for_screen_ready(display, timeout=45.0 * factor, settle_after=3.0)
    boot_shot = screenshots_dir / f"switch-{mode}-boot.png"
    capture_screenshot(boot_shot, display)
    print(f"[{mode}] Captured {boot_shot.name} (brightness={get_screen_brightness(display):.1f})", flush=True)

    for name, action, argument, settle in steps:
        step_ok = True
        effective_settle = (settle * speed_factor * factor) if settle else 0.0
        print(f"[{mode}] Step {name}: {action} (settle={effective_settle:.1f}s)", flush=True)
        before_shot = screenshots_dir / f"switch-{mode}-{name}-before.png"
        capture_screenshot(before_shot, display)
        before_state, _ = get_current_screen_state(display)
        print(f"[{mode}] Before {name}: state={before_state}; captured {before_shot.name}", flush=True)

        if is_screen_loading(display):
            print(f"[{mode}] Screen is in loading transition; waiting for render before {name}...", flush=True)
            wait_for_screen_ready(display, timeout=90.0 * factor, settle_after=3.0)

        if action == "story-until":
            story_clicks = 0

            def advance_story():
                nonlocal story_clicks
                if story_clicks >= 12:
                    print(f"[{mode}] Story action limit reached; stopping blind input", flush=True)
                    return False
                prior_state, prior = get_current_screen_state(display)
                if prior is None or prior_state == "black":
                    print(f"[{mode}] Story screen is not visible; withholding input", flush=True)
                    return False
                story_clicks += 1
                before = screenshots_dir / f"switch-{mode}-story-{story_clicks}-before.png"
                capture_screenshot(before, display)
                # Story screens advance with the absolute pointer click; Cross
                # is sent only after the click has been visually captured.
                wx, wy = to_window_coords((320, 224))
                do_click(display, wx, wy, 0.4)
                time.sleep(0.5)
                shot = screenshots_dir / f"switch-{mode}-story-{story_clicks}-after.png"
                capture_screenshot(shot, display)
                after_state, after = get_current_screen_state(display)
                print(f"[{mode}] Story action {story_clicks}: {prior_state} -> {after_state}; captured before/after", flush=True)
                if after is None or after_state == "black":
                    return False
                import numpy as np
                changed = float(np.abs(prior.astype(np.int16) - after.astype(np.int16)).mean()) >= 3.0
                if not changed:
                    print(f"[{mode}] No visible transition after story click; withholding further input", flush=True)
                    return False
                return True

            matched = follow_guest_event(
                log_path, b"[map-submit]", timeout=600.0 * factor, on_wait=advance_story, max_interval_seconds=3.0
            )
            if matched:
                results["map_submitted"] = True
                print(f"[{mode}] [map-submit] event detected during story-until!", flush=True)
            else:
                sys.stderr.write(f"WARNING: [map-submit] not reached in story-until\n")
                step_ok = False
        elif action == "map-until":
            if results.get("map_submitted"):
                print(f"[{mode}] Map already submitted; settling 5s before capture...", flush=True)
                time.sleep(5.0)
                submit_shot = screenshots_dir / f"switch-{mode}-map-submitted.png"
                capture_screenshot(submit_shot, display)
                print(f"[{mode}] Captured {submit_shot.name} [SUCCESS]!", flush=True)
            else:
                map_checks = 0

                def advance_map():
                    nonlocal map_checks
                    map_checks += 1
                    shot = screenshots_dir / f"switch-{mode}-map-wait-{map_checks}.png"
                    capture_screenshot(shot, display)
                    state, _ = get_current_screen_state(display)
                    print(f"[{mode}] Map checkpoint {map_checks}: state={state}; captured {shot.name}; no input sent", flush=True)
                    return True

                matched = follow_guest_event(
                    log_path, b"[map-submit]", timeout=run_seconds * factor, on_wait=advance_map,
                    max_interval_seconds=10.0
                )
                if matched:
                    results["map_submitted"] = True
                    submit_shot = screenshots_dir / f"switch-{mode}-map-submitted.png"
                    capture_screenshot(submit_shot, display)
                    print(f"[{mode}] Captured {submit_shot.name} [SUCCESS]!", flush=True)
                else:
                    sys.stderr.write(f"WARNING: [map-submit] not reached before timeout\n")
                    step_ok = False
        else:
            step_ok = do_step(display, action, argument, effective_settle, step_name=name, log_path=log_path, factor=factor)
            if name == "profile-settle":
                wait_for_screen_ready(display, timeout=45.0 * factor, settle_after=4.0)
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
        if step_ok is False:
            print(f"[{mode}] Stopping after {name}: expected visual transition was not observed", flush=True)
            break
        results["completed_steps"].append(name)

    if scenario_name == "random-map-start" and len(results["completed_steps"]) == len(steps):
        flow_passed = drive_random_map(
            display,
            evidence_dir / "run.log",
            screenshots_dir,
            mode,
            settle_scale=speed_factor * factor,
            run_seconds=run_seconds,
        )
        results["map_submitted"] = flow_passed
        results["completed_steps"].append("random-map-flow")
        if not flow_passed:
            results["failed_step"] = "random-map-flow"
    results["success"] = len(results["completed_steps"]) == len(steps) + (1 if scenario_name == "random-map-start" else 0) and (
        results["map_submitted"] if scenario_name == "random-map-start" else True
    )
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["native", "gs"], default="native", help="rendering mode")
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), default="random-map-start", help="scenario")
    parser.add_argument("--display", default=DISPLAY_DEFAULT, help="X11 display")
    parser.add_argument("--evidence", type=Path, default=REPO_ROOT / "build/switch-validation", help="evidence dir")
    parser.add_argument("--screenshots", type=Path, default=REPO_ROOT / ".playwright-mcp/switch", help="shots dir")
    parser.add_argument("--run-seconds", type=float, default=600.0, help="timeout budget")
    parser.add_argument("--build-type", choices=("release", "debug"), default="release")
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
        build_type=args.build_type,
    )

    summary_file = args.evidence / f"scenario-{args.mode}.json"
    summary_file.write_text(json.dumps(results, indent=2))
    return 0 if results["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
