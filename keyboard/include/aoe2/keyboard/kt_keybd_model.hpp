// Origin: samuelcaldas/age-of-empires-2-decomp, kt_keybd.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Binary-checked guest-memory port.
#pragma once
#include <aoe2/runtime/guest_access.hpp>

namespace aoe2::keyboard {
class KeyboardRpc {
public:
    virtual ~KeyboardRpc() = default;
    virtual int Info(uint32_t reply) = 0;
    virtual int Read(int port, uint32_t reply) = 0;
    virtual int DeviceInfo(int port, uint32_t reply) = 0;
    virtual int SetLed(int port, int mask) = 0;
    virtual int SetCapsType(int port, int type) = 0;
};
class KtKeybdModel {
public:
    KtKeybdModel(runtime::GuestAccess& memory, KeyboardRpc& service);
    void Init(int count);                           // func_00135EC0
    void WorkPadding();                            // func_00135F90: alignment return
    int NextIndex(int port, int index);             // func_00136058
    void AdvanceWrite(int port);                    // func_00136080
    uint32_t WriteEntry(int port);                  // func_001360D0
    void AdvanceRead(int port);                     // func_00136108
    uint32_t ReadEntry(int port);                   // func_00136158
    int PortState(uint32_t address);                // func_00136228
    int State(int port);                            // func_00136230
    int PortType(uint32_t address);                 // func_001362B8
    int Status(int port);                           // func_001362D8
    void DevicePadding();                          // func_00137200: alignment return
    int ReadPacket(int port, uint32_t output);       // func_00136190
    int ReadPayload(int port, uint32_t output);      // func_00136360
    int ResetPorts(int force);                      // func_00136450
    void RequestRepeat(int port, int value);         // func_00136508
    void RequestLed(int port, int value);            // func_00136598
    uint32_t FindFreePort();                         // func_00136690
    void ResetQueue(uint32_t address);               // func_001366F8
    int Allocate(int type);                          // func_00136730
    void Release(int port, int type);                // func_00136798
    bool IsAllocated(uint32_t address);              // func_00136840
    int SetState(int port, int state);               // func_00136850
    int SetPayload(int port, uint32_t source, int size); // func_00136908
    int PostPacket(int port, int kind, int modifier, uint32_t source, int size); // func_001369E8
    int RepeatRequest(int port);                     // func_00136B50
    void ClearRepeatRequest(int port, int value);    // func_00136BE0
    int LedRequest(int port);                        // func_00136C90
    void ClearLedRequest(int port, int value);       // func_00136D20
    void InitRecord(uint32_t address, int value);    // func_00136DD0
    void InitRecords(uint32_t address, int count, int stride); // func_00136DE8
    void ReconcileRecords(uint32_t address, int count, int stride, int type); // func_00136E50
    void ApplyInfo(uint32_t address, int count, int stride, int type, uint32_t reply); // func_00136ED0
    void InitProcessing(int count);                  // func_00136FA0
    void ProcessEvents();                            // func_00137000
private:
    void WritePacket(uint32_t entry, int kind, int modifier, uint32_t source, int size);
    void CopyBytes(uint32_t destination, uint32_t source, uint32_t size);
    void ApplyRequests(int device, int port);
    void DrainEvents(int device, int port);
    uint32_t Port(int port);
    void AssertPort(int port, int line);
    void Clear(uint32_t address, uint32_t size);
    runtime::GuestAccess& memory_;
    KeyboardRpc& service_;
};
}
