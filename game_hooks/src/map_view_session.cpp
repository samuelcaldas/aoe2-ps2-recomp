#include <aoe2/game_hooks/map_view_session.hpp>
#include "map_view_module_private.hpp"

namespace aoe2::game_hooks {
MapViewSession::MapViewSession(PS2Runtime& runtime) {
    map_view::Install(runtime);
    runtime_ = &runtime;
}

MapViewSession::~MapViewSession() { Close(); }

void MapViewSession::Close() {
    if (!runtime_) return;
    map_view::Teardown(*runtime_);
    runtime_ = nullptr;
}
}
