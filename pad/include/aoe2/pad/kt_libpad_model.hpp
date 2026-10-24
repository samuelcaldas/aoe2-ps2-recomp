// Native port of the KCET gamepad module (kt_libpad) over guest memory.
//
// Origin: samuelcaldas/age-of-empires-2-decomp, src/kcet/kt_libpad.c (main 20fbbd5a5, working tree with the
// pad corrections). Where the decomp and the recompiled binary disagree the binary wins; the slot layout below
// follows the binary (see docs/sot/pad-libpad-semantics.md).
//
// The port runs atomically inside one handler, so the module's semaphore lock (func_00125AA8/func_00125AC8)
// is not taken. Heap allocation of the slot array and the async thread helpers stay generated code.
#pragma once

#include <aoe2/pad/pad_device.hpp>
#include <aoe2/pad/pad_guest_memory.hpp>

#include <array>
#include <cstdint>

namespace aoe2::pad {

// Offsets inside the 0x200-byte PadSlot, taken from the binary.
namespace slot_field {
inline constexpr uint32_t kButtons = 0x00;
inline constexpr uint32_t kPressed = 0x02;
inline constexpr uint32_t kReleased = 0x04;
inline constexpr uint32_t kAnalogHigh = 0x08;
inline constexpr uint32_t kAnalogLow = 0x0C;
inline constexpr uint32_t kData = 0x10;          // 12 bytes of pressure data
inline constexpr uint32_t kModeInfo = 0x1C;      // 6 actuators x 4 descriptor bytes
inline constexpr uint32_t kModeWord = 0x34;
inline constexpr uint32_t kRapid = 0x38;         // 3 channels x 4 bytes
inline constexpr uint32_t kActData = 0x44;       // 4 channels x 4 bytes
inline constexpr uint32_t kActConfigTypes = 0x54; // type byte of each actuator triple
inline constexpr uint32_t kActConfig = 0x55;     // first value byte of each actuator triple
inline constexpr uint32_t kEnableMask = 0x68;
inline constexpr uint32_t kPort = 0x6C;
inline constexpr uint32_t kSlot = 0x70;
inline constexpr uint32_t kReqPort = 0x74;
inline constexpr uint32_t kReqSlot = 0x78;
inline constexpr uint32_t kActMode = 0x7C;
inline constexpr uint32_t kActParam = 0x80;
inline constexpr uint32_t kRetry = 0x84;
inline constexpr uint32_t kPhase = 0x88;
inline constexpr uint32_t kState = 0x8C;
inline constexpr uint32_t kStep = 0x90;
inline constexpr uint32_t kModeId = 0x94;
inline constexpr uint32_t kErrors = 0x98;
inline constexpr uint32_t kPressMode = 0x9C;
inline constexpr uint32_t kFailures = 0xA0;
inline constexpr uint32_t kActAlign = 0xA4;      // 6 actuators x 2 bytes
inline constexpr uint32_t kFlags = 0xB0;
inline constexpr uint32_t kActExec = 0xB4;       // 2 words
inline constexpr uint32_t kModel = 0xBC;
inline constexpr uint32_t kReadLength = 0xC0;
inline constexpr uint32_t kReport = 0xC4;        // 32 bytes read from the pad
inline constexpr uint32_t kLibraryBuffer = 0x100;// 256 bytes owned by the pad library
inline constexpr uint32_t kSize = 0x200;
}  // namespace slot_field

namespace slot_flag {
inline constexpr uint32_t kChanged = 0x10;
inline constexpr uint32_t kModePending = 0x20;
inline constexpr uint32_t kPressPending = 0x40;
inline constexpr uint32_t kRumbleEnabled = 0x08;
inline constexpr uint32_t kPortChanged = 0x02;
inline constexpr uint32_t kDataDisabled = 0x04;
inline constexpr uint32_t kReportFailed = 0x10000;
inline constexpr uint32_t kActuatorsLive = 0x20000;
inline constexpr uint32_t kReady = 0x40000;
inline constexpr uint32_t kNoPressure = 0x80000;
}  // namespace slot_flag

struct KtLibpadGlobals {
    uint32_t actuator_types = 0x004E2BD8;  // GP-relative table, 2 bytes per actuator type
    uint32_t work = 0x004EA148;            // PadWork: slots, n_pad, sema, flags
    uint32_t port_table = 0x004EA158;      // 4 words, -1 when unused
    uint32_t step_table = 0x0038C7B8;      // 3 guest function addresses
    uint32_t analog_default = 0x0038C7C8;  // 12 bytes used for pressure-mode ids
};

// Guest addresses of the functions the step table can name.
inline constexpr uint32_t kOpenPortAddress = 0x00127488;
inline constexpr uint32_t kPollStateAddress = 0x001275A0;
inline constexpr uint32_t kClosePortAddress = 0x00127750;

// Pure helpers (func_00127D88 and friends).
int ScaleValue(int value, int scale, int min_value, int max_value);
int ScaleClamp(int value, int min_value, int max_value);
int ScaleWithThreshold(int value, int low, int high, int center);
int ScaleByMode(int value, int mode, int a, int b, int c);
bool IsPressureModeId(int id);
bool IsExtendedMode(int value);
int EnumeratePorts(const int* slots_per_port, int start, uint16_t* out_pairs, int max_items, int port_count);

class KtLibpadModel {
public:
    KtLibpadModel(PadGuestMemory& memory, PadDevice& device, KtLibpadGlobals globals = {});

