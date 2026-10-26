#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include "game_overrides.h"
#include "guest_rdram_access.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

extern void FUN_00129560_0x129560(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_00129428_0x129428(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_001294c0_0x1294c0(uint8_t*, R5900Context*, PS2Runtime*);
namespace {
constexpr uint32_t STACK=0x100000, RET=0x8000, CALL=0x9000, RESUME=0x9004;
constexpr std::array<uint32_t,3> entries{0x129560,0x129428,0x1294c0};
constexpr std::array<PS2Runtime::RecompiledFunction,3> originals{
    FUN_00129560_0x129560,FUN_00129428_0x129428,FUN_001294c0_0x1294c0};
void Check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
using aoe2::game_hooks::RdramGuestAccess;
std::vector<uint64_t> trace;
uint64_t reply;
bool suspendCall;
bool realSemaphore = false;
bool nestedCallback = false;
void Poll(uint8_t*, R5900Context* c, PS2Runtime* r) {
    trace.push_back(0x5011);
    SET_GPR_S32(c, 2, r->eeScheduler().pollSemaphore(getRegU32(c, 4)));
    c->pc = getRegU32(c, 31);
}
void Wait(uint8_t*, R5900Context* c, PS2Runtime* r) {
    trace.push_back(0x7a17);
    c->pc = getRegU32(c, 31);
    r->eeScheduler().waitSemaphore(getRegU32(c, 4));
}
void Callback2(uint8_t* ram, R5900Context* c, PS2Runtime* r);
void Complete(uint8_t*, R5900Context* c, PS2Runtime*) {
    trace.push_back(0xc0);
    SET_GPR_U64(c,2,reply); c->pc=getRegU32(c,31);
}
void Callback(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    trace.push_back(GPR_U64(c,4)); trace.push_back(GPR_U64(c,5));
    trace.push_back(GPR_U64(c,31)); trace.push_back(GPR_U64(c,29));
    if (suspendCall) {
        c->pc=RESUME;
        r->eeScheduler().scheduleHostCallback(1, [] {});
        r->eeScheduler().accountCycles(2);
        Check(r->eeScheduler().checkpointDue(), "callback scheduler yield missing");
        return;
    }
    Complete(ram,c,r);
}
void Callback2(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    trace.push_back(0xca112);
    Callback(ram, c, r);
}
void NestedReturn(uint8_t* ram, R5900Context* c, PS2Runtime*) {
    RdramGuestAccess m; m.Bind(ram);
    auto sp=getRegU32(c,29);
    SET_GPR_U64(c,31,m.Load32(sp) | (uint64_t(m.Load32(sp+4))<<32));
    SET_GPR_S32(c,29,int32_t(sp+16));
    c->pc=getRegU32(c,31);
}
void Nested(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    RdramGuestAccess m; m.Bind(ram);
    auto sp=getRegU32(c,29)-16;
    m.Store32(sp,uint32_t(GPR_U64(c,31)));
    m.Store32(sp+4,uint32_t(GPR_U64(c,31)>>32));
    SET_GPR_S32(c,29,int32_t(sp));
    SET_GPR_U32(c,4,CALL); SET_GPR_U32(c,31,0x9024);
    if (!r->dispatchGuestBranch(ram,c,0x129560,0x9020,0x9024,
            PS2Runtime::GuestBranchKind::DirectCall,"nested HD test")) return;
    NestedReturn(ram,c,r);
}
void Finish(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    Check(getRegU32(c,29)==STACK,"final guest stack not restored");
    trace.push_back(0xfeed);
    r->eeScheduler().publishSnapshot();
    trace.push_back(r->eeScheduler().snapshot().eeCycle);
    for (const auto& sema : r->eeScheduler().snapshot().semaphores) {
        trace.push_back(sema.id); trace.push_back(sema.count);
        trace.push_back(sema.maxCount); trace.push_back(sema.waiters);
    }
    trace.push_back(c->pc); trace.push_back(c->branch_pc);
    for (unsigned i=0;i<32;++i) {
        uint64_t words[2]; std::memcpy(words,&c->r[i],16);
        trace.insert(trace.end(),words,words+2);
    }
    RdramGuestAccess m; m.Bind(ram);
    for (uint32_t p=STACK-80;p<STACK;p+=4) trace.push_back(m.Load32(p));
    c->pc=0; r->eeScheduler().requestStop();
}
void Register(PS2Runtime& r, bool native) {
    for (unsigned i=0;i<3;++i) r.registerFunction(entries[i],originals[i]);
    r.registerFunction(0x12957c,originals[0]);
    r.registerFunction(0x129480,originals[1]);
    r.registerFunction(0x1294d0,originals[2]);
    r.registerFunction(CALL,realSemaphore ? Poll : Callback);
    r.registerFunction(CALL+0x10,realSemaphore ? Wait : Callback2);
    r.registerFunction(RESUME,Complete);
    r.registerFunction(RET,Finish);
    r.registerFunction(0x9020,Nested); r.registerFunction(0x9024,NestedReturn);
    auto paths=PS2Runtime::getIoPaths();
    paths.elfDirectory=std::filesystem::current_path()/"hd-test-config";
    std::filesystem::create_directories(paths.elfDirectory);
    PS2Runtime::setIoPaths(paths);
    std::ofstream(paths.elfDirectory/"native_modules.ini") << "hd=" << (native?"on":"off") << '\n';
    ps2_game_overrides::applyMatching(r,"AOK.ELF",0,0,false);
}
std::vector<uint64_t> Run(PS2Runtime& r, unsigned slot, bool native,
                          uint32_t arg, int32_t sema, uint64_t result, bool yield, unsigned deadline) {
    Register(r,native); trace.clear(); reply=result; suspendCall=yield;
    auto ram=r.memory().getRDRAM(); std::memset(ram,0,0x2000000);
    R5900Context c{};
    for(unsigned i=1;i<32;++i) {
        uint64_t words[2]{0x1234567800000000ull+i,0xabcdef9800000000ull+i};
        std::memcpy(&c.r[i],words,16);
    }
    SET_GPR_U32(&c,29,STACK); SET_GPR_U32(&c,31,RET); SET_GPR_U32(&c,28,0x500000);
    SET_GPR_U64(&c,4,slot==0 ? (arg?(nestedCallback?0x9020:CALL):0):arg);
    SET_GPR_U64(&c,5,0xfedcba9876543210ull);
    RdramGuestAccess m; m.Bind(ram);
    m.Store32(0x500000-0x7bd8,uint32_t(sema));
    m.Store32(0x4e2c28,CALL); m.Store32(0x4e2c2c,CALL+0x10);
    c.pc=entries[slot]; r.eeScheduler().reset(ram,c);
    r.eeScheduler().scheduleHostCallback(deadline,[&] {
        trace.push_back(0xdead); trace.push_back(m.Load32(STACK-16));
        trace.push_back(m.Load32(STACK-64));
    });
    if (realSemaphore) {
        r.eeScheduler().scheduleHostCallback(0, [&] {
            const int id = r.eeScheduler().createSemaphore(sema, 1, 0, 0);
            Check(id > 0, "semaphore creation failed");
            m.Store32(0x500000-0x7bd8, uint32_t(id));
            r.eeScheduler().scheduleHostCallback(100, [&, id] {
                const auto* object = r.eeScheduler().semaphore(id);
                trace.push_back(0x5eaa); trace.push_back(object->count); trace.push_back(object->waiters.size());
                Check(object->waiters.size()==1,"empty semaphore wait did not block guest");
                r.eeScheduler().signalSemaphore(id, false);
            });
        });
    }
    r.eeScheduler().run();
    Check(std::find(trace.begin(),trace.end(),0xfeed)!=trace.end(),"guest did not return");
    return trace;
}
}
int main() {
    try {
        PS2Runtime r; Check(r.memory().initialize(),"memory initialization");
        Check(r.syncCoreSubsystems(),"scheduler initialization");
        unsigned failures=0, comparisons=0;
        for(unsigned slot=0;slot<3;++slot) {
            bool match=true;
            for(uint32_t arg : {0u,1u,0xffffffffu})
            for(int32_t sema : {-1,7})
            for(uint64_t result : {7ull,0xffffffffffffffffull})
            for(bool yield : {false,true})
            for(unsigned deadline : {1u,8u}) {
                auto expected=Run(r,slot,false,arg,sema,result,yield,deadline);
                auto actual=Run(r,slot,true,arg,sema,result,yield,deadline);
                match &= expected==actual; ++comparisons;
            }
            std::cout << std::hex << entries[slot] << std::dec << (match?" PASS":" FAIL") << '\n';
            failures+=!match;
        }
        nestedCallback = true;
        for (bool yield : {false,true}) for (unsigned deadline : {1u,8u}) {
            auto expected=Run(r,0,false,1,7,42,yield,deadline);
            auto actual=Run(r,0,true,1,7,42,yield,deadline);
            Check(expected==actual,"nested callback scheduler differential mismatch");
            ++comparisons;
        }
        nestedCallback = false;
        realSemaphore = true;
        for (unsigned slot : {1u,2u}) for (unsigned arg : {0u,1u})
        for (int count : {0,1}) {
            auto expected=Run(r,slot,false,arg,count,0,false,8);
            auto actual=Run(r,slot,true,arg,count,0,false,8);
            Check(expected==actual,"actual semaphore wait/poll differential mismatch");
            ++comparisons;
        }
        std::cout << comparisons << " generated/native scheduler comparisons, " << failures << " failing routines\n";
        return failures?1:0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
