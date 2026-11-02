#pragma once
#include "battlespades/render/bgfx_ui_renderer.hpp"
#include <span>

namespace battlespades::render {
/** Decode an authenticated PNG payload without creating plaintext files. */
[[nodiscard]] UiTextureDecodeResult decode_png_rgba8(std::span<const std::uint8_t> bytes);
} // namespace battlespades::render
