---
name: docker-validate
description: Validates, runs headless runtime tests, or visually inspects the native recompiled Age of Empires II binary inside isolated Docker containers (Desktop Xvfb probe, Nintendo Switch Ryujinx headless & live noVNC, and Android headless emulator) without host dependencies.
---

# Docker Validate Skill

Validates execution, probes headless initialization, and performs live visual inspection of the recompiled game inside isolated Docker containers without host display or dependency requirements.

Read [docs/multiplatform-build-isolation.md](../../../docs/multiplatform-build-isolation.md) and [docs/desktop-runtime-probe.md](../../../docs/desktop-runtime-probe.md) for background.

## High Output & Observability Requirements

All validations must be executed with **maximum diagnostic verbosity**:
- **Log Archival**: Validation logs (`desktop_validation.log`, `switch_validation.log`) capture all stdout/stderr, guest branch warnings, and RPC transactions.
- **Trace Variables**: When investigating stalls or testing new modules, export rich tracing variables:
  - `AOE2_CALL_TRACE_FIRST=N`: Log first N calls per function.
  - `AOE2_CALL_TRACE_MEM=1`: Hex-dump 16 bytes at `$a0`.
  - `AOE2_SHAPE_TRACE=1`: Log SLP shape rendering calls.
- **Diagnostics Classification**: Triage any runtime failure or loop stall using `python3 tools/classify_diagnostics.py --log <logfile>`.

## Validation Workflows

### 1. Desktop Containerized Runtime Validation
Validates Desktop probe initialization, headless boot, and ctest execution inside the `aoe2-validate-desktop` container using virtual X11 (Xvfb):

```bash
# Full containerized desktop validation (default: 90s timeout, 15s sample interval, -j8)
python3 tools/validate_desktop.py --docker --run-seconds 90 --interval 15

# Custom concurrency
python3 tools/validate_desktop.py --docker --jobs 8
```
- **Container Name**: `aoe2-validate-desktop`
- **Output Artifacts**: Validation logs and ctest reports in `build/docker-desktop/` and `build/desktop-validation/`; visual screenshots in `.playwright-mcp/pc/`.

### 2. Nintendo Switch Headless Validation
Runs the compiled Switch NRO inside the `aoe2-validate-switch` container using headless Ryujinx on a virtual X11 display:

```bash
# Build the Debug NRO expected by the validator, then run the probe
python3 tools/docker_build.py --platform switch --build-type Debug
python3 tools/validate_switch.py --run-seconds 60 --timeout 120
```
- **Container Name**: `aoe2-validate-switch`
- **Telemetry & Logs**: Captures Ryujinx execution logs, Horizon OS service calls, and frame rates in `build/switch-validation/run.log`; timed screenshots in `.playwright-mcp/switch/`.

### 3. Nintendo Switch Live Visual Inspection (noVNC)
Starts a live visual inspection session with Ryujinx running inside the `aoe2-view-switch` container, streaming the GUI over WebSockets/noVNC to a host browser:

```bash
# Launch noVNC viewer container on port 6080
python3 tools/view_switch_novnc.py --port 6080
```
- **Container Name**: `aoe2-view-switch`
- **Access**: Open `http://localhost:6080/vnc.html` for the standard desktop client or `http://localhost:6080/mobile/` for Chrome mobile touch gestures and the phone soft keyboard. From another device, replace `localhost` with the host machine's reachable address.
- **Stop**: Press `Ctrl+C` in the terminal to cleanly terminate the container.

### 4. Android APK & Emulator Validation
Build the Debug APK expected by the validator, then execute headless Android emulator checks inside the `aoe2-validate-android` container:

```bash
python3 tools/docker_build.py --platform android --build-type Debug
python3 tools/validate_android.py
```
- **Container Name**: `aoe2-validate-android`
- **Output Artifacts**: `build/docker-android/aoe2-recomp-debug.apk`, logcat in `build/android-validation/logcat.txt`, timed screenshots in `.playwright-mcp/android/`.

### 5. Android Live Visual Inspection & Interactive Testing (noVNC)
Starts a live visual inspection session with the Android emulator running inside the `aoe2-view-android` container with KVM hardware acceleration, streaming the screen and touch/pointer events over noVNC:

```bash
# Launch noVNC viewer container on port 6082
python3 tools/view_android_novnc.py --port 6082
```
- **Container Name**: `aoe2-view-android`
- **Access**: Open `http://localhost:6082/vnc.html` for standard desktop browser control or `http://localhost:6082/mobile/` for mobile touch navigation.
- **Stop**: Press `Ctrl+C` in the terminal to cleanly terminate the container.

## Container Isolation & Cleanliness

All validation runners enforce:
- Unique container names (`aoe2-validate-desktop`, `aoe2-validate-switch`, `aoe2-view-switch`, `aoe2-validate-android`, `aoe2-view-android`).
- Metadata labels (`aoe2.role=validate`, `aoe2.role=view`).
- Default `-j8` parallelism.
- Stale container cleanup on boot with fail-fast detection of active duplicates.

Note: `docker_build.py` auto-extracts the owned ISO (`tools/extract_disc.py`, `--skip-extract` to disable); devices never receive the ISO. One-shot: `tools/setup_game.py`.
