// kt_libpad: per-frame update, the per-slot state machine and report processing.
// Origin: decomp src/kcet/kt_libpad.c. Where the decomp and the recompiled binary differ the binary wins:
// the state dispatcher (5/6/7), the step function's return value, the report layout, and the report read.
#include <aoe2/pad/kt_libpad_model.hpp>

namespace aoe2::pad {
namespace {

constexpr const char* kSourceFile = "kt_libpad.c";
constexpr int kStepCount = 3;
constexpr uint32_t kRapidChannels = 3;
constexpr uint32_t kRapidStride = 4;
constexpr uint32_t kActuatorCount = 6;
constexpr uint32_t kUnusedAlign = 0xFF;
constexpr int kStateDisconnected = 0;
constexpr int kStateFindPad = 1;
constexpr int kStateFindController = 2;
constexpr int kStateExecutingCommand = 5;
constexpr int kStateStable = 6;
constexpr int kStateError = 7;
constexpr int kStateClosed = 0x63;
constexpr int kRetryLimit = 6;
constexpr int kPressureModeShift = 4;

}  // namespace

// func_00127380
void KtLibpadModel::Update() {
    const uint32_t slots = memory_.Load32(globals_.work);
    const int count = static_cast<int>(WorkPadCount());
    for (int i = 0; i < count; ++i) StepSlot(slots + static_cast<uint32_t>(i) * slot_field::kSize);
}

// func_001273D8: runs the current step; advances only when the step reports it is done
void KtLibpadModel::StepSlot(uint32_t slot) {
    int step = static_cast<int>(Field32(slot, slot_field::kStep));
    if (step < 0 || step >= kStepCount) {
        memory_.ReportAssertion(kSourceFile, 0x568, "step >= 0 && step < n_func");
        return;
    }
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) & ~slot_flag::kReady);
    const uint32_t function = memory_.Load32(globals_.step_table + static_cast<uint32_t>(step) * 4);
    int done = 0;
    if (function == kOpenPortAddress) done = OpenPort(slot);
    else if (function == kPollStateAddress) done = PollState(slot);
    else if (function == kClosePortAddress) done = ClosePort(slot);
    else memory_.ReportAssertion(kSourceFile, 0, "unknown step function in the step table");
    if (done == 0) return;
    step = step + 1 >= kStepCount ? 0 : step + 1;
    SetField32(slot, slot_field::kStep, static_cast<uint32_t>(step));
    InvalidateState(slot);
}

// func_00127488: opens the requested port; the library buffer is the last 256 bytes of the slot
int KtLibpadModel::OpenPort(uint32_t slot) {
    const int request_port = static_cast<int>(Field32(slot, slot_field::kReqPort));
    const int request_slot = static_cast<int>(Field32(slot, slot_field::kReqSlot));
    const int port = static_cast<int>(Field32(slot, slot_field::kPort));
    const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
    if (NoUnitBusy(request_port) != 0) return 0;
    if (port == request_port && unit == request_slot) return 0;
    if (port != -1) memory_.ReportAssertion(kSourceFile, 0x5B7, "port == -1");
    if (unit != -1) memory_.ReportAssertion(kSourceFile, 0x5B8, "slot == -1");
    if (request_port == -1 || request_slot == -1) return 0;
    const uint32_t buffer = slot + slot_field::kLibraryBuffer;
    if (buffer & 0x3F) memory_.ReportAssertion(kSourceFile, 0x5BE, "!((int)p->dma_buffer & 63)");
    if (device_.PortOpen(request_port, request_slot, buffer) != 1) return 0;
    SetField32(slot, slot_field::kPort, static_cast<uint32_t>(request_port));
    SetField32(slot, slot_field::kSlot, static_cast<uint32_t>(request_slot));
    return 1;
}

