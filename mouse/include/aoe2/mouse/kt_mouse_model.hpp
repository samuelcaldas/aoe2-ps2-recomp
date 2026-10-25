// Origin: samuelcaldas/age-of-empires-2-decomp, kt_mouse.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Control flow checked against the owned binary.
#pragma once
#include <aoe2/runtime/guest_access.hpp>

namespace aoe2::mouse {

class MouseRpc {
public:
    virtual ~MouseRpc() = default;
    virtual int Info(uint32_t reply) = 0;
    virtual int Read(int port, uint32_t reply) = 0;
    virtual int DeviceInfo(int port, uint32_t reply) = 0;
};

class KtMouseModel {
public:
    KtMouseModel(runtime::GuestAccess& memory, MouseRpc& service);
    void WorkPadding();                            // func_00134F18: binary alignment return
    void DevicePadding();                          // func_00135D98: binary alignment return
    void Init(int count);                          // func_00134E50
    int NextIndex(int port, int index);             // func_00134FE0
    void AdvanceWrite(int port);                   // func_00135008
    uint32_t WriteEntry(int port);                  // func_00135058
    void AdvanceRead(int port);                    // func_00135090
    uint32_t ReadEntry(int port);                   // func_001350E0
    int PortState(uint32_t address);                // func_001351B0
    int State(int port);                            // func_001351B8
    int PortType(uint32_t address);                 // func_00135238
    int Status(int port);                           // func_00135258
    void Enable();                                 // func_00135478
    void Disable();                                // func_001354A0
    bool IsAllocated(uint32_t address);             // func_001356D8
    int Enabled();                                 // func_00135980
    int ReadPacket(int port, uint32_t output);       // func_00135118
    int ReadPayload(int port, uint32_t output);      // func_001352D8
    int ResetPorts(int force);                      // func_001353C0
    uint32_t FindFreePort();                        // func_00135530
    void ResetQueue(uint32_t address);              // func_00135598
    int Allocate(int type);                         // func_001355D0
    void Release(int port, int type);               // func_00135638
    int SetState(int port, int state);              // func_001356E8
    int SetPayload(int port, uint32_t source, int size); // func_00135798
    int PostEvent(int port, int buttons, int dx, int dy, int wheel); // func_00135870
    void InitRecord(uint32_t address, int value);   // func_001359B8
    void InitRecords(uint32_t address, int count, int stride); // func_001359D0
    void ReconcileRecords(uint32_t address, int count, int stride, int type); // func_00135A38
    void ApplyInfo(uint32_t address, int count, int stride, int type, uint32_t reply); // func_00135AB8
    void InitProcessing(int count);                 // func_00135B88
    void ProcessEvents();                          // func_00135BF0

private:
    void DrainEvents(int device, int port);
    uint32_t Port(int port);
    void AssertPort(int port, int line);
    void Clear(uint32_t address, uint32_t size);
    runtime::GuestAccess& memory_;
    MouseRpc& service_;
};
}
