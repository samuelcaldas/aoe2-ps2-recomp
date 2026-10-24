// kt_libpad: setup, port selection and the engine-facing API.
// Origin: decomp src/kcet/kt_libpad.c; see kt_libpad_model.hpp for the contract.
#include <aoe2/pad/kt_libpad_model.hpp>

#include <vector>

namespace aoe2::pad {
namespace {

constexpr const char* kSourceFile = "kt_libpad.c";
constexpr const char* kPadRange = "pad_number >= 0 && pad_number < W.n_pad";
constexpr uint32_t kWorkSlots = 0x00;
constexpr uint32_t kWorkCount = 0x04;
constexpr uint32_t kWorkSemaphore = 0x08;
constexpr uint32_t kWorkSize = 16;
constexpr int kMaxActuators = 6;
constexpr uint16_t kUnusedPair = 0xFFFF;

}  // namespace

KtLibpadModel::KtLibpadModel(PadGuestMemory& memory, PadDevice& device, KtLibpadGlobals globals)
    : memory_(memory), device_(device), globals_(globals) {}

// ---------------------------------------------------------------- helpers

void KtLibpadModel::Assert(int line, const char* expression) {
    memory_.ReportAssertion(kSourceFile, line, expression);
}

bool KtLibpadModel::ValidPad(int pad, int line) { return ValidPad(pad, line, kPadRange); }

bool KtLibpadModel::ValidPad(int pad, int line, const char* expression) {
    if (pad >= 0 && pad < static_cast<int>(WorkPadCount())) return true;
    Assert(line, expression);
    return false;  // the original reporter halts; nothing is touched afterwards
}

uint32_t KtLibpadModel::Field32(uint32_t slot, uint32_t offset) { return memory_.Load32(slot + offset); }

void KtLibpadModel::SetField32(uint32_t slot, uint32_t offset, uint32_t value) {
    memory_.Store32(slot + offset, value);
}

uint32_t KtLibpadModel::WorkPadCount() { return memory_.Load32(globals_.work + kWorkCount); }

void KtLibpadModel::Zero(uint32_t address, uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) memory_.Store8(address + i, 0);
}

// ---------------------------------------------------------------- setup

// func_00125A70
uint32_t KtLibpadModel::SlotAddress(int index) {
    if (static_cast<uint32_t>(index) >= WorkPadCount()) return 0;
    return memory_.Load32(globals_.work + kWorkSlots) + (static_cast<uint32_t>(index) << 9);
}

// func_00125AE8: all actuators off (the original passes a zeroed constant buffer)
int KtLibpadModel::StopActuators(int port, int slot) {
    const uint8_t off[6] = {};
    return device_.SetActDirect(port, slot, off);
}

// func_00125AF8
int KtLibpadModel::ClosePads() {
    int result = 1;
    const int count = static_cast<int>(WorkPadCount());
    for (int i = 0; i < count; ++i) {
        const uint32_t slot = SlotAddress(i);
        const int port = static_cast<int>(Field32(slot, slot_field::kPort));
        const int unit = static_cast<int>(Field32(slot, slot_field::kSlot));
        if (port == -1 || unit == -1) continue;
        if (Field32(slot, slot_field::kFlags) & slot_flag::kActuatorsLive) StopActuators(port, unit);
        if (device_.PortClose(port, unit) != 1) continue;
        result = 0;
        SetField32(slot, slot_field::kSlot, static_cast<uint32_t>(-1));
        SetField32(slot, slot_field::kPort, static_cast<uint32_t>(-1));
    }
    if (result != 0 && device_.End() != 1) result = 0;
    return result;
}

// func_00125C58. Skipped natively: SIF init, semaphore creation (the semaphore field holds -1) and heap
// allocation of the slot array (the game passes a static buffer).
void KtLibpadModel::Init(int rpc_id, int pad_count, uint32_t slots, int flag) {
    if (pad_count <= 0) Assert(0x166, "n_pad > 0");
    Zero(globals_.work, kWorkSize);
    memory_.Store32(globals_.work + kWorkSemaphore, static_cast<uint32_t>(-1));
    if (device_.Init(rpc_id) != 1) return;
    device_.Start(0);
    if (slots == 0) {
        Assert(0x17A, "p != NULL");  // heap allocation is not supported natively
        return;
    }
    memory_.Store32(globals_.work + kWorkSlots, slots);
    memory_.Store32(globals_.work + kWorkCount, static_cast<uint32_t>(pad_count));
    if (pad_count <= 0) return;
    for (int i = 0; i < pad_count; ++i) InitSlot(slots + static_cast<uint32_t>(i) * slot_field::kSize);
    AssignPorts(slots, pad_count, flag);
}