// func_001275A0: binary dispatch table (5 resets, 6 processes, 7 counts the error)
int KtLibpadModel::PollState(uint32_t slot) {
    if (PortChanged(slot) != 0) return 1;
    const int state = device_.State(static_cast<int>(Field32(slot, slot_field::kPort)),
                                    static_cast<int>(Field32(slot, slot_field::kSlot)));
    SetField32(slot, slot_field::kState, static_cast<uint32_t>(state));
    switch (state) {
        case kStateFindPad: StateFindPad(slot); break;
        case kStateFindController: StateFindController(slot); break;
        case kStateExecutingCommand: StateExecutingCommand(slot); break;
        case kStateStable: StateStable(slot); break;
        case kStateError: CountError(slot); break;
        case kStateClosed:
            StateDisconnected(slot);
            return 1;
        default: StateDisconnected(slot); break;
    }
    return 0;
}

// func_001276B0
int KtLibpadModel::NoPendingWork(uint32_t) { return 0; }

// func_001276B8
int KtLibpadModel::PortChanged(uint32_t slot) {
    bool changed = NoUnitBusy(static_cast<int>(Field32(slot, slot_field::kPort))) != 0;
    if (NoPendingWork(slot) != 0) changed = true;
    if (Field32(slot, slot_field::kPort) != Field32(slot, slot_field::kReqPort) ||
        Field32(slot, slot_field::kSlot) != Field32(slot, slot_field::kReqSlot))
        changed = true;
    if (!changed) return 0;
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kPortChanged);
    return 1;
}

// func_00127750
int KtLibpadModel::ClosePort(uint32_t slot) {
    const int port = static_cast<int>(Field32(slot, slot_field::kPort));
    const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
    bool done = true;
    if (device_.State(port, unit) != kStateClosed && device_.PortClose(port, unit) != 1) done = false;
    if (!done) return 0;
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) & ~slot_flag::kPortChanged);
    SetField32(slot, slot_field::kPort, static_cast<uint32_t>(-1));
    SetField32(slot, slot_field::kSlot, static_cast<uint32_t>(-1));
    return 1;
}

// func_001277E0, func_001277F8, func_00127810
void KtLibpadModel::SetReportFailed(uint32_t slot) {
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kReportFailed);
}

void KtLibpadModel::ClearReportFailed(uint32_t slot) {
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) & ~slot_flag::kReportFailed);
}

int KtLibpadModel::ReportFailed(uint32_t slot) {
    return static_cast<int>(Field32(slot, slot_field::kFlags) & slot_flag::kReportFailed);
}

// func_00127820: descriptor bytes (4 per actuator, queried with terms 1 to 4)
void KtLibpadModel::ReadActuatorInfo(uint32_t slot) {
    const int port = static_cast<int>(Field32(slot, slot_field::kPort));
    const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
    int count = device_.InfoAct(port, unit, -1, 0);
    if (count > static_cast<int>(kActuatorCount)) count = static_cast<int>(kActuatorCount);
    for (int i = 0; i < count; ++i)
        for (int term = 1; term <= 4; ++term)
            memory_.Store8(slot + slot_field::kModeInfo + static_cast<uint32_t>(i * 4 + term - 1),
                           static_cast<uint8_t>(device_.InfoAct(port, unit, i, term)));
}

// func_00127900: current mode ids; reports whether the word changed
int KtLibpadModel::ReadModeWord(uint32_t slot) {
    const int port = static_cast<int>(Field32(slot, slot_field::kPort));
    const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
    uint32_t word = 0;
    for (int i = 0; i < 4; ++i)
        word |= static_cast<uint32_t>(static_cast<uint8_t>(device_.InfoMode(port, unit, i + 1, 0))) << (8 * i);
    if (Field32(slot, slot_field::kModeWord) == word) return 0;
    SetField32(slot, slot_field::kModeWord, word);
    return 1;
}

// func_001279A0, func_001279A8, func_001279B0, func_001279B8, func_001279C0
void KtLibpadModel::StateDisconnected(uint32_t slot) { FullReset(slot); }
void KtLibpadModel::StateFindPad(uint32_t slot) { ResetButtons(slot); }
void KtLibpadModel::StateFindController(uint32_t slot) { ProcessPhases(slot, 0); }
void KtLibpadModel::StateExecutingCommand(uint32_t slot) { ResetButtons(slot); }
void KtLibpadModel::StateStable(uint32_t slot) { ProcessPhases(slot, 1); }

