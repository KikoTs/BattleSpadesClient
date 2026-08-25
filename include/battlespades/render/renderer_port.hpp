#pragma once

#include "battlespades/core/runtime_module.hpp"
#include "battlespades/platform/window_port.hpp"

namespace battlespades::render {

struct RenderFrame final {
    float interpolation_alpha{};
};

/**
 * Rendering boundary implemented by the future bgfx adapter.
 *
 * Simulation data will be submitted as immutable render snapshots; rendering
 * must never mutate authoritative game state.
 */
class RendererPort : public core::RuntimeModule {
public:
    virtual void resize(platform::WindowExtent extent) = 0;
    virtual void render(const RenderFrame& frame) = 0;
};

} // namespace battlespades::render
