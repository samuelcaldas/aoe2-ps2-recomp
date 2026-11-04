---
name: docker-build
description: Compiles the native static-recompilation port of Age of Empires II (SLES-50282) using isolated Docker containers across Desktop Linux/PC, Nintendo Switch, and Android platforms, with isolated build-type outputs, persistent build caches, and default -j8 concurrency.
---

# Docker Build Skill

Builds the native recompilation targets (`desktop`/`pc`, `switch`, `android`) inside hermetic, isolated Docker containers without polluting host dependencies.

Read [docs/multiplatform-build-isolation.md](../../../docs/multiplatform-build-isolation.md) for full architectural isolation specifications.

## Build Types & High Output Policy

Use Release by default for performance; runtime diagnostics are disabled. Desktop and Switch accept `--build-type Debug|Release|RelWithDebInfo` and use separate output directories. Android supports Gradle Debug and Release variants; Release uses the local debug signing key. Use `--build-type Debug` for assertion-based tests (`-DCMAKE_BUILD_TYPE=Debug`); desktop `--diagnostics` defaults to Debug.
- **High Output & Verbosity**:
  - Build verbosity: Pass `-v` to `ninja` / `cmake --build` and use `ctest --output-on-failure -V` (verbose mode).
  - Runtime diagnostics: Pass `--diagnostics` (`-DAOE2_RUNTIME_DIAGNOSTICS=ON`) to enable aggressive PS2Recomp runtime logging (`PS2X_ENABLE_AGRESSIVE_LOGS=ON`), IOP RPC traces (`PS2X_ENABLE_IOP_RPC_TRACE=ON`), EE DECI2 TTY output (`AOE2_GAME_TTY=ON`), and `StopOnMissingFunction`.
  - Rich trace variables: Utilize runtime environment variables for high output capture:
    - `AOE2_CALL_TRACE_FIRST=N`: Log first N calls per guest function (default 3).
    - `AOE2_CALL_TRACE_MEM=1`: Hex dump 16 bytes at `$a0` arguments.
    - `AOE2_SHAPE_TRACE=1`: SLP shape loader and draw entry-point logging.

## Quick Reference Commands

All builds are orchestrated via [tools/docker_build.py](../../../tools/docker_build.py) and default to 8 parallel workers (`-j8` / `--cpus=8`).

```bash
# 1. Desktop Linux / PC with opt-in runtime diagnostics (-DAOE2_RUNTIME_DIAGNOSTICS=ON)
python3 tools/docker_build.py --platform desktop --diagnostics

# 2. Desktop Linux with CTest unit test execution inside the container (verbose output)
python3 tools/docker_build.py --platform desktop --diagnostics --test

# 3. Desktop Linux / PC standard build
python3 tools/docker_build.py --platform desktop
# or using the pc alias:
python3 tools/docker_build.py --platform pc

# 4. Nintendo Switch Homebrew NRO (devkitA64 / libnx)
python3 tools/docker_build.py --platform switch
# Experimental graphics variants (both feature flags remain OFF in the default build)
python3 tools/docker_build.py --platform switch --experimental-expanded-viewport
python3 tools/docker_build.py --platform switch --experimental-native-graphics
# Debug Switch build (separate cache and output directory)
python3 tools/docker_build.py --platform switch --build-type Debug
# Opt in to HD asset staging and loader initialization
python3 tools/docker_build.py --platform switch --enable-hd-assets

# 5. Android APK (arm64-v8a / x86_64 Gradle release build)
python3 tools/docker_build.py --platform android
```

## Platform Isolation & Concurrent Execution

Every platform runs in a strictly named, labeled container. Builds on different platforms can execute simultaneously without resource collision:

| Platform | Flag | Container Name | Hostname | Output Directory | Primary Artifact |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Desktop / PC** | `--platform desktop` or `pc` | `aoe2-build-desktop-release` | `aoe2-build-desktop-release` | `build/docker-desktop-release/` | `aoe2_game`, `desktop_runtime_probe` |
| **Switch** | `--platform switch` | `aoe2-build-switch-release` | `aoe2-build-switch-release` | `build/docker-switch-release/` | `aoe2_game.nro` |
| **Switch Debug** | `--platform switch --build-type Debug` | `aoe2-build-switch` | `aoe2-build-switch` | `build/docker-switch/` | `aoe2_game.nro` |
| **Android** | `--platform android` | `aoe2-build-android` | `aoe2-build-android` | `build/docker-android/` | `aoe2-recomp-release.apk` |
| **Diag**    | `--diagnostics` | `aoe2-build-desktop-diagnostics` | `aoe2-build-desktop-diagnostics` | `build/docker-desktop-diagnostics/` | `aoe2_game` (diagnostics build) |

### Operational Invariants:
1. **Container Cleanup & Collision Prevention**: `tools/docker_build.py` automatically checks for and removes stale/stopped containers (`ensure_container_clean()`). If an active container with the same name is currently running, it fails fast to prevent concurrent writes to the same build directory.
2. **Build Cache Preservation**:
   - Desktop and Switch preserve incremental object files (`.o`), `.ninja_deps`, and CMake caches in separate directories for each build type.
   - Android preserves Gradle daemon cache via `--build-cache` in `build/docker-android/gradle-home` and native C++ in `build/android-cxx`.
   - HD asset staging is skipped by default. With `--enable-hd-assets`, staging uses an advisory file lock (`.staging.lock` via `fcntl.flock`) to prevent corruption when multiple platforms stage assets simultaneously.
3. **Diagnostics Classification**:
   If a build fails or outputs compiler warnings/errors, run diagnostic classification:
   ```bash
   python3 tools/classify_diagnostics.py --log build.log [--out report.json]
   ```

Note: `docker_build.py` auto-extracts the owned ISO (`tools/extract_disc.py`, `--skip-extract` to disable); devices never receive the ISO. One-shot: `tools/setup_game.py`.
