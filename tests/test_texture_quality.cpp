#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/render/texture_quality.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

using battlespades::render::TextureQualityTier;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, std::string_view message) {
    if (!condition) fail(message);
}

void source_resource_roots_are_selected_without_touching_ui_art() {
    using battlespades::render::texture_quality_asset;
    expect(texture_quality_asset("png/high/white.png", TextureQualityTier::low) ==
               std::filesystem::path{"png/low/white.png"},
           "Low must redirect the native authored high key to png/low");
    expect(texture_quality_asset("png/high/white.png", TextureQualityTier::medium) ==
               std::filesystem::path{"png/med/white.png"},
           "Medium must use retail's abbreviated png/med spelling");
    expect(texture_quality_asset("png/low/white.png", TextureQualityTier::high) ==
               std::filesystem::path{"png/high/white.png"},
           "High must replace any prior quality-root spelling");
    expect(texture_quality_asset("png/ui/cursor.png", TextureQualityTier::low) ==
               std::filesystem::path{"png/ui/cursor.png"},
           "fixed-resolution png/ui art must not be quality redirected");
    expect(texture_quality_asset("../png/high/white.png", TextureQualityTier::low) ==
               std::filesystem::path{"../png/high/white.png"},
           "unsafe paths must remain available to the checked loader for rejection");
}

void shipped_quality_roots_are_complete_and_decode_at_their_authored_sizes() {
    using battlespades::render::decode_png_rgba8;
    using battlespades::render::texture_quality_asset;
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    struct Fixture final {
        TextureQualityTier quality;
        std::uint32_t side;
    };
    for (const auto fixture : {Fixture{TextureQualityTier::low, 128U},
                               Fixture{TextureQualityTier::medium, 256U},
                               Fixture{TextureQualityTier::high, 512U}}) {
        const auto relative = texture_quality_asset(
            "png/high/Tumbling_cube_anim.png", fixture.quality);
        expect(std::filesystem::is_regular_file(root / relative),
               "each retail texture root must contain the selected particle atlas");
        const auto decoded = decode_png_rgba8(root / relative);
        expect(decoded && decoded.texture->extent.width == fixture.side &&
                   decoded.texture->extent.height == fixture.side,
               "particle atlas dimensions must follow retail Low/Medium/High roots");
    }
}

} // namespace

int main() {
    source_resource_roots_are_selected_without_touching_ui_art();
    shipped_quality_roots_are_complete_and_decode_at_their_authored_sizes();
    std::cout << "texture quality tests passed\n";
    return EXIT_SUCCESS;
}