    // ---- setup and teardown
    uint32_t SlotAddress(int index);                                             // func_00125A70
    int StopActuators(int port, int slot);                                       // func_00125AE8
    int ClosePads();                                                             // func_00125AF8
    void Init(int rpc_id, int pad_count, uint32_t slots, int flag);              // func_00125C58
    void InitDefault(int rpc_id, int pad_count, uint32_t slots);                 // func_00125C38
    void ClearActAlign(uint32_t slot);                                           // func_00125DB8
    void ClearModeInfo(uint32_t slot);                                           // func_00125DC8
    void ClearModeWord(uint32_t slot);                                           // func_00125DD8
    void ClearActConfigValues(uint32_t slot);                                    // func_00125DE8
    void InvalidateState(uint32_t slot);                                         // func_00125E20
    void ResetButtons(uint32_t slot);                                            // func_00125E30
    void FullReset(uint32_t slot);                                               // func_00125E68
    void InitSlot(uint32_t slot);                                                // func_00125EC0
    void QueryPrimarySlots();                                                    // func_00125F48
    void AssignPorts(uint32_t slots, int pad_count, int flag);                   // func_00125F68
    void BuildPortList(uint32_t out_pairs, int pad_count, int port_count, int flag, uint32_t slots_per_port);  // func_00126040
    int SinglePairs(uint32_t out_pairs, int pad_count, int port_count, uint32_t slots_per_port);  // func_00126140
    int BothPairs(uint32_t out_pairs, int pad_count, int port_count, uint32_t slots_per_port);    // func_001261E0
    int PortPairs(uint32_t out_pairs, int pad_count, int port_count, uint32_t slots_per_port);    // func_00126258
    void QuerySlotCounts(uint32_t slots_per_port, int count);                    // func_00126288
    int EnumeratePortsGuest(uint32_t slots_per_port, int start, uint32_t out_pairs, int max_items, int port_count);  // func_001262E8

    // ---- engine-facing API
    void Query(int pad, int command);                                            // func_00126350
    uint32_t Buttons(int pad);                                                   // func_00126410
    uint32_t ButtonsPressed(int pad);                                            // func_00126488
    uint32_t ButtonsReleased(int pad);                                           // func_00126500
    uint32_t AnalogHigh(int pad);                                                // func_00126578
    uint32_t AnalogLow(int pad);                                                 // func_001265F0
    void EnableRumble(int pad);                                                  // func_00126668
    void DisableRumble(int pad);                                                 // func_001266D8
    void SetActuator(int pad, int actuator, int value1, int value2);             // func_00126750
    void SetActuatorAlign(int pad, uint32_t table);                              // func_00126810
    void MarkChanged(uint32_t slot);                                             // func_001268C0
    void MarkPadChanged(int pad);                                                // func_001268D0
    void SetEnableMask(int pad, uint32_t mask);                                  // func_00126938
    void ClearEnableMask(int pad, uint32_t mask);                                // func_001269B0
    void SetActData(int pad, int channel, uint32_t data);                        // func_00126A30
    void SetRapid(int pad, uint32_t table, int count);                           // func_00126AE8
    void SetRapidEntry(uint32_t slot, int channel, int mask, int mode, int speed);  // func_00126BC0
    int Status(int pad);                                                         // func_00126C30
    int IsReady(uint32_t slot);                                                  // func_00126D50
    int IsPadReady(int pad);                                                     // func_00126D60
    int ModeId(int pad);                                                         // func_00126DD8
    void SetActMode(int pad, int mode, int param);                               // func_00126E68
    int FindModeIndexForPad(int pad, int type);                                  // func_00126EF8
    void SetPressMode(int pad, int mode);                                        // func_00126FA8
    int IsPressureModel(int pad);                                                // func_00127048
    int DataByte(int pad, int offset);                                           // func_001270E8
    int StoreData(int pad, uint32_t source);                                     // func_00127188
    int HasPressure(int pad);                                                    // func_001271E8
    void ClearActuator(int pad, int actuator);                                   // func_00127288
    int PadCount();                                                              // func_00127370