// func_00125C38
void KtLibpadModel::InitDefault(int rpc_id, int pad_count, uint32_t slots) { Init(rpc_id, pad_count, slots, 0); }

// func_00125DB8, func_00125DC8, func_00125DD8
void KtLibpadModel::ClearActAlign(uint32_t slot) { Zero(slot + slot_field::kActAlign, 12); }
void KtLibpadModel::ClearModeInfo(uint32_t slot) { Zero(slot + slot_field::kModeInfo, 24); }
void KtLibpadModel::ClearModeWord(uint32_t slot) { Zero(slot + slot_field::kModeWord, 4); }

// func_00125DE8: the first two bytes of each of the six actuator triples
void KtLibpadModel::ClearActConfigValues(uint32_t slot) {
    for (uint32_t i = 0; i < kMaxActuators; ++i) {
        memory_.Store8(slot + slot_field::kActConfig + i * 3, 0);
        memory_.Store8(slot + slot_field::kActConfig + i * 3 + 1, 0);
    }
}

// func_00125E20
void KtLibpadModel::InvalidateState(uint32_t slot) { SetField32(slot, slot_field::kState, static_cast<uint32_t>(-1)); }

// func_00125E30
void KtLibpadModel::ResetButtons(uint32_t slot) {
    memory_.Store16(slot + slot_field::kButtons, 0);
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kPressPending);
    memory_.Store16(slot + slot_field::kPressed, 0);
    memory_.Store16(slot + slot_field::kReleased, 0);
    SetField32(slot, slot_field::kAnalogHigh, 0);
    SetField32(slot, slot_field::kAnalogLow, 0);
    SetField32(slot, slot_field::kModeId, 0);
    SetField32(slot, slot_field::kErrors, 0);
    SetField32(slot, slot_field::kPhase, 0);
    ClearActConfigValues(slot);
}

// func_00125E68
void KtLibpadModel::FullReset(uint32_t slot) {
    ClearActAlign(slot);
    ClearModeInfo(slot);
    ClearModeWord(slot);
    ResetButtons(slot);
    if (Field32(slot, slot_field::kActMode) == 0) return;
    SetField32(slot, slot_field::kModel, 0);
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kModePending);
}

// func_00125EC0
void KtLibpadModel::InitSlot(uint32_t slot) {
    Zero(slot, slot_field::kSize);
    SetField32(slot, slot_field::kActMode, 0);
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kDataDisabled);
    SetField32(slot, slot_field::kActParam, 0);
    SetField32(slot, slot_field::kEnableMask, 0x90);
    SetField32(slot, slot_field::kReqSlot, static_cast<uint32_t>(-1));
    SetField32(slot, slot_field::kPort, static_cast<uint32_t>(-1));
    SetField32(slot, slot_field::kSlot, static_cast<uint32_t>(-1));
    InvalidateState(slot);
    SetField32(slot, slot_field::kReqPort, static_cast<uint32_t>(-1));
    for (uint32_t i = 0; i < kMaxActuators; ++i) memory_.Store8(slot + 0x63 - i * 3, 0);
}

// func_00125F48
void KtLibpadModel::QueryPrimarySlots() { device_.SlotMax(0); }

