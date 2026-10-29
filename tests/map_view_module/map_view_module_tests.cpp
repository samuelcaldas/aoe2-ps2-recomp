#include "map_view_module_private.hpp"
#include <stdexcept>
#include <iostream>
using namespace aoe2::game_hooks::map_view;
void check(bool ok) { if (!ok) throw std::runtime_error("contract failed"); }
int main() {
    Relocation state;
    state.begin(0x1000, 640, 262, 0x3000000, 8*1024*1024, 1);
    check(state.redirect(0x1ab7e0, 0x31a488, 0x1000, 0x2000, 640, 262, true) == 0x3000010);
    check(state.redirect(0x1abac0, 0x31a4b4, 0x1000, 0x2000, 640, 262, true) == 0x3000010);
    check(!state.redirect(0x1abac0, 0x1234, 0x1000, 0x2000, 640, 262, true));
    check(!state.redirect(0x1abac0, 0x31a4b4, 0x1000, 0x3000, 640, 262, true));
    check(!state.redirect(0x1abac0, 0x31a4b4, 0x1000, 0x2000, 641, 262, true));
    check(!state.redirect(0x1abac0, 0x31a4b4, 0x1000, 0x2000, 640, 262, false));
    check(state.layout.visible_offset + 640*264 <= state.layout.buffer_stride-16);
    state.clear();
    check(!state.redirect(0x1abac0, 0x31a4b4, 0x1000, 0x2000, 640, 262, true));
    state.begin(0x1000, 640, 262, 0x3100000, 8*1024*1024, 2);
    // Reconfiguration may occur without the first area initializer on this invocation.
    check(state.redirect(0x1abac0, 0x31a4b4, 0x1000, 0x4000, 640, 262, true) == 0x3100010);
    bool rejected = false;
    try { state.begin(0x1000, 1280, 720, 0x3000000, 8*1024*1024, 3); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    check(!state.generation);
    std::cout << "legacy relocation contracts passed\n";
}