// func_00127BD8
void KtLibpadModel::CountError(uint32_t slot) { SetField32(slot, slot_field::kErrors, Field32(slot, slot_field::kErrors) + 1); }

// func_00127BE8: reads the report; bytes past the returned length are filled with 0xFF
int KtLibpadModel::ReadReport(uint32_t slot) {
    std::array<uint8_t, 32> report{};
    const int length = device_.Read(static_cast<int>(Field32(slot, slot_field::kPort)),
                                    static_cast<int>(Field32(slot, slot_field::kSlot)), report);
    const uint32_t base = slot + slot_field::kReport;
    if (length > 0) memory_.WriteBytes(base, report.data(), report.size());
    if (static_cast<uint32_t>(length - 1) < 31) {
        for (int i = length; i <= 31; ++i) memory_.Store8(base + static_cast<uint32_t>(i), 0xFF);
    }
    SetField32(slot, slot_field::kReadLength, static_cast<uint32_t>(length));
    return length;
}

// func_00127C68, func_00127C88, func_00127C98
int KtLibpadModel::DataEnabled(uint32_t slot) {
    return static_cast<int>(Field32(slot, slot_field::kFlags) & slot_flag::kDataDisabled);
}

void KtLibpadModel::StoreButtons(uint32_t slot, int held, int pressed, int released) {
    memory_.Store16(slot + slot_field::kButtons, static_cast<uint16_t>(held));
    memory_.Store16(slot + slot_field::kPressed, static_cast<uint16_t>(pressed));
    memory_.Store16(slot + slot_field::kReleased, static_cast<uint16_t>(released));
}

void KtLibpadModel::StoreSlotData(uint32_t slot, uint32_t source, int length) {
    if (source == 0) {
        memory_.ReportAssertion(kSourceFile, 0x7F4, "src");
        return;
    }
    if (length == 0) memory_.ReportAssertion(kSourceFile, 0x7F5, "n_dat");
    for (int i = 0; i < length; ++i)
        memory_.Store8(slot + slot_field::kData + static_cast<uint32_t>(i), memory_.Load8(source + static_cast<uint32_t>(i)));
}