// func_00125F68
void KtLibpadModel::AssignPorts(uint32_t slots, int pad_count, int flag) {
    if (static_cast<uint32_t>(pad_count) >= 3) {
        Assert(0x255, "n_pad <= numberof( buf )");
        return;
    }
    if (slots == 0) slots = SlotAddress(0);
    int slots_per_port[2];
    for (int i = 0; i < 2; ++i) slots_per_port[i] = device_.SlotMax(i);
    uint16_t pairs[16];
    for (int i = 0; i < pad_count; ++i) pairs[i * 2] = pairs[i * 2 + 1] = kUnusedPair;
    if (flag == 1) {
        int single[8];
        for (int i = 0; i < 2; ++i) single[i] = 1;
        const int first = EnumeratePorts(single, 0, pairs, pad_count, 2);
        EnumeratePorts(slots_per_port, 1, pairs + first * 2, pad_count - first, 2);
    } else if (flag < 2) {
        int single[8];
        for (int i = 0; i < 2; ++i) single[i] = 1;
        EnumeratePorts(single, 0, pairs, pad_count, 2);
    } else if (flag == 2) {
        EnumeratePorts(slots_per_port, 0, pairs, pad_count, 2);
    }
    for (int i = 0; i < pad_count; ++i) {
        const uint32_t slot = slots + static_cast<uint32_t>(i) * slot_field::kSize;
        SetField32(slot, slot_field::kReqPort, static_cast<uint32_t>(static_cast<int16_t>(pairs[i * 2])));
        SetField32(slot, slot_field::kReqSlot, static_cast<uint32_t>(static_cast<int16_t>(pairs[i * 2 + 1])));
    }
}

namespace {
// Reads `count` words of a guest int array.
std::vector<int> ReadInts(PadGuestMemory& memory, uint32_t address, int count) {
    std::vector<int> values;
    for (int i = 0; i < count; ++i) values.push_back(static_cast<int>(memory.Load32(address + static_cast<uint32_t>(i) * 4)));
    return values;
}

void WritePairs(PadGuestMemory& memory, uint32_t address, const uint16_t* pairs, int count) {
    for (int i = 0; i < count * 2; ++i) memory.Store16(address + static_cast<uint32_t>(i) * 2, pairs[i]);
}
}  // namespace

// func_001262E8 on guest memory
int KtLibpadModel::EnumeratePortsGuest(uint32_t slots_per_port, int start, uint32_t out_pairs, int max_items, int port_count) {
    if (out_pairs == 0) return 0;
    if (port_count <= 0 || max_items <= 0) return 0;
    std::vector<uint16_t> pairs(static_cast<size_t>(max_items) * 2);
    const std::vector<int> slots = ReadInts(memory_, slots_per_port, port_count);
    const int count = EnumeratePorts(slots.data(), start, pairs.data(), max_items, port_count);
    WritePairs(memory_, out_pairs, pairs.data(), count);
    return count;
}

// func_00126140
int KtLibpadModel::SinglePairs(uint32_t out_pairs, int pad_count, int port_count, uint32_t) {
    if (static_cast<uint32_t>(port_count) >= 3) {
        Assert(0x2B8, "port_max <= numberof( dummy_slot_data )");
        return 0;
    }
    std::vector<int> one(8, 1);
    if (out_pairs == 0 || pad_count <= 0) return 0;
    std::vector<uint16_t> pairs(static_cast<size_t>(pad_count) * 2);
    const int count = EnumeratePorts(one.data(), 0, pairs.data(), pad_count, port_count);
    WritePairs(memory_, out_pairs, pairs.data(), count);
    return count;
}

// func_001261E0
int KtLibpadModel::BothPairs(uint32_t out_pairs, int pad_count, int port_count, uint32_t slots_per_port) {
    const int first = SinglePairs(out_pairs, pad_count, port_count, slots_per_port);
    const int second = EnumeratePortsGuest(slots_per_port, 1, out_pairs + static_cast<uint32_t>(first) * 4,
                                           pad_count - first, port_count);
    return first + second;
}

// func_00126258
int KtLibpadModel::PortPairs(uint32_t out_pairs, int pad_count, int port_count, uint32_t slots_per_port) {
    return EnumeratePortsGuest(slots_per_port, 0, out_pairs, pad_count, port_count);
}

