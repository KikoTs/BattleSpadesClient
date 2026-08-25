#include "battlespades/render/bgfx_ui_renderer.hpp"

#include <iostream>

int main() {
    const auto backends = battlespades::render::supported_graphics_backends();
    for (const auto backend : backends) {
        std::cout << battlespades::render::graphics_backend_name(backend) << '\n';
    }
    return backends.empty() ? 1 : 0;
}