// func_00127E68
int KtLibpadModel::ProcessReport(uint32_t slot, int flag) {
    if (flag == 0) {
        SetField32(slot, slot_field::kErrors, 0);
        return 0;
    }
    const uint32_t report = slot + slot_field::kReport;
    if (memory_.Load8(report) != 0) {
        CountError(slot);
        SetReportFailed(slot);
        memory_.Store16(slot + slot_field::kPressed, 0);
        memory_.Store16(slot + slot_field::kReleased, 0);
        SetField32(slot, slot_field::kAnalogHigh, 0);
        SetField32(slot, slot_field::kAnalogLow, 0);
        return 0;
    }
    const uint32_t mode_byte = memory_.Load8(report + 1);
    if (!((Field32(slot, slot_field::kEnableMask) >> (mode_byte >> kPressureModeShift)) & 1)) return 0;

    const uint16_t held = static_cast<uint16_t>(~((memory_.Load8(report + 2) << 8) | memory_.Load8(report + 3)));
    if (held != 0) {
        for (uint32_t i = 0; i < kRapidChannels; ++i) {
            const uint32_t entry = slot + slot_field::kRapid + i * kRapidStride;
            const uint16_t mask = memory_.Load16(entry);
            const uint8_t mode = memory_.Load8(entry + 2);
            const uint8_t speed = memory_.Load8(entry + 3);
            if (!(mask & held)) continue;
            if ((speed & 0xF0) == 0) memory_.Store8(entry + 2, speed & 0xF);
            else if (mode != 0) memory_.Store8(entry + 2, static_cast<uint8_t>(mode - 1));
            else memory_.Store8(entry + 2, speed & 0xF);
        }
    }

    uint32_t source = report + 8;
    uint32_t sticks_raw = 0;
    uint32_t sticks_scaled = 0;
    if (DataEnabled(slot) && IsExtendedMode(static_cast<int>(mode_byte >> kPressureModeShift))) {
        for (uint32_t i = 0; i < 4; ++i) {
            const uint32_t channel = slot + slot_field::kActData + i * 4;
            const int value = memory_.Load8(report + 7 - i);
            const int scaled = ScaleByMode(value, memory_.Load8(channel + 2), memory_.Load8(channel),
                                           memory_.Load8(channel + 1), memory_.Load8(channel + 3));
            sticks_raw = (sticks_raw << 8) | static_cast<uint32_t>(value);
            sticks_scaled = (sticks_scaled << 8) | static_cast<uint32_t>(scaled & 0xFF);
        }
    }
    if (IsPressureModeId(static_cast<int>(mode_byte))) source = globals_.analog_default;

    if (ReportFailed(slot) != 0) {
        ClearReportFailed(slot);
        SetField32(slot, slot_field::kErrors, 0);
    }
    const uint16_t previous = memory_.Load16(slot + slot_field::kButtons);
    StoreButtons(slot, held, held & ~previous, previous & ~held);
    StoreSlotData(slot, source, 12);
    SetField32(slot, slot_field::kModeId, mode_byte >> kPressureModeShift);
    SetField32(slot, slot_field::kAnalogLow, sticks_raw);
    SetField32(slot, slot_field::kAnalogHigh, sticks_scaled);
    return 1;
}

// func_00128130
int KtLibpadModel::FindAlignment(uint32_t entry, int index) {
    if (index == 0) return 0xFF;
    if (memory_.Load8(entry) == act_match_0_ && memory_.Load8(entry + 1) == act_match_1_) return 0;
    return 0xFF;
}

// func_001281B0
int KtLibpadModel::NeedsAlignment(uint32_t slot) {
    return (Field32(slot, slot_field::kFlags) & slot_flag::kChanged) != 0;
}

// func_001281C0
int KtLibpadModel::ConfigureAlignment(uint32_t slot) {
    if (NeedsAlignment(slot) == 0) return 0;
    uint8_t found[6];
    for (uint8_t& value : found) value = static_cast<uint8_t>(kUnusedAlign);
    uint32_t used = 0;
    for (uint32_t i = 0; i < kActuatorCount; ++i) {
        memory_.Store8(slot + slot_field::kActConfig + i * 3 + 1, 0);
        const uint32_t align = slot + slot_field::kActAlign + i * 2;
        found[i] = static_cast<uint8_t>(FindAlignment(align, memory_.Load8(slot + slot_field::kModeInfo + i * 4)));
        if (found[i] == kUnusedAlign) continue;
        const uint32_t descriptor = memory_.Load8(slot + slot_field::kModeInfo + found[i] * 4u + 2);
        const uint32_t step = descriptor ? 1 : descriptor;
        memory_.Store8(align, found[i]);
        if (used + step < 7) {
            memory_.Store8(align + 1, static_cast<uint8_t>(used));
            used += step;
        }
    }
    int result = 0;
    const int port = static_cast<int>(Field32(slot, slot_field::kPort));
    const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
    if (Field32(slot, slot_field::kFlags) & slot_flag::kActuatorsLive) {
        result = device_.SetActAlign(port, unit, found);
        if (result == 1) {
            StopActuators(port, unit);
            result = -1;
        }
    }
    if (result != -1) SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) ^ slot_flag::kChanged);
    return result;
}