// func_00126040
void KtLibpadModel::BuildPortList(uint32_t out_pairs, int pad_count, int port_count, int flag, uint32_t slots_per_port) {
    if (out_pairs == 0) {
        Assert(0x298, "p != NULL");
        return;
    }
    for (int i = 0; i < pad_count; ++i) {
        memory_.Store16(out_pairs + static_cast<uint32_t>(i) * 4, kUnusedPair);
        memory_.Store16(out_pairs + static_cast<uint32_t>(i) * 4 + 2, kUnusedPair);
    }
    if (flag == 1) BothPairs(out_pairs, pad_count, port_count, slots_per_port);
    else if (flag < 2) SinglePairs(out_pairs, pad_count, port_count, slots_per_port);
    else if (flag == 2) PortPairs(out_pairs, pad_count, port_count, slots_per_port);
}

// func_00126288
void KtLibpadModel::QuerySlotCounts(uint32_t slots_per_port, int count) {
    for (int i = 0; i < count; ++i)
        memory_.Store32(slots_per_port + static_cast<uint32_t>(i) * 4, static_cast<uint32_t>(device_.SlotMax(i)));
}

// ---------------------------------------------------------------- engine-facing API

// func_00126350
void KtLibpadModel::Query(int pad, int command) {
    if (pad >= static_cast<int>(WorkPadCount())) Assert(0x319, "pad_number < W.n_pad");
    switch (command) {
        case 0: Buttons(pad); break;
        case 1: ButtonsPressed(pad); break;
        case 2: ButtonsReleased(pad); break;
        case 3: AnalogHigh(pad); break;
        case 4: AnalogLow(pad); break;
        default: break;
    }
}

uint32_t KtLibpadModel::Buttons(int pad) {  // func_00126410
    if (!ValidPad(pad, 0x32C)) return 0;
    return memory_.Load16(SlotAddress(pad) + slot_field::kButtons);
}

uint32_t KtLibpadModel::ButtonsPressed(int pad) {  // func_00126488
    if (!ValidPad(pad, 0x33B)) return 0;
    return memory_.Load16(SlotAddress(pad) + slot_field::kPressed);
}

uint32_t KtLibpadModel::ButtonsReleased(int pad) {  // func_00126500
    if (!ValidPad(pad, 0x34A)) return 0;
    return memory_.Load16(SlotAddress(pad) + slot_field::kReleased);
}

uint32_t KtLibpadModel::AnalogHigh(int pad) {  // func_00126578
    if (!ValidPad(pad, 0x359)) return 0;
    return Field32(SlotAddress(pad), slot_field::kAnalogHigh);
}

uint32_t KtLibpadModel::AnalogLow(int pad) {  // func_001265F0
    if (!ValidPad(pad, 0x368)) return 0;
    return Field32(SlotAddress(pad), slot_field::kAnalogLow);
}

void KtLibpadModel::EnableRumble(int pad) {  // func_00126668
    if (!ValidPad(pad, 0x378)) return;
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kRumbleEnabled);
}

void KtLibpadModel::DisableRumble(int pad) {  // func_001266D8
    if (!ValidPad(pad, 0x386)) return;
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) & ~slot_flag::kRumbleEnabled);
}

// func_00126750: value2 is the first byte of the actuator triple, value1 the second
void KtLibpadModel::SetActuator(int pad, int actuator, int value1, int value2) {
    if (!ValidPad(pad, 0x398)) return;
    if (actuator >= kMaxActuators) {
        Assert(0x39B, "act_number < KT_PAD_MAX_ACTUATER");
        return;
    }
    const uint32_t triple = SlotAddress(pad) + slot_field::kActConfig + static_cast<uint32_t>(actuator * 3);
    memory_.Store8(triple, static_cast<uint8_t>(value2));
    memory_.Store8(triple + 1, static_cast<uint8_t>(value1));
}

// func_00126810
void KtLibpadModel::SetActuatorAlign(int pad, uint32_t table) {
    if (!ValidPad(pad, 0x3AD)) return;
    const uint32_t slot = SlotAddress(pad);
    for (uint32_t i = 0; i < kMaxActuators; ++i)
        memory_.Store8(slot + slot_field::kActAlign + i * 2, memory_.Load8(table + i));
    MarkPadChanged(pad);
}

// func_001268C0, func_001268D0
void KtLibpadModel::MarkChanged(uint32_t slot) {
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kChanged);
}

