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
#if defined(__SWITCH__)
    std::clog << "[native-presentation] map-only: menus and HUD are not implemented\n";
    runtime.setNativeFrameProvider([&runtime]() -> std::optional<PS2OwnedPresentationFrame> {
        auto map_snap = map_view::CaptureNative(runtime);
        auto ui_snap = map_view::CaptureUi(runtime);

        const bool has_map = map_snap && !map_snap->indices.empty();
        const bool has_ui = ui_snap && !ui_snap->rgba.empty();

        if (!has_map && !has_ui) {
            map_view::PublishComposedLayout(runtime, nullptr);
            return std::nullopt;
        }

        auto layout = has_map ? map_snap->layout
                              : map_view::ActiveLayout(runtime).value_or(map_view::ResolveOutputLayout());
        uint32_t out_w = static_cast<uint32_t>(layout.output.width);
        uint32_t out_h = static_cast<uint32_t>(layout.output.height);

        std::optional<renderer::RgbaFrameView> ui_view;
        if (has_ui) {
            ui_view.emplace(ui_snap->rgba, ui_snap->width, ui_snap->height, ui_snap->pitch);
        }

        if (has_map) {
            renderer::MapFrameSnapshot frame;
            frame.width = map_snap->width;
            frame.height = map_snap->height;
            frame.pitch = map_snap->pitch;
            frame.indices = map_snap->indices;
            frame.layout = map_snap->layout;
            for (size_t i = 0; i < 256; ++i)
                for (size_t c = 0; c < 4; ++c) frame.palette[i][c] = map_snap->palette[i * 4 + c];

            std::optional<renderer::ComposedMapFrame> composed;
            if (ui_view) {
                composed = renderer::MapViewCompositor::TryCompose(*ui_view, frame);
            } else {
                composed = renderer::MapViewCompositor::TryComposeNativeMap(frame);
            }
            if (!composed || !map_view::PublishComposedLayout(runtime, &*map_snap))
                throw std::runtime_error("native map frame composition failed");
            return PS2OwnedPresentationFrame{composed->pixels, composed->width, composed->height, composed->strideBytes};
        }

        map_view::PublishComposedLayout(runtime, nullptr);
        if (ui_view) {
            auto composed = renderer::MapViewCompositor::TryComposeUi(*ui_view, out_w, out_h);
            if (composed) {
                return PS2OwnedPresentationFrame{composed->pixels, composed->width, composed->height, composed->strideBytes};
            }
        }
        return std::nullopt;
    });
#endif
    runtime.setFrameCompositor([&runtime](PS2PresentationFrameView legacy) -> std::optional<PS2OwnedPresentationFrame> {
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
#if defined(__SWITCH__)
    runtime_->setNativeFrameProvider(nullptr);
#endif
    runtime_->setFrameCompositor(nullptr);
    map_view::Teardown(*runtime_);
    runtime_ = nullptr;
}

}  // namespace aoe2::game_hooks

