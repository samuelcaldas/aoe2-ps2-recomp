#pragma once

#include <aoe2/pad/pad_report.hpp>

#include <array>
#include <cstdint>

namespace aoe2::pad {

class PadBackend {
public:
    virtual ~PadBackend() = default;
    virtual HostPadState Sample(int port) = 0;
    virtual void Rumble(int port, uint8_t small_motor, uint8_t large_motor) = 0;
};

class PadDevice {
public:
    explicit PadDevice(PadBackend& backend);
    int Init(int mode);
    int Start(int arg);
    int End();
    int SlotMax(int port) const;
    int PortMax() const;
    int PortOpen(int port, int slot, uint32_t dma_buffer);
    int PortClose(int port, int slot);
    int State(int port, int slot);
    int ReqState(int port, int slot) const;
    int Read(int port, int slot, std::array<uint8_t, 32>& out);
    int InfoAct(int port, int slot, int actuator, int term) const;
    int InfoMode(int port, int slot, int term, int offset) const;
    int SetMainMode(int port, int slot, int offset, int lock);
    int InfoPressMode(int port, int slot) const;
    int SetActAlign(int port, int slot, const uint8_t data[6]);
    int SetActDirect(int port, int slot, const uint8_t data[6]);

private:
    bool Valid(int port, int slot) const;
    bool Connected(int port);
    void StopRumble(int port);
    PadBackend& backend_;
    std::array<bool, 2> opened_{};
    std::array<PadMode, 2> modes_{PadMode::kAnalog, PadMode::kAnalog};
    std::array<uint16_t, 2> rumble_{};
    std::array<bool, 2> rumble_changed_{};
    std::array<int, 2> polls_since_open_{};  // drives the FINDPAD -> FINDCTP1 -> STABLE sequence
    std::array<int, 2> busy_polls_{};        // polls left in EXECCMD after a configuration command
};

}