// func_00128390: translates the engine's actuator values to the library's output order
void KtLibpadModel::UpdateActuators(uint32_t slot) {
    uint8_t output[8] = {};
    const bool rumble = (Field32(slot, slot_field::kFlags) & slot_flag::kRumbleEnabled) != 0;
    for (uint32_t i = 0; i < kActuatorCount; ++i) {
        const uint8_t align = memory_.Load8(slot + slot_field::kActAlign + i * 2);
        if (align == kUnusedAlign) continue;
        const uint32_t triple = slot + slot_field::kActConfig + i * 3;
        uint32_t value = memory_.Load8(triple + 1);
        if (!rumble) value = 0;
        if (memory_.Load8(slot + slot_field::kModeInfo + align * 4u + 2) == 0) value = value != 0 ? 1 : 0;
        const uint32_t target = memory_.Load8(slot + slot_field::kActAlign + i * 2 + 1);
        if (target + value < 6) {
            if (target < sizeof(output)) output[target] = static_cast<uint8_t>(value);
        } else {
            memory_.ReportAssertion(kSourceFile, 0x9D9, "offset+length < n_act");
        }
    }
    uint32_t words[2];
    for (int i = 0; i < 2; ++i) words[i] = output[i * 4] | (output[i * 4 + 1] << 8) | (output[i * 4 + 2] << 16) |
                                           (static_cast<uint32_t>(output[i * 4 + 3]) << 24);
    const bool changed = words[0] != Field32(slot, slot_field::kActExec) || words[1] != Field32(slot, slot_field::kActExec + 4);
    if (!changed || !(Field32(slot, slot_field::kFlags) & slot_flag::kActuatorsLive)) return;
    device_.SetActDirect(static_cast<int>(Field32(slot, slot_field::kPort)), static_cast<int>(Field32(slot, slot_field::kSlot)), output);
    SetField32(slot, slot_field::kActExec, words[0]);
    SetField32(slot, slot_field::kActExec + 4, words[1]);
}

// func_001285F8
int KtLibpadModel::FindModeIndex(int port, int slot, int type) {
    const int count = device_.InfoMode(port, slot, 4, -1);
    for (int i = 0; i < count; ++i) {
        const int id = device_.InfoMode(port, slot, 4, i);
        if (id <= 0) break;
        if (id == type) return i;
    }
    return -1;
}

// func_00128698
int KtLibpadModel::ModePending(uint32_t slot) { return (Field32(slot, slot_field::kFlags) & slot_flag::kModePending) != 0; }

// func_001286A8
int KtLibpadModel::ApplyMode(uint32_t slot) {
    int mode = static_cast<int>(Field32(slot, slot_field::kActMode));
    int param = static_cast<int>(Field32(slot, slot_field::kActParam));
    if (ModePending(slot) == 0) return 0;
    const int word = static_cast<int>(Field32(slot, slot_field::kModeWord)) & 0xFF;
    if (param == 0) param = word;
    if (mode == 0) mode = word;
    mode ^= param;
    if (mode <= 0) {
        SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) ^ slot_flag::kModePending);
        return 0;
    }
    const int port = static_cast<int>(Field32(slot, slot_field::kPort));
    const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
    int result = 0;
    const int index = FindModeIndex(port, unit, mode);
    if (index >= 0) {
        if (device_.SetMainMode(port, unit, index, param) == 1) {
            result = 1;
        } else if (static_cast<int>(Field32(slot, slot_field::kRetry)) < kRetryLimit) {
            SetField32(slot, slot_field::kRetry, Field32(slot, slot_field::kRetry) + 1);
            result = param != 3 ? 0 : -1;
        } else {
            SetField32(slot, slot_field::kRetry, 0);
            result = param != 3 ? -1 : 0;
        }
    }
    if (param == 3 && result == 0) result = -1;
    if (result >= 0) {
        SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) ^ slot_flag::kModePending);
        if (result == 1) MarkChanged(slot);
    }
    return result;
}

// func_001287D8, func_001287E8, func_00128820, func_00128850
int KtLibpadModel::PressPending(uint32_t slot) { return (Field32(slot, slot_field::kFlags) & slot_flag::kPressPending) != 0; }

int KtLibpadModel::PressModeA(uint32_t slot) {
    const int mode = static_cast<int>(Field32(slot, slot_field::kPressMode));
    return mode == 1 ? 1 : 2;
}