    // ---- per-frame update and state machine
    void Update();                                                               // func_00127380
    void StepSlot(uint32_t slot);                                                // func_001273D8
    int OpenPort(uint32_t slot);                                                 // func_00127488
    int PollState(uint32_t slot);                                                // func_001275A0
    int NoPendingWork(uint32_t slot);                                            // func_001276B0
    int PortChanged(uint32_t slot);                                              // func_001276B8
    int ClosePort(uint32_t slot);                                                // func_00127750
    void SetReportFailed(uint32_t slot);                                         // func_001277E0
    void ClearReportFailed(uint32_t slot);                                       // func_001277F8
    int ReportFailed(uint32_t slot);                                             // func_00127810
    void ReadActuatorInfo(uint32_t slot);                                        // func_00127820
    int ReadModeWord(uint32_t slot);                                             // func_00127900
    void StateDisconnected(uint32_t slot);                                       // func_001279A0
    void StateFindPad(uint32_t slot);                                            // func_001279A8
    void StateFindController(uint32_t slot);                                     // func_001279B0
    void StateExecutingCommand(uint32_t slot);                                   // func_001279B8
    void StateStable(uint32_t slot);                                             // func_001279C0
    void ProcessPhases(uint32_t slot, int flag);                                 // func_001279C8
    void CountError(uint32_t slot);                                              // func_00127BD8
    int ReadReport(uint32_t slot);                                               // func_00127BE8
    int DataEnabled(uint32_t slot);                                              // func_00127C68
    void StoreButtons(uint32_t slot, int held, int pressed, int released);       // func_00127C88
    void StoreSlotData(uint32_t slot, uint32_t source, int length);              // func_00127C98
    int ProcessReport(uint32_t slot, int flag);                                  // func_00127E68
    int FindAlignment(uint32_t entry, int index);                                // func_00128130
    int NeedsAlignment(uint32_t slot);                                           // func_001281B0
    int ConfigureAlignment(uint32_t slot);                                       // func_001281C0
    void UpdateActuators(uint32_t slot);                                         // func_00128390
    int FindModeIndex(int port, int slot, int type);                             // func_001285F8
    int ModePending(uint32_t slot);                                              // func_00128698
    int ApplyMode(uint32_t slot);                                                // func_001286A8
    int PressPending(uint32_t slot);                                             // func_001287D8
    int PressModeA(uint32_t slot);                                               // func_001287E8
    int PressModeB(uint32_t slot);                                               // func_00128820
    void ClearPressPending(uint32_t slot);                                       // func_00128850
    int ApplyPressMode(uint32_t slot);                                           // func_00128878
    uint64_t VibrationMask(uint64_t value);                                      // func_00128990
    void ResetPortTable();                                                       // func_001289C0
    int NoUnitBusy(int port);                                                    // func_00128A18
    uint32_t FindChar(uint32_t text, int ch);                                    // func_00128A20

private:
    bool ValidPad(int pad, int line);
    bool ValidPad(int pad, int line, const char* expression);
    void Assert(int line, const char* expression);
    uint32_t Field32(uint32_t slot, uint32_t offset);
    void SetField32(uint32_t slot, uint32_t offset, uint32_t value);
    uint32_t WorkPadCount();
    void Zero(uint32_t address, uint32_t length);

    PadGuestMemory& memory_;
    PadDevice& device_;
    KtLibpadGlobals globals_;
    uint16_t vibration_0_ = 0;  // s_vibe_0
    uint16_t vibration_1_ = 0;  // s_vibe_1
};

}  // namespace aoe2::pad