void KtLibpadModel::MarkPadChanged(int pad) {
    if (!ValidPad(pad, 0x3C7)) return;
    MarkChanged(SlotAddress(pad));
}

// func_00126938, func_001269B0
void KtLibpadModel::SetEnableMask(int pad, uint32_t mask) {
    if (!ValidPad(pad, 0x3D4)) return;
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kEnableMask, Field32(slot, slot_field::kEnableMask) | mask);
}

void KtLibpadModel::ClearEnableMask(int pad, uint32_t mask) {
    if (!ValidPad(pad, 0x3E1)) return;
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kEnableMask, Field32(slot, slot_field::kEnableMask) & ~mask);
}

// func_00126A30
void KtLibpadModel::SetActData(int pad, int channel, uint32_t data) {
    if (!ValidPad(pad, 0x3EE)) return;
    if (channel >= 4) {
        Assert(0x3F1, "channel < 4");
        return;
    }
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kActData + static_cast<uint32_t>(channel) * 4, memory_.Load32(data));
}

// func_00126AE8
void KtLibpadModel::SetRapid(int pad, uint32_t table, int count) {
    if (!ValidPad(pad, 0x405)) return;
    if (count >= 4) {
        Assert(0x406, "n_rpd <= KT_PAD_RAPID_GRP");
        return;
    }
    const uint32_t slot = SlotAddress(pad);
    for (int i = 0; i < count; ++i) {
        const uint32_t entry = table + static_cast<uint32_t>(i) * 4;
        SetRapidEntry(slot, i, memory_.Load16(entry), memory_.Load8(entry + 2), memory_.Load8(entry + 3));
    }
}

// func_00126BC0: the binary stores a 16-bit mask, a zero counter and the packed mode/speed byte
void KtLibpadModel::SetRapidEntry(uint32_t slot, int channel, int mask, int mode, int speed) {
    const uint32_t entry = slot + slot_field::kRapid + static_cast<uint32_t>(channel) * 4;
    memory_.Store16(entry, static_cast<uint16_t>(mask));
    memory_.Store8(entry + 2, 0);
    memory_.Store8(entry + 3, static_cast<uint8_t>(((mode + 15) & 0xF0) | (speed & 0xF)));
}

// func_00126C30
int KtLibpadModel::Status(int pad) {
    if (!ValidPad(pad, 0x429)) return 0;
    const uint32_t slot = SlotAddress(pad);
    const int state = static_cast<int>(Field32(slot, slot_field::kState));
    const int request_port = static_cast<int>(Field32(slot, slot_field::kReqPort));
    const int request_slot = static_cast<int>(Field32(slot, slot_field::kReqSlot));
    if (state < 3) {
        if (state > 0) return IsReady(slot) ? 4 : 3;
        if (state == -1) return request_port == state && request_slot == state ? 1 : 0;
        return state == 0 ? 2 : 0;
    }
    if (state == 6 || state == 0x63) return 1;
    if (state == 5 || state == 7) return IsReady(slot) ? 4 : 3;
    return 0;
}

// func_00126D50, func_00126D60, func_00126DD8
int KtLibpadModel::IsReady(uint32_t slot) {
    return static_cast<int>(Field32(slot, slot_field::kFlags) & slot_flag::kReady);
}

int KtLibpadModel::IsPadReady(int pad) {
    if (!ValidPad(pad, 0x47C)) return 0;
    return IsReady(SlotAddress(pad));
}

int KtLibpadModel::ModeId(int pad) {
    if (!ValidPad(pad, 0x48B)) return 0;
    const uint32_t slot = SlotAddress(pad);
    return IsReady(slot) ? static_cast<int>(Field32(slot, slot_field::kModeId)) : 0;
}

// func_00126E68
void KtLibpadModel::SetActMode(int pad, int mode, int param) {
    if (!ValidPad(pad, 0x49E)) return;
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kActMode, static_cast<uint32_t>(mode));
    SetField32(slot, slot_field::kActParam, static_cast<uint32_t>(param));
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kModePending);
}

