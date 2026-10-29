// Characterization fixtures only: dependencies below are observable test doubles,
// never production replacements. Authority: current ignored generated originals.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#define DECL(a) extern void FUN_00##a##_0x##a(uint8_t*, R5900Context*, PS2Runtime*);
DECL(31a410) DECL(1ab7e0) DECL(1abac0) DECL(1ac110) DECL(1ac2e0) DECL(1ae38c) DECL(21b4c0) DECL(21b560) DECL(21acd0) DECL(21a680) DECL(21e350)
extern void thunk_FUN_001ae38c_0x1ae350(uint8_t*, R5900Context*, PS2Runtime*);
extern void entry_001ae358_0x1ae358(uint8_t*, R5900Context*, PS2Runtime*);
namespace {
constexpr uint32_t A=0x2000, OWNER=0x8000, PANEL=0xa000, BUFFER=0x200000;
constexpr uint32_t STACK=0x100000, RET=0x90000, SURFACE=0x9000, OUT=0x10000;
uint32_t load(uint8_t* ram, uint32_t p) { uint32_t v; std::memcpy(&v,ram+p,4); return v; }
void store(uint8_t* ram,uint32_t p,uint32_t v) { std::memcpy(ram+p,&v,4); }
void check(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
unsigned allocations=0, clears=0, links=0, rowEntries=0;
uint32_t requestedHeight=0, requestedWidth=0;
unsigned paletteUploads=0, mapUploads=0;
void done(R5900Context* c) { c->pc=GPR_U32(c,31); }
void noop(uint8_t*,R5900Context* c,PS2Runtime*) { SET_GPR_U32(c,2,0); done(c); }
void zero(uint8_t* r,R5900Context* c,PS2Runtime*) {
    ++links; std::memset(r+GPR_U32(c,4),GPR_U32(c,5),GPR_U32(c,6)); done(c);
}
void allocate(uint8_t* r,R5900Context* c,PS2Runtime*) {
    ++allocations; auto spec=GPR_U32(c,5);
    requestedHeight=load(r,spec+8); requestedWidth=load(r,spec+12);
    store(r,GPR_U32(c,6),SURFACE);
    // Controlled allocator output, deliberately padded pitch; not inferred hardware behavior.
    store(r,A+0x54,672); store(r,A+8,BUFFER);
    SET_GPR_U32(c,2,0); done(c);
}
void clear(uint8_t*,R5900Context* c,PS2Runtime*) { ++clears; done(c); }
void clutUpload(uint8_t* r,R5900Context* c,PS2Runtime*) {
    ++paletteUploads;
    check(GPR_U32(c,4)==0x1f00 && GPR_U32(c,6)==4 && GPR_U32(c,7)==0xc5df00,
          "CLUT upload destination/format/source");
    check(GPR_U32(c,10)==16 && GPR_U32(c,11)==16,"CLUT upload extent");
    for(unsigned i=0;i<256;++i) {
        unsigned j=(i & ~24u) | ((i & 8u)<<1) | ((i & 16u)>>1);
        check(std::memcmp(r+0xc5df00+4*j,r+0x18000+4*i,4)==0,"CLUT index 3/4 swap");
    }
    done(c);
}
void mapUpload(uint8_t*,R5900Context* c,PS2Runtime*) {
    ++mapUploads;
    check(GPR_U32(c,5)==0x28c0 && GPR_U32(c,6)==19 && GPR_U32(c,7)==10,
          "map transfer destination/PSMT8/width units");
    check(GPR_U32(c,8)==0x1f98c80 && GPR_U32(c,11)==264,"map pointer/aligned height");
    done(c);
}
void getArea(uint8_t*,R5900Context* c,PS2Runtime*) { SET_GPR_U32(c,2,A); done(c); }
void row(uint8_t* r,R5900Context* c,PS2Runtime* rt) {
    ++rowEntries;
    if (rowEntries == 1 && rt->eeScheduler().isExecutingGuest())
        rt->eeScheduler().scheduleHostCallback(1, [] {});
    FUN_001ac110_0x1ac110(r,c,rt);
}
void stop(uint8_t*,R5900Context* c,PS2Runtime* rt) {
    check(GPR_U32(c,29)==STACK && GPR_U32(c,16)==0,"resumed callee context restoration");
    c->pc=0; rt->eeScheduler().requestStop();
}
void setup(PS2Runtime& rt,R5900Context& c) {
    auto r=rt.memory().getRDRAM(); std::memset(r,0,0x400000); c={};
    SET_GPR_U32((&c),29,STACK); SET_GPR_U32((&c),31,RET); SET_GPR_U32((&c),4,A);
    allocations=clears=links=rowEntries=0;
    store(r,OWNER+4,0x1234); store(r,OWNER+0x1c,0x5678);
}
void run(PS2Runtime& rt,R5900Context& c,uint32_t pc) {
    c.pc=pc; auto r=rt.memory().getRDRAM();
    for(unsigned i=0;c.pc!=RET && i<2000;++i) {
        auto fn=rt.lookupFunction(c.pc); check(fn!=nullptr,"unregistered fixture continuation"); fn(r,&c,&rt);
    }
    check(c.pc==RET,"fixture did not return"); check(GPR_U32((&c),29)==STACK,"stack restoration");
}
void registerAll(PS2Runtime& rt) {
#define REG(a) rt.registerFunction(0x##a,FUN_00##a##_0x##a)
    REG(31a410); REG(1ab7e0); REG(1abac0); REG(1ac2e0); REG(1ae38c); REG(21b4c0); REG(21b560); REG(21acd0); REG(21a680); REG(21e350);
#undef REG
    rt.registerFunction(0x21de90,clutUpload); rt.registerFunction(0x21df50,mapUpload);
    rt.registerFunction(0x221b60,noop);
    rt.registerFunction(0x1ac110,row); rt.registerFunction(0x1ac1c0,row);
    rt.registerFunction(0x1ae350,thunk_FUN_001ae38c_0x1ae350);
    rt.registerFunction(0x1ae358,entry_001ae358_0x1ae358);
    for(auto p:{0x31a454u,0x31a488u,0x31a4b4u,0x31a4ccu,0x31a4e0u,0x31a500u}) rt.registerFunction(p,FUN_0031a410_0x31a410);
    for(auto p:{0x1ab848u,0x1ab88cu,0x1ab8c0u}) rt.registerFunction(p,FUN_001ab7e0_0x1ab7e0);
    // Allocator, external surface methods, clip/clear and visibility are isolated.
    rt.registerFunction(0x117cac,zero); rt.registerFunction(0x21b880,allocate);
    rt.registerFunction(0x21a780,clear); rt.registerFunction(0x1ae480,getArea);
    for(auto p:{0x21a570u,0x21a580u,0x1ac320u,0x1abd90u,0x1ae3a0u,0x260860u,0x260810u,0x25fa10u,0x25fd50u,0x1ac220u,0x326900u,0x326b40u}) rt.registerFunction(p,noop);
}
void init(PS2Runtime& rt) {
    R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM();
    store(r,PANEL+0x14,640); store(r,PANEL+0x18,262); store(r,PANEL+0x20,OUT); store(r,OUT,OWNER);
    SET_GPR_U32((&c),4,PANEL); run(rt,c,0x31a410);
    check(requestedWidth==640 && requestedHeight==272,"guard allocation height+10");
    check(load(r,A+0xb0)==A+0xb4 && load(r,OWNER+0x18)==A+0xb4,"embedded list insertion");
    check(load(r,A+0x20)==672,"pitch copied from descriptor +54");
    check(load(r,A+0xd4)==BUFFER+5*672,"top five guard pitches");
    // Guarded rows step by WIDTH, although initial offset uses PITCH (1AC198).
    check(load(r,A+0xd4+261*4)==BUFFER+5*672+261*640,"guarded row stride is width");
    check(load(r,A+0xd4+262*4)==0,"only visible rows entered");
    check(requestedHeight-262-5==5,"five bottom rows reserved by descriptor");
    auto snapshot=std::vector<uint8_t>(r+A,r+A+0xf58); auto n=allocations;
    SET_GPR_U32((&c),4,PANEL); run(rt,c,0x31a410);
    check(allocations==n && snapshot==std::vector<uint8_t>(r+A,r+A+0xf58),"panel init repeatability");
    // Calling area init again with nonzero B0 must skip insertion, not append itself.
    SET_GPR_U32((&c),4,A); SET_GPR_U32((&c),5,OWNER); SET_GPR_U32((&c),6,640); SET_GPR_U32((&c),7,262);
    SET_GPR_U32((&c),8,1); SET_GPR_U32((&c),9,0); SET_GPR_U32((&c),10,0x1f98000);
    run(rt,c,0x1ab7e0); check(load(r,A+0xbc)==0 && allocations==n,"repeat area avoids self-link/allocation");
    std::cout<<"panel/area: 640x262 requests 640x272; pitch 672; guarded stride 640; repeat stable\n";
}
void dimensions(PS2Runtime& rt) {
    for (int height : {262, -1, 0}) for (unsigned guard : {0u, 1u}) {
        R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM();
        SET_GPR_U32(&c,5,OWNER); SET_GPR_U32(&c,6,640);
        SET_GPR_U32(&c,7,uint32_t(height)); SET_GPR_U32(&c,8,guard);
        SET_GPR_U32(&c,9,0); SET_GPR_U32(&c,10,0x1f98000);
        run(rt,c,0x1ab7e0);
        if (height==0) {
            check(allocations==0 && load(r,A+0x1c)==0,"zero height rejects reconfiguration");
        } else {
            unsigned visible=height<0?1:262;
            check(allocations==1 && requestedHeight==visible+guard*10,"guard allocation/clamp");
            check(load(r,A+0x1c)==visible && load(r,A+0xf54)==guard,"visible dimensions/guard flag");
        }
    }
    R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM();
    SET_GPR_U32(&c,4,PANEL); run(rt,c,0x31a410);
    check(GPR_U32((&c),2)==1 && allocations==0 && load(r,PANEL+0x1c5c)==0,
          "zero-sized panel reports success without creating an area");
    std::cout<<"dimensions: guarded/unguarded allocation, negative height clamp, zero height/panel branches passed\n";
}
void rows(PS2Runtime& rt) {
    for(unsigned guard:{0u,1u}) for(unsigned orientation:{0u,1u}) {
        R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM();
        store(r,A+8,BUFFER); store(r,A+0x18,640); store(r,A+0x1c,262);
        store(r,A+0x20,672); store(r,A+0x24,orientation); store(r,A+0xf54,guard);
        run(rt,c,0x1ac110);
        auto first=guard?BUFFER+5*672:orientation==1?BUFFER:BUFFER+261*672;
        int stride=guard?640:orientation==1?672:-672;
        for(unsigned i=0;i<262;++i) check(load(r,A+0xd4+4*i)==uint32_t(first+i*stride),"row orientation/stride");
    }
    // D4..F27 holds 917 pointers. No generated bound check protects F28.
    for(unsigned height:{917u,918u}) {
        R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM();
        store(r,A+8,BUFFER); store(r,A+0x18,640); store(r,A+0x1c,height);
        store(r,A+0x20,640); store(r,A+0x24,1); store(r,A+0xf28,0xdeadbeef);
        run(rt,c,0x1ac110);
        check(load(r,A+0xf28)==(height==917?0xdeadbeef:BUFFER+917*640),"finite embedded table overflow characterization");
    }
    // Real scheduler deadline forces the generated backedge to return at 1AC1C0.
    R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM();
    store(r,A+8,BUFFER); store(r,A+0x18,640); store(r,A+0x1c,262);
    store(r,A+0x20,640); store(r,A+0x24,1); c.pc=0x1ac110;
    check(rt.syncCoreSubsystems(),"scheduler init"); rt.registerFunction(RET,stop);
    rt.eeScheduler().reset(r,c); rt.eeScheduler().run();
    check(rowEntries>1,"row loop did not yield/resume");
    check(load(r,A+0xd4+261*4)==BUFFER+261*640,"resumed row completion");
    std::cout<<"rows: four orientation/guard combinations; capacity 917, row 918 overwrites F28; scheduler resumes="<<rowEntries-1<<'\n';
}
void palette(PS2Runtime& rt) {
    R5900Context c; setup(rt,c); auto r=rt.memory().getRDRAM(); store(r,A,OWNER);
    for(unsigned i=0;i<1024;++i) r[OWNER+0x5c+i]=uint8_t(i*37+i/4);
    std::memset(r+OUT-1,0xa5,1026); SET_GPR_U32((&c),5,OUT); run(rt,c,0x1ae350);
    check(std::memcmp(r+OUT,r+OWNER+0x5c,1024)==0,"palette bytes/order/alpha");
    check(r[OUT-1]==0xa5 && r[OUT+1024]==0xa5 && GPR_U32((&c),10)==256,"palette extent");
    r[OWNER+0x5c+3]^=0xff; SET_GPR_U32((&c),4,A); SET_GPR_U32((&c),5,OUT); run(rt,c,0x1ae350);
    check(r[OUT+3]==r[OWNER+0x5f],"palette rereads current owner bytes");
    // Static caller chain (not a claim about live GS state):
    // 1AACC0:1AAE4C initializes owner+28 from owner+5C via 21B4C0.
    // 1ABAC0:1ABC58 binds owner+28 through 21A580 to surface+14.
    // 21A960:21AB74 calls 21A680(source), which selects the texture/CLUT.
    // 1AB370 forwards owner+28 to 21B4C0; characterize its real upload path.
    // Getter preserves alpha, whereas this upload setter rewrites it.
    store(r,0x17000+4,0x18000);
    SET_GPR_U32(&c,4,0x17000); SET_GPR_U32(&c,5,0); SET_GPR_U32(&c,6,0);
    SET_GPR_U32(&c,7,256); SET_GPR_U32(&c,8,OUT); SET_GPR_U32(&c,9,255);
    run(rt,c,0x21b4c0);
    check(paletteUploads==1,"palette upload call count");
    for(unsigned j=0;j<256;++j) {
        check(std::memcmp(r+0x18000+4*j,r+OUT+4*j,3)==0,"CLUT RGB preservation");
        check(r[0x18000+4*j+3]==(j==255?0:0x80),"CLUT transparent index/alpha rewrite");
    }
    // 21A780 calls 21ACD0(source,destination) before draw setup.
    store(r,SURFACE+0x98,0x800); store(r,SURFACE+0x54,0x1f98c80);
    store(r,SURFACE+0x1c,19); store(r,SURFACE+0x38,640); store(r,SURFACE+0x3c,262);
    SET_GPR_U32(&c,4,SURFACE); SET_GPR_U32(&c,5,0x19000);
    run(rt,c,0x21acd0); check(mapUploads==1,"map transfer call count");
    for (uint32_t slot : {0u, 0x123u, 0x1ffu}) {
        store(r,SURFACE+0x14,0x17000); store(r,0x17000,slot<<16);
        SET_GPR_U32(&c,4,SURFACE); run(rt,c,0x21a680);
        uint64_t tex0; std::memcpy(&tex0,r+SURFACE+0x20,8);
        check((tex0 & 0x3fff)==0x28c0 && ((tex0>>20)&63)==19,"map TEX0 staging base/PSMT8");
        check(((tex0>>37)&0x3fff)==((0x7c000+(slot&0x1f0)*16)>>6),"TEX0 CBP follows surface palette slot");
        check(((tex0>>56)&31)==(slot&15) && ((tex0>>61)&7)==1,"TEX0 CSA/CLD");
    }
    std::cout<<"uploads: alpha 80/index FF transparent; CLUT bits 3/4 swapped; map source 1F98C80 height 264; three TEX0 palette slots passed\n";
    std::cout<<"palette: 1AE350 -> 1AE38C/1AE358 copies current (*a0)+5C, 256 RGBA-sized entries unchanged\n";
}
}
// Generated 21B560 uses a direct C++ tail call at 21B634.
void FUN_0021de90_0x21de90(uint8_t* r, R5900Context* c, PS2Runtime* rt) { clutUpload(r,c,rt); }
int main() {
    try { PS2Runtime rt; check(rt.memory().initialize(),"RAM init"); registerAll(rt); init(rt); dimensions(rt); palette(rt); rows(rt); }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
