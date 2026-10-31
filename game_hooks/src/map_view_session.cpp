#include <aoe2/game_hooks/map_view_session.hpp>
#include "map_view_module_private.hpp"
#include <aoe2/renderer/map_view_compositor.hpp>
#include "ps2_runtime.h"
#include <exception>
#include <iostream>
#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace aoe2::game_hooks {

MapViewSession::MapViewSession(PS2Runtime& runtime) {
    map_view::Install(runtime);
    runtime_ = &runtime;
    runtime.setFrameCompositor([&runtime](PS2PresentationFrameView legacy) -> std::optional<PS2OwnedPresentationFrame> {
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] MapViewSession compositor callback ENTER\n", 53);
#endif
        auto snap = map_view::CaptureNative(runtime);
        if (!snap || snap->indices.empty() || snap->layout.output.width == 0) {
            map_view::PublishComposedLayout(runtime, nullptr);
            if (map_view::IsNativeMode(runtime) && map_view::IsMapActive(runtime)) {
                throw std::runtime_error("Native map view snapshot failed; runtime fallback to GS is prohibited in native mode");
            }
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
        auto composed = renderer::MapViewCompositor::TryCompose(legacy_view, frame_snap);
        if (!composed) {
            map_view::PublishComposedLayout(runtime, nullptr);
            if (map_view::IsNativeMode(runtime)) {
                throw std::runtime_error("Native map view TryCompose failed; runtime fallback to GS is prohibited in native mode");
            }
            return std::nullopt;
        }
        if (!map_view::PublishComposedLayout(runtime, &*snap)) {
            if (map_view::IsNativeMode(runtime)) {
                throw std::runtime_error("Native map view PublishComposedLayout failed; runtime fallback to GS is prohibited in native mode");
            }
            return std::nullopt;
        }
        return PS2OwnedPresentationFrame{
            composed->pixels,
            composed->width,
            composed->height,
            composed->strideBytes
        };
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

