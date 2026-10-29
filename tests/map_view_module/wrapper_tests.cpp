#include "map_view_module_private.hpp"
#include "ps2_runtime.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <sstream>
using namespace aoe2::game_hooks::map_view;
void check(bool ok) { if (!ok) throw std::runtime_error("wrapper assertion failed"); }
void reg(R5900Context& c, int r, uint32_t v) { c.r[r] = _mm_set_epi64x(0, v); }
void word(std::vector<uint8_t>& ram, uint32_t a, uint32_t v) { std::memcpy(ram.data()+a, &v, 4); }
unsigned calls = 0;
uint32_t expected = 0;
int buffer_reg = 0;
void original(uint8_t*, R5900Context* c, PS2Runtime*) {
    ++calls;
    if (buffer_reg) check(getRegU32(c, buffer_reg) == expected);
    // Simulate a partial guest call: returning C++ does not mean guest completion.
    c->pc = 0x1abb80;
    reg(*c, 2, 0xabcdef);
}
int main() {
    setenv("AOE2_MAP_VIEW", "native", 1);
    unsetenv("AOE2_MAP_VIEW_OVERRIDE");
    PS2Runtime rt;
    for (auto address : {0x31a410,0x1ab7e0,0x1abac0,0x21acd0,0x1ae350}) rt.functions[address] = original;
    std::vector<uint8_t> ram(64*1024*1024);
    const uint32_t panel=0x1000, area=0x4000, viewport=0x8000, manager=0x9000, target=0xa000;
    word(ram,panel+0x14,640); word(ram,panel+0x18,262); word(ram,panel+0x1c5c,area);
    word(ram,area,manager);
    const uint32_t vtable=0xc000;
    word(ram,panel,vtable);
    for (auto offset : {0x50,0xb0,0xb4,0xf0}) word(ram,vtable+offset,0x100000+offset);
    std::ostringstream trace;
    auto* previous_log=std::clog.rdbuf(trace.rdbuf());
    R5900Context c;
    Install(rt);
    c.pc=0x31a410; reg(c,4,panel);
    rt.lookupFunction(0x31a410)(ram.data(),&c,&rt);
    check(c.pc==0x1abb80 && getRegU32(&c,2)==0xabcdef);
    const auto diagnostic=trace.str();
    check(diagnostic.find("[map-panel-init] panel=4096 vtable=49152")!=std::string::npos);
    check(diagnostic.find(" slot_50=1048656 slot_b0=1048752 slot_b4=1048756 slot_f0=1048816")!=std::string::npos);
    // A resumed panel wrapper cannot interpret its current a0 as a fresh panel.
    reg(c,4,0xffffffff);
    rt.lookupFunction(0x31a410)(ram.data(),&c,&rt);
    check(trace.str()==diagnostic);
    // Reentry logs the actual current instance without resetting live arena bytes.
    ram[0x3000010]=0x73;
    c.pc=0x31a410; reg(c,4,panel);
    rt.lookupFunction(0x31a410)(ram.data(),&c,&rt);
    check(ram[0x3000010]==0x73);
    check(trace.str().size()==2*diagnostic.size());
    // Invalid vtables remain diagnostic-only, with no out-of-range guest read.
    word(ram,panel,0xfffffffc); c.pc=0x31a410; reg(c,4,panel);
    rt.lookupFunction(0x31a410)(ram.data(),&c,&rt);
    check(trace.str().find("vtable=4294967292 slots=unreadable")!=std::string::npos);
    word(ram,panel,vtable);
    for (auto entry : {0x1ab7e0,0x1abac0}) {
        const bool first=entry==0x1ab7e0;
        c.pc=entry; reg(c,4,area); reg(c,16,panel); reg(c,31, first?0x31a488:0x31a4b4);
        reg(c,first?6:5,640); reg(c,first?7:6,262);
        buffer_reg=first?10:8; expected=0x3000010; reg(c,buffer_reg,0x1f98000);
        rt.lookupFunction(entry)(ram.data(),&c,&rt);
        check(c.pc==0x1abb80);
        // A resumed shared routine must retain its current registers.
        expected=0xdead; reg(c,buffer_reg,expected);
        rt.lookupFunction(entry)(ram.data(),&c,&rt);
    }
    buffer_reg=0;
    word(ram,area+0x40,viewport);
    word(ram,viewport+0x98,0x800); word(ram,viewport+0x38,640); word(ram,viewport+0x3c,262);
    word(ram,viewport+0x54,0x3000010+640*5);
    for (unsigned i=1;i<=2;++i) {
        c.pc=0x21acd0; reg(c,4,viewport); reg(c,5,target);
        ram[0x3000010+640*5]=i;
        rt.lookupFunction(0x21acd0)(ram.data(),&c,&rt);
        auto snap=Capture(rt); check(snap && snap->submission==i && snap->indices[0]==i);
    }
    auto owned=Capture(rt);
    // Palette observation is scoped to the map area and reads its actual slot.
    c.pc=0x1ae350; reg(c,4,area); reg(c,5,0xb000);
    rt.lookupFunction(0x1ae350)(ram.data(),&c,&rt);
    c.pc=0x21acd0; reg(c,4,viewport); reg(c,5,target);
    rt.lookupFunction(0x21acd0)(ram.data(),&c,&rt);
    check(Capture(rt)->palette_calls==1 && Capture(rt)->palette_source==manager+0x5c);
    // Unrelated callers of the shared routine retain their buffer argument.
    c.pc=0x1abac0; reg(c,31,0x123456); reg(c,8,0x1234);
    buffer_reg=8; expected=0x1234;
    rt.lookupFunction(0x1abac0)(ram.data(),&c,&rt);
    buffer_reg=0;
    // Rounded upload reaches into valid bottom guards; external canaries do not.
    ram[0x3000000]=0;
    bool damaged=false;
    try { c.pc=0x21acd0; reg(c,4,viewport); reg(c,5,target);
          rt.lookupFunction(0x21acd0)(ram.data(),&c,&rt); }
    catch (const std::runtime_error&) { damaged=true; }
    check(damaged); ram[0x3000000]=0xa5;
    ++rt.generation;
    check(!Capture(rt));
    bool rejected=false;
    try { c.pc=0x1abac0; reg(c,4,area); reg(c,16,panel); reg(c,31,0x31a4b4);
          rt.lookupFunction(0x1abac0)(ram.data(),&c,&rt); }
    catch (const std::runtime_error&) { rejected=true; }
    check(rejected && owned->indices[0]==2);
    // Runtime reset invalidates ownership as well as the token.
    rt.owner=nullptr;
    Teardown(rt); check(rt.lookupFunction(0x1abac0)==original);
    Install(rt); c.pc=0x31a410; reg(c,4,panel);
    rejected=false;
    try { rt.lookupFunction(0x31a410)(ram.data(),&c,&rt); }
    catch (const std::runtime_error&) { rejected=true; }
    check(rejected); // An old viewport can contain stale arena pointers.
    word(ram,area+0x40,0); c.pc=0x31a410;
    rt.lookupFunction(0x31a410)(ram.data(),&c,&rt);
    check(!Capture(rt));
    Teardown(rt); Teardown(rt);
    std::clog.rdbuf(previous_log);
    std::cout << "wrapper yield, two submissions, stale lease and teardown tests passed\n";
}