// func_00126EF8
int KtLibpadModel::FindModeIndexForPad(int pad, int type) {
    if (!ValidPad(pad, 0x4B2)) return -2;
    if (IsPadReady(pad) == 0) return -2;
    const uint32_t slot = SlotAddress(pad);
    return FindModeIndex(static_cast<int>(Field32(slot, slot_field::kPort)), static_cast<int>(Field32(slot, slot_field::kSlot)), type);
}

// func_00126FA8
void KtLibpadModel::SetPressMode(int pad, int mode) {
    if (!ValidPad(pad, 0x4C4)) return;
    if (static_cast<uint32_t>(mode) >= 3) {
        Assert(0x4C7, "mode >= KT_PAD_PRESS_OFF && mode < N_KT_PAD_PRESS_MODE");
        return;
    }
    const uint32_t slot = SlotAddress(pad);
    SetField32(slot, slot_field::kPressMode, static_cast<uint32_t>(mode));
    SetField32(slot, slot_field::kFlags, Field32(slot, slot_field::kFlags) | slot_flag::kPressPending);
}

// func_00127048
int KtLibpadModel::IsPressureModel(int pad) {
    if (!ValidPad(pad, 0x4E8)) return -1;
    if (IsPadReady(pad) == 0) return -1;
    return IsPressureModeId(static_cast<int>(Field32(SlotAddress(pad), slot_field::kModel)));
}

// func_001270E8
int KtLibpadModel::DataByte(int pad, int offset) {
    if (!ValidPad(pad, 0x4F9)) return 0;
    if (static_cast<uint32_t>(offset) >= 12) {
        Assert(0x4FC, "offset >= 0 && offset < KT_PAD_N_PRESS");
        return 0;
    }
    return memory_.Load8(SlotAddress(pad) + slot_field::kData + static_cast<uint32_t>(offset));
}

// func_00127188: copies 12 bytes from `source` into the slot data area (as the decomp does)
int KtLibpadModel::StoreData(int pad, uint32_t source) {
    if (!ValidPad(pad, 0x509)) return 0;
    StoreSlotData(SlotAddress(pad), source, 12);
    return 12;
}

// func_001271E8
int KtLibpadModel::HasPressure(int pad) {
    if (!ValidPad(pad, 0x515)) return -1;
    if (IsPadReady(pad) == 0) return -1;
    return static_cast<int>((Field32(SlotAddress(pad), slot_field::kFlags) >> 19) & 1);
}

// func_00127288
void KtLibpadModel::ClearActuator(int pad, int actuator) {
    if (!ValidPad(pad, 0x529)) return;
    if (static_cast<uint32_t>(actuator + 1) >= 7) {
        Assert(0x52C, "act_number >= -1 && act_number < KT_PAD_MAX_ACTUATER");
        return;
    }
    const uint32_t slot = SlotAddress(pad);
    const int first = actuator == -1 ? 0 : actuator;
    const int last = actuator == -1 ? kMaxActuators : actuator + 1;
    for (int i = first; i < last; ++i) memory_.Store8(slot + slot_field::kActConfig + static_cast<uint32_t>(i) * 3, 0);
}

// func_00127370
int KtLibpadModel::PadCount() { return static_cast<int>(WorkPadCount()); }

// func_001289C0
void KtLibpadModel::ResetPortTable() {
    Zero(globals_.port_table, 16);
    for (uint32_t i = 0; i < 2; ++i) {
        memory_.Store32(globals_.port_table + i * 8, static_cast<uint32_t>(-1));
        memory_.Store32(globals_.port_table + i * 8 + 4, static_cast<uint32_t>(-1));
    }
}

// func_00128A18
int KtLibpadModel::NoUnitBusy(int) { return 0; }

// func_00128A20: strchr on a guest string
uint32_t KtLibpadModel::FindChar(uint32_t text, int ch) {
    for (uint32_t cursor = text; memory_.Load8(cursor) != 0; ++cursor) {
        if (memory_.Load8(cursor) == static_cast<uint8_t>(ch)) return cursor;
    }
    return 0;
}

// func_00128990
uint64_t KtLibpadModel::VibrationMask(uint64_t value) {
    value = (value & 0xFFFF0000ull) | vibration_0_;
    return (value & 0xFFFFFFFFull) | (static_cast<uint64_t>(vibration_1_) << 16);
}

}  // namespace aoe2::pad
