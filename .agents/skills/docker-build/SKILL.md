---
name: docker-build
description: Compiles the native static-recompilation port of Age of Empires II (SLES-50282) using isolated Docker containers across Desktop Linux/PC, Nintendo Switch, and Android platforms with persistent build caches and default -j8 concurrency.
---

# Docker Build Skill

Builds the native recompilation targets (`desktop`/`pc`, `switch`, `android`) inside hermetic, isolated Docker containers without polluting host dependencies.

Read [docs/multiplatform-build-isolation.md](../../../docs/multiplatform-build-isolation.md) for full architectural isolation specifications.

## Quick Reference Commands

All builds are orchestrated via [tools/docker_build.py](../../../tools/docker_build.py) and default to 8 parallel workers (`-j8` / `--cpus=8`).

```bash
# 1. Desktop Linux / PC (Release build)
python3 tools/docker_build.py --platform desktop
# or using the pc alias:
python3 tools/docker_build.py --platform pc

# 2. Desktop Linux with CTest unit test execution inside the container
python3 tools/docker_build.py --platform desktop --test

# 3. Desktop Linux with Runtime Diagnostics tracing (-DAOE2_RUNTIME_DIAGNOSTICS=ON)
python3 tools/docker_build.py --platform desktop --diagnostics

# 4. Nintendo Switch Homebrew NRO (devkitA64 / libnx)
python3 tools/docker_build.py --platform switch

# 5. Android APK (arm64-v8a / x86_64 Gradle build)
python3 tools/docker_build.py --platform android
```

## Platform Isolation & Concurrent Execution

Every platform runs in a strictly named, labeled container. Builds on different platforms can execute simultaneously without resource collision:

| Platform | Flag | Container Name | Hostname | Output Directory | Primary Artifact |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Desktop** | `--platform desktop` | `aoe2-build-desktop` | `aoe2-build-desktop` | `build/docker-desktop/` | `aoe2_game`, `desktop_runtime_probe` |
| **PC Alias**| `--platform pc` | `aoe2-build-pc` | `aoe2-build-pc` | `build/docker-desktop/` | `aoe2_game`, `desktop_runtime_probe` |
| **Switch**  | `--platform switch` | `aoe2-build-switch` | `aoe2-build-switch` | `build/docker-switch/` | `aoe2_game_nro` (`aoe2_game.nro`) |
| **Android** | `--platform android`| `aoe2-build-android` | `aoe2-build-android` | `build/docker-android/` | `aoe2-recomp-debug.apk` |
| **Diag**    | `--diagnostics` | `aoe2-build-desktop-diagnostics` | `aoe2-build-desktop-diagnostics` | `build/docker-desktop-diagnostics/` | `aoe2_game` (diagnostics build) |

### Operational Invariants:
1. **Container Cleanup & Collision Prevention**: `tools/docker_build.py` automatically checks for and removes stale/stopped containers (`ensure_container_clean()`). If an active container with the same name is currently running, it fails fast to prevent concurrent writes to the same build directory.
2. **Build Cache Preservation**:
   - Desktop and Switch preserve incremental object files (`.o`), `.ninja_deps`, and CMake caches under `build/docker-desktop/` and `build/docker-switch/`.
   - Android preserves Gradle daemon cache via `--build-cache` in `build/docker-android/gradle-home` and native C++ in `build/android-cxx`.
   - Asset staging uses an advisory file lock (`.staging.lock` via `fcntl.flock`) to prevent corruption when multiple platforms stage HD assets simultaneously.
3. **Diagnostics Classification**:
   If a build fails or outputs compiler warnings/errors, run diagnostic classification:
   ```bash
   python3 tools/classify_diagnostics.py --log build.log [--out report.json]
   ```
