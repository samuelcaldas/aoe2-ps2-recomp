#include <aoe2/game_hooks/map_view_session.hpp>
#include "map_view_module_private.hpp"
#include <aoe2/renderer/map_view_compositor.hpp>
#include "ps2_runtime.h"

namespace aoe2::game_hooks {

MapViewSession::MapViewSession(PS2Runtime& runtime) {
    map_view::Install(runtime);
    runtime_ = &runtime;
    runtime.setFrameCompositor([&runtime](PS2PresentationFrameView legacy) -> std::optional<PS2OwnedPresentationFrame> {
        auto snap = map_view::CaptureNative(runtime);
        if (!snap || snap->indices.empty() || snap->layout.output.width == 0) {
            map_view::PublishComposedLayout(runtime, nullptr);
            return std::nullopt;
        }
        renderer::MapFrameSnapshot frame_snap;
        frame_snap.width = snap->width;
        frame_snap.height = snap->height;
        frame_snap.pitch = snap->pitch;
        frame_snap.indices = snap->indices;
        frame_snap.layout = snap->layout;
        for (size_t i = 0; i < 256; ++i) {
            frame_snap.palette[i] = {
                snap->palette[i * 4 + 0],
                snap->palette[i * 4 + 1],
                snap->palette[i * 4 + 2],
                snap->palette[i * 4 + 3]
            };
        }
        renderer::RgbaFrameView legacy_view{
            std::span<const uint8_t>(legacy.pixels, PS2PresentationFrameBytes(legacy)),
            legacy.width, legacy.height, legacy.strideBytes
        };
        try {
            auto composed = renderer::MapViewCompositor::Compose(legacy_view, frame_snap);
            map_view::PublishComposedLayout(runtime, &*snap);
            return PS2OwnedPresentationFrame{
                composed.pixels,
                composed.width,
                composed.height,
                composed.strideBytes
            };
        } catch (...) {
            map_view::PublishComposedLayout(runtime, nullptr);
            return std::nullopt;
        }
    });
}

MapViewSession::~MapViewSession() { Close(); }

void MapViewSession::Close() {
    if (!runtime_) return;
    runtime_->setFrameCompositor(nullptr);
    map_view::Teardown(*runtime_);
    runtime_ = nullptr;
}

}  // namespace aoe2::game_hooks

