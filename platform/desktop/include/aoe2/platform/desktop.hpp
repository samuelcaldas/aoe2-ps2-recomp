#pragma once

#include <aoe2/input/actions.hpp>
#include <aoe2/runtime/guest_memory.hpp>
#include <chrono>
#include <iostream>
#include <vector>

namespace aoe2::platform::desktop {

class DesktopPlatformServices : public aoe2::runtime::PlatformServices {
public:
    DesktopPlatformServices() = default;
    ~DesktopPlatformServices() override = default;

    void LogInfo(const char* message) override {
        std::cout << "[INFO] " << message << std::endl;
    }

    void LogError(const char* message) override {
        std::cerr << "[ERROR] " << message << std::endl;
    }

    uint64_t GetMicroseconds() override {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   now.time_since_epoch())
            .count();
    }
};

class ActionRecorderInputSink : public aoe2::input::InputSink {
public:
    ActionRecorderInputSink() = default;
    ~ActionRecorderInputSink() override = default;

    void SubmitEvent(const aoe2::input::ActionEvent& event) override {
        recorded_events_.push_back(event);
    }

    const std::vector<aoe2::input::ActionEvent>& GetEvents() const {
        return recorded_events_;
    }

    void Clear() {
        recorded_events_.clear();
    }

private:
    std::vector<aoe2::input::ActionEvent> recorded_events_;
};

} // namespace aoe2::platform::desktop
