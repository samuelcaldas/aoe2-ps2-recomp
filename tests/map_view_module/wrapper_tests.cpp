#include "map_view_module_private.hpp"
#include <aoe2/game_hooks/map_view_session.hpp>
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
R5900Context observer_expected;
unsigned observer_calls = 0;
void observer_original(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    check(std::memcmp(ctx, &observer_expected, sizeof(*ctx)) == 0);
    ++observer_calls;
}
void observer_tests() {
    PS2Runtime rt;
    for (auto entry : {0x31a410,0x1ab7e0,0x1abac0,0x21acd0,0x1ae350})
        rt.functions[entry] = observer_original;
    unsetenv("AOE2_MAP_VIEW");
    Install(rt);
    check(rt.replacements == 0 && rt.claims == 0 && !Capture(rt));
    setenv("AOE2_MAP_VIEW", "legacy", 1);
    // An external arena owner must neither block observation nor be released.
    int external_owner;
    rt.owner = &external_owner;
    std::vector<uint8_t> ram(64*1024*1024, 0x73);
    const uint32_t panel=0x1000, area=0x4000, viewport=0x8000,
                   palette=0x9000, target=0xa000, source=0x100000;
    word(ram,panel,0); word(ram,panel+0x14,640); word(ram,panel+0x18,262);
    word(ram,panel+0x1c5c,0);
    word(ram,area,palette); word(ram,area+0x40,viewport);
    word(ram,viewport+0x98,0x800); word(ram,target+0x98,0);
    word(ram,viewport+0x38,640); word(ram,viewport+0x3c,262);
    word(ram,viewport+0x54,source);
    const auto invoke = [&](uint32_t entry, uint32_t a0, uint32_t a1) {
        R5900Context ctx;
        for (int i=0;i<32;++i) ctx.r[i]=_mm_set_epi64x(0x123456789abcdef, i+123);
        ctx.pc=entry; reg(ctx,4,a0); reg(ctx,5,a1); reg(ctx,16,panel);
        reg(ctx,31,entry==0x1ab7e0?0x31a488:0x31a4b4);
        observer_expected=ctx;
        const auto before=ram;
        const auto calls_before=observer_calls;
        rt.lookupFunction(entry)(ram.data(),&ctx,&rt);
        check(observer_calls==calls_before+1);
        check(std::memcmp(&ctx,&observer_expected,sizeof(ctx))==0 && ram==before);
    };
    Install(rt);
    check(rt.replacements==5 && rt.claims==0 && !Capture(rt));
    invoke(0x31a410,panel,0);
    // Area creation happens inside the original, after Prepare has observed panel init.
    word(ram,panel+0x1c5c,area);
    invoke(0x1ab7e0,area,0); invoke(0x1abac0,area,0);
    invoke(0x1ae350,area,target);
    invoke(0x21acd0,viewport,target);
    auto snap=Capture(rt);
    check(snap && snap->generation==0 && snap->panel==panel && snap->area==area);
    check(snap->source==source && snap->palette_source==palette+0x5c && snap->palette_calls==1);
    check(snap->indices==std::vector<uint8_t>(640*264,0x73));
    check(snap->palette[0]==0x73 && snap->indices_hash && snap->palette_hash);
    // Capture owns actual upload bytes and survives unrelated arena generations.
    ++rt.generation; ram[source]=0x19; ram[palette+0x5c]=0x29;
    invoke(0x31a410,panel,0); // Existing initialized viewport is valid for observation.
    invoke(0x21acd0,viewport,target);
    auto next=Capture(rt);
    check(next && next->submission==2 && next->indices[0]==0x19 && next->palette[0]==0x29);
    check(next->indices_hash!=snap->indices_hash && next->palette_hash!=snap->palette_hash);
    check(snap->indices[0]==0x73 && snap->palette[0]==0x73);
    Teardown(rt); Teardown(rt);
    check(rt.owner==&external_owner && rt.claims==0 && rt.releases==0 && !Capture(rt));
    for (auto entry : {0x31a410,0x1ab7e0,0x1abac0,0x21acd0,0x1ae350})
        check(rt.lookupFunction(entry)==observer_original);
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
    aoe2::game_hooks::MapViewSession session(rt);
    check(rt.claims == 1 && rt.owner);
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
    session.Close(); check(rt.lookupFunction(0x1abac0)==original);
    Install(rt); c.pc=0x31a410; reg(c,4,panel);
    rejected=false;
    try { rt.lookupFunction(0x31a410)(ram.data(),&c,&rt); }
    catch (const std::runtime_error&) { rejected=true; }
    check(rejected); // An old viewport can contain stale arena pointers.
    word(ram,area+0x40,0); c.pc=0x31a410;
    rt.lookupFunction(0x31a410)(ram.data(),&c,&rt);
    check(!Capture(rt));
    Teardown(rt); Teardown(rt);
    const auto released = [&] {
        check(!rt.owner && !Capture(rt));
        for (auto address : {0x31a410,0x1ab7e0,0x1abac0,0x21acd0,0x1ae350})
            check(rt.lookupFunction(address) == original);
    };
    released();
    auto claims = rt.claims, releases = rt.releases;
    {
        aoe2::game_hooks::MapViewSession scoped(rt);
        check(rt.claims == claims+1 && rt.owner);
        scoped.Close(); released();
        check(rt.releases == releases+1);
        auto replacements = rt.replacements;
        scoped.Close();
        check(rt.replacements == replacements && rt.releases == releases+1);
    }
    check(rt.releases == releases+1); released();
    try {
        aoe2::game_hooks::MapViewSession scoped(rt);
        check(rt.owner);
        throw std::runtime_error("ordinary host exception");
    } catch (const std::runtime_error&) {}
    check(rt.releases == releases+2); released();
    // A constructor failing after two wrappers were installed rolls everything back.
    rt.fail_replacement = rt.replacements+3;
    bool install_failed = false;
    try { aoe2::game_hooks::MapViewSession failing(rt); }
    catch (const std::runtime_error&) { install_failed = true; }
    check(install_failed && rt.releases == releases+3); released();
    rt.fail_replacement = 0;
    observer_tests();
    std::clog.rdbuf(previous_log);
    std::cout << "wrapper yield, two submissions, stale lease and teardown tests passed\n";
}