int KtLibpadModel::PressModeB(uint32_t slot) {
    const int mode = static_cast<int>(Field32(slot, slot_field::kPressMode));
    return mode == 1 || mode < 2 ? 1 : 2;
}

void KtLibpadModel::ClearPressPending(uint32_t slot) {
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) & ~slot_flag::kPressPending);
}

// func_00128878
int KtLibpadModel::ApplyPressMode(uint32_t slot) {
    const uint32_t flags = Field32(slot, slot_field::kFlags);
    const bool supported = device_.InfoPressMode(static_cast<int>(Field32(slot, slot_field::kPort)),
                                                 static_cast<int>(Field32(slot, slot_field::kSlot))) == 1;
    SetField32(slot, slot_field::kFlags, supported ? flags & ~slot_flag::kNoPressure : flags | slot_flag::kNoPressure);
    if (PressPending(slot) == 0) return 0;
    const int is_pressure = IsPressureModeId(static_cast<int>(Field32(slot, slot_field::kModel))) ? 1 : 0;
    if (PressModeA(slot) == is_pressure) PressModeB(slot);
    ClearPressPending(slot);
    return 0;
}

// func_001279C8: phase machine. Phase 0 reads the mode word and descriptors; 1 applies a requested mode; 2 sets
// the actuator alignment; 3 applies the pressure mode; 4 and above process the report.
void KtLibpadModel::ProcessPhases(uint32_t slot, int flag) {
    int phase = static_cast<int>(Field32(slot, slot_field::kPhase));
    int event = 0;
    if (ReadReport(slot) == 0) {
        const uint8_t reported = memory_.Load8(slot + slot_field::kReport + 1);
        if ((reported & 0xF0) != (Field32(slot, slot_field::kModel) & 0xF0)) {
            SetField32(slot, slot_field::kModel, reported);
            phase = 0;
            event = 1;
        }
    }
    const uint32_t flags = slot_field::kFlags;
    switch (phase) {
        case 0:
            SetField32(slot, slot_field::kRetry, 0);
            SetReportFailed(slot);
            SetField32(slot, slot_field::kErrors, 0);
            event |= ReadModeWord(slot);
            if (event != 0) {
                ReadActuatorInfo(slot);
                SetField32(slot, flags, Field32(slot, flags) | slot_flag::kChanged);
                SetField32(slot, flags, flag ? Field32(slot, flags) | slot_flag::kActuatorsLive : Field32(slot, flags) & ~slot_flag::kActuatorsLive);
            }
            ++phase;
            break;
        case 1:
        case 2:
        case 3: {
            int result = phase == 1 ? ApplyMode(slot) : phase == 2 ? ConfigureAlignment(slot) : ApplyPressMode(slot);
            result = result == 0 ? 2 : (result == 1 ? 1 : 0);
            if (result > 0) {
                SetField32(slot, slot_field::kFailures, 0);
                phase += result;
                break;
            }
            const int request = device_.ReqState(static_cast<int>(Field32(slot, slot_field::kPort)),
                                                 static_cast<int>(Field32(slot, slot_field::kSlot)));
            int next = phase;
            if (request == 1) SetField32(slot, slot_field::kFailures, Field32(slot, slot_field::kFailures) + 1);
            else if (request < 2) next = request == 0 ? phase + 1 : phase - 1;
            else if (request != 2) next = phase - 1;
            phase = next;
            break;
        }
        default:
            ProcessReport(slot, 1);
            UpdateActuators(slot);
            if (ModePending(slot) == 0 && NeedsAlignment(slot) == 0 && PressPending(slot) == 0) {
                SetField32(slot, flags, Field32(slot, flags) | slot_flag::kReady);
                break;
            }
            ResetButtons(slot);
            phase = static_cast<int>(Field32(slot, slot_field::kPhase));
            break;
    }
    SetField32(slot, slot_field::kPhase, static_cast<uint32_t>(phase));
}

}  // namespace aoe2::pad
