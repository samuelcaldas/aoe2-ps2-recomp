#include "aoe2/platform/switch/switch_platform.hpp"

#include <iostream>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace aoe2::platform::switch_env {

SwitchPlatform& SwitchPlatform::Instance() {
    static SwitchPlatform instance;
    return instance;
}

SwitchPlatform::SwitchPlatform() = default;

SwitchPlatform::~SwitchPlatform() {
    if (initialized_) {
        Shutdown();
    }
}

bool SwitchPlatform::Initialize(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    if (initialized_) {
        return true;
    }

#if defined(__SWITCH__)
    // Initialize standard SDMC filesystem driver
    Result rc = fsdevMountSdmc();
    if (R_FAILED(rc)) {
        std::cerr << "[SwitchPlatform] Warning: fsdevMountSdmc() failed with code: " << rc << '\n';
    }

    // Attempt to mount RomFS (embedded in NRO or program)
    rc = romfsInit();
    if (R_FAILED(rc)) {
        std::cerr << "[SwitchPlatform] Notice: romfsInit() returned " << rc << " (using SDMC assets)\n";
    }

    // Initialize default operation mode from Horizon OS
    AppletOperationMode op_mode = appletGetOperationMode();
    operation_mode_ = (op_mode == AppletOperationMode_Console) 
        ? ConsoleOperationMode::Docked 
        : ConsoleOperationMode::Handheld;

    // Configure default native window display dimensions according to operation mode
    NWindow* win = nwindowGetDefault();
    if (win) {
        u32 width = (operation_mode_ == ConsoleOperationMode::Docked) ? 1920 : 1280;
        u32 height = (operation_mode_ == ConsoleOperationMode::Docked) ? 1080 : 720;
        nwindowSetDimensions(win, width, height);
    }

    // Check memory mode on boot
    SwitchMemoryStatus mem = CheckMemoryStatus();
    if (!mem.is_title_takeover) {
        std::cerr << "[SwitchPlatform] WARNING: Running in Applet Mode (Album)! "
                  << "Only ~400MB memory available. Launch via Title Takeover (hold R over a game) "
                  << "to access full 3.5GB RAM and prevent OOM crashes.\n";
    }
#endif

    initialized_ = true;
    return true;
}

void SwitchPlatform::Shutdown() {
    if (!initialized_) {
        return;
    }

#if defined(__SWITCH__)
    romfsExit();
    fsdevUnmountAll();
#endif

    initialized_ = false;
}

SwitchMemoryStatus SwitchPlatform::CheckMemoryStatus() const {
    SwitchMemoryStatus status;

#if defined(__SWITCH__)
    AppletType at = appletGetAppletType();
    switch (at) {
        case AppletType_Application:
        case AppletType_SystemApplication:
            status.mode = AppletMode::Application;
            status.is_title_takeover = true;
            status.available_memory_bytes = 3500ULL * 1024ULL * 1024ULL; // ~3.5GB
            break;
        case AppletType_LibraryApplet:
            status.mode = AppletMode::LibraryApplet;
            status.is_title_takeover = false;
            status.available_memory_bytes = 400ULL * 1024ULL * 1024ULL;  // ~400MB
            break;
        case AppletType_OverlayApplet:
            status.mode = AppletMode::OverlayApplet;
            status.is_title_takeover = false;
            status.available_memory_bytes = 100ULL * 1024ULL * 1024ULL;
            break;
        default:
            status.mode = AppletMode::Unknown;
            status.is_title_takeover = false;
            status.available_memory_bytes = 400ULL * 1024ULL * 1024ULL;
            break;
    }
#else
    // Host mock behavior for unit testing
    status.mode = AppletMode::Application;
    status.is_title_takeover = true;
    status.available_memory_bytes = 3500ULL * 1024ULL * 1024ULL;
#endif

    return status;
}

std::filesystem::path SwitchPlatform::ResolveStoragePath(const std::filesystem::path& relative_path) const {
    if (relative_path.is_absolute()) {
        return relative_path;
    }
    return (sdmc_root_ / relative_path).lexically_normal();
}

void SwitchPlatform::SetSdmcRoot(const std::filesystem::path& root) {
    sdmc_root_ = root;
}

const std::filesystem::path& SwitchPlatform::GetSdmcRoot() const {
    return sdmc_root_;
}

ConsoleOperationMode SwitchPlatform::GetOperationMode() const {
    return operation_mode_;
}

void SwitchPlatform::SetOperationMode(ConsoleOperationMode mode) {
    if (operation_mode_ != mode) {
        operation_mode_ = mode;
        if (mode_callback_) {
            mode_callback_(operation_mode_);
        }
    }
}

void SwitchPlatform::RegisterOperationModeCallback(OperationModeCallback callback) {
    mode_callback_ = std::move(callback);
}

} // namespace aoe2::platform::switch_env
