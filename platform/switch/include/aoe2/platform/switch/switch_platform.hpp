#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace aoe2::platform::switch_env {

enum class AppletMode : uint8_t {
    Application,     // Running via Title Takeover (full memory access, ~3.5GB)
    LibraryApplet,   // Running via Album Applet Mode (restricted memory, ~400MB)
    OverlayApplet,   // System overlay
    Unknown
};

enum class ConsoleOperationMode : uint8_t {
    Handheld,  // 720p internal display
    Docked     // 1080p external display
};

struct SwitchMemoryStatus {
    AppletMode mode{AppletMode::Unknown};
    bool is_title_takeover{false};
    uint64_t available_memory_bytes{0};
};

class SwitchPlatform {
public:
    static SwitchPlatform& Instance();

    bool Initialize(int argc = 0, char* argv[] = nullptr);
    void Shutdown();

    // Memory environment query (guards against OOM crashes from Album applet mode)
    SwitchMemoryStatus CheckMemoryStatus() const;

    // Path resolution: maps paths relative to sdmc:/switch/aoe2/
    std::filesystem::path ResolveStoragePath(const std::filesystem::path& relative_path) const;
    void SetSdmcRoot(const std::filesystem::path& root);
    const std::filesystem::path& GetSdmcRoot() const;

    // Operation mode (Handheld 720p vs Docked 1080p)
    ConsoleOperationMode GetOperationMode() const;
    void SetOperationMode(ConsoleOperationMode mode);
    using OperationModeCallback = std::function<void(ConsoleOperationMode)>;
    void RegisterOperationModeCallback(OperationModeCallback callback);

private:
    SwitchPlatform();
    ~SwitchPlatform();

    std::filesystem::path sdmc_root_{"sdmc:/switch/aoe2"};
    ConsoleOperationMode operation_mode_{ConsoleOperationMode::Handheld};
    OperationModeCallback mode_callback_{};
    bool initialized_{false};
};

} // namespace aoe2::platform::switch_env
