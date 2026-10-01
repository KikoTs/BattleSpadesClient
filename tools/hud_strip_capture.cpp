// Developer tool (not a test): CPU-composites the tool-switch strip draw list
// into PNGs so the retail hud.pyd geometry can be inspected without a window.
//
//   aos_hud_strip_capture out=<dir> [assets=<root>] [width=1280] [height=720]
//
// Writes strip-after-<n>.png from the live GameHudPresentation and
// strip-before-<n>.png from a verbatim copy of the pre-parity strip code,
// for a pistol, spade, block and two prefabs, each selected in turn. Sprites
// are box-filtered from the shipped PNGs; labels use the production
// FreeType/HarfBuzz rasterizer. It approximates the bgfx UI pass closely
// enough for layout comparison; it is not a pixel-exact renderer oracle.

#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/text/text_rasterizer.hpp"
#include "battlespades/ui/draw_list.hpp"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {
using namespace battlespades;

struct Image final {
    int width{};
    int height{};
    std::vector<float> rgba; // premultiplied, 0..1
};

struct Canvas final {
    int width{};
    int height{};
    std::vector<float> rgb;

    Canvas(int w, int h) : width{w}, height{h}, rgb(static_cast<std::size_t>(w * h * 3)) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                // Muted sky/ground backdrop so white, green and red read.
                const float shade = y < h / 2 ? 0.42F : 0.30F;
                auto* p = &rgb[static_cast<std::size_t>((y * w + x) * 3)];
                p[0] = shade * 0.85F;
                p[1] = shade * 0.90F;
                p[2] = shade;
            }
        }
    }

    void blend(int x, int y, float r, float g, float b, float a) {
        if (x < 0 || y < 0 || x >= width || y >= height || a <= 0.0F) return;
        auto* p = &rgb[static_cast<std::size_t>((y * width + x) * 3)];
        p[0] = r + p[0] * (1.0F - a);
        p[1] = g + p[1] * (1.0F - a);
        p[2] = b + p[2] * (1.0F - a);
    }
};

class Assets final {
public:
    explicit Assets(std::filesystem::path root) : root_{std::move(root)} {}

    const Image* get(const std::string& id) {
        const auto found = cache_.find(id);
        if (found != cache_.end()) return found->second.get();
        auto image = std::make_unique<Image>();
        int channels{};
        const auto path = (root_ / id).string();
        stbi_uc* data = stbi_load(path.c_str(), &image->width, &image->height, &channels, 4);
        if (data == nullptr) {
            std::cerr << "missing asset " << path << '\n';
            cache_[id] = nullptr;
            return nullptr;
        }
        image->rgba.resize(static_cast<std::size_t>(image->width * image->height * 4));
        for (std::size_t i = 0; i < image->rgba.size(); i += 4U) {
            const float a = static_cast<float>(data[i + 3U]) / 255.0F;
            for (std::size_t c = 0; c < 3U; ++c) {
                image->rgba[i + c] = static_cast<float>(data[i + c]) / 255.0F * a;
            }
            image->rgba[i + 3U] = a;
        }
        stbi_image_free(data);
        return (cache_[id] = std::move(image)).get();
    }

private:
    std::filesystem::path root_;
    std::map<std::string, std::unique_ptr<Image>> cache_;
};

void draw_sprite(Canvas& canvas, const Image& image, const ui::DrawRect& rect) {
    if (rect.width <= 0.0 || rect.height <= 0.0) return;
    const int x0 = static_cast<int>(std::floor(rect.x));
    const int y0 = static_cast<int>(std::floor(rect.y));
    const int x1 = static_cast<int>(std::ceil(rect.x + rect.width));
    const int y1 = static_cast<int>(std::ceil(rect.y + rect.height));
    constexpr int samples{6};
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float acc[4]{};
            for (int sy = 0; sy < samples; ++sy) {
                for (int sx = 0; sx < samples; ++sx) {
                    const double px = x + (sx + 0.5) / samples;
                    const double py = y + (sy + 0.5) / samples;
                    const double u = (px - rect.x) / rect.width;
                    const double v = (py - rect.y) / rect.height;
                    if (u < 0.0 || v < 0.0 || u >= 1.0 || v >= 1.0) continue;
                    const int tx = std::clamp(static_cast<int>(u * image.width), 0, image.width - 1);
                    const int ty = std::clamp(static_cast<int>(v * image.height), 0, image.height - 1);
                    const float* t = &image.rgba[static_cast<std::size_t>((ty * image.width + tx) * 4)];
                    for (int c = 0; c < 4; ++c) acc[c] += t[c];
                }
            }
            constexpr float n = samples * samples;
            canvas.blend(x, y, acc[0] / n, acc[1] / n, acc[2] / n, acc[3] / n);
        }
    }
}

void draw_text(Canvas& canvas, text::TextRasterizer& fonts, const ui::TextDrawCommand& command) {
    text::TextRasterRequest request;
    request.utf8 = command.localization_key;
    request.pixel_height =
        std::max<std::uint32_t>(1U, static_cast<std::uint32_t>(
                                        std::lround(command.requested_font_size_pixels)));
    const auto result = fonts.rasterize(request);
    if (!result) {
        std::cerr << "text failed: " << result.error << '\n';
        return;
    }
    const auto& raster = *result.output;
    const auto& m = raster.metrics;
    double pen_x = command.destination.x;
    if (command.horizontal_alignment == ui::HorizontalTextAlignment::center) {
        pen_x = command.destination.x + command.destination.width * 0.5 -
                m.advance_width_pixels * 0.5;
    }
    double baseline = command.destination.y;
    if (command.vertical_alignment == ui::VerticalTextAlignment::top) {
        baseline = command.destination.y + m.ascender_pixels;
    }
    const int left = static_cast<int>(std::lround(pen_x)) - m.baseline_x_in_bitmap;
    const int top = static_cast<int>(std::lround(baseline)) - m.baseline_y_in_bitmap;
    const auto color = command.modulation.color;
    for (std::uint32_t y = 0; y < raster.bitmap.height; ++y) {
        for (std::uint32_t x = 0; x < raster.bitmap.width; ++x) {
            const auto a = static_cast<float>(
                               raster.bitmap.pixels[y * raster.bitmap.row_stride_bytes + x * 4U + 3U]) /
                           255.0F;
            canvas.blend(left + static_cast<int>(x), top + static_cast<int>(y),
                         color.red / 255.0F * a, color.green / 255.0F * a,
                         color.blue / 255.0F * a, a);
        }
    }
}

// Verbatim geometry of the strip before the hud.pyd parity pass (git HEAD
// src/frontend/game_hud.cpp): 0.25 frame, 0.4/0.9 portraits, no blueprint,
// selected entry without the green frame, top-left 11/20 px labels.
ui::DrawList legacy_strip(const std::vector<frontend::GameHudInventorySlot>& slots,
                          std::size_t selected_index, double window_width,
                          double window_height) {
    ui::DrawList list;
    constexpr double stride{80.0};
    constexpr double frame_normal{195.0 * 0.25};
    constexpr double frame_selected{195.0 * 0.70};
    constexpr double icon_normal{330.0 * 0.40 * 0.5};
    constexpr double icon_selected{330.0 * 0.90 * 0.5};
    constexpr double label_normal{1.3 * 38.0 * 0.5};
    constexpr double label_selected{2.0 * 38.0 * 0.5};
    const double count = static_cast<double>(slots.size());
    const double start_x = window_width * 0.5 - count * stride * 0.5 + stride * 0.5;
    const double center_y = window_height * 0.70;
    const auto sprite = [](std::string asset, double l, double t, double w, double h) {
        ui::SpriteDrawCommand command;
        command.asset_id = std::move(asset);
        command.destination = {l, t, w, h};
        command.space = ui::DrawSpace::window_pixels;
        return command;
    };
    for (std::size_t index{}; index < slots.size(); ++index) {
        const bool selected = index == selected_index;
        const double frame = selected ? frame_selected : frame_normal;
        const double icon = selected ? icon_selected : icon_normal;
        const double cx = start_x + static_cast<double>(index) * stride;
        list.push(sprite(selected ? "png/ui/weapon_select/weapon_frame_selected.png"
                                  : "png/ui/weapon_select/weapon_frame.png",
                         cx - frame * 0.5, center_y - frame * 0.5, frame, frame));
        list.push(sprite(slots[index].icon_asset, cx - icon * 0.5, center_y - icon * 0.5, icon,
                         icon));
        const double offset = selected ? label_selected : label_normal;
        ui::TextDrawCommand label;
        label.localization_key = index == 9U ? "0" : std::to_string(index + 1U);
        label.destination = {cx + offset, center_y + offset, 18.0, 18.0};
        label.space = ui::DrawSpace::window_pixels;
        label.requested_font_size_pixels = selected ? 20.0 : 11.0;
        label.modulation.color = {255U, 239U, 0U, 255U};
        list.push(std::move(label));
    }
    return list;
}

bool strip_command(const ui::DrawCommand& command, double top, double bottom) {
    if (const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
        const auto& id = sprite->asset_id;
        const bool strip_art = id.find("weapon_select/") != std::string::npos ||
                               id.find("prefab_selection/") != std::string::npos ||
                               id.find("prefabs/") == 0U || id.find("png/ui/weapons/") == 0U;
        return strip_art && sprite->destination.y < bottom &&
               sprite->destination.y + sprite->destination.height > top;
    }
    if (const auto* label = std::get_if<ui::TextDrawCommand>(&command)) {
        return label->localization_key.size() <= 2U && label->destination.y > top &&
               label->destination.y < bottom;
    }
    return false;
}

void write_png(const Canvas& canvas, int crop_top, int crop_height, const std::string& path) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(canvas.width * crop_height * 3));
    for (int y = 0; y < crop_height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float v = canvas.rgb[static_cast<std::size_t>(
                    ((y + crop_top) * canvas.width + x) * 3 + c)];
                bytes[static_cast<std::size_t>((y * canvas.width + x) * 3 + c)] =
                    static_cast<std::uint8_t>(std::clamp(v, 0.0F, 1.0F) * 255.0F + 0.5F);
            }
        }
    }
    if (stbi_write_png(path.c_str(), canvas.width, crop_height, 3, bytes.data(),
                       canvas.width * 3) == 0) {
        throw std::runtime_error{"PNG write failed: " + path};
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> args;
        for (int i = 1; i < argc; ++i) {
            const std::string arg{argv[i]};
            const auto eq = arg.find('=');
            if (eq != std::string::npos) args[arg.substr(0, eq)] = arg.substr(eq + 1U);
        }
        const auto get = [&](const std::string& key, const std::string& fallback) {
            const auto found = args.find(key);
            return found == args.end() ? fallback : found->second;
        };
        const std::filesystem::path out{get("out", ".")};
        std::filesystem::create_directories(out);
        const std::filesystem::path asset_root{get("assets", AOS_TOOL_ASSET_ROOT)};
        const int width = std::stoi(get("width", "1280"));
        const int height = std::stoi(get("height", "720"));

        Assets assets{asset_root};
        text::TextRasterizer fonts{text::TextRasterizerConfig{
            asset_root, frontend::game_hud_assets::help_font, 18U, {}, true}};
        if (!fonts.ready()) throw std::runtime_error{std::string{fonts.initialization_error()}};

        using Slot = frontend::GameHudInventorySlot;
        const std::vector<Slot> slots{
            Slot{"png/ui/weapons/pistol.png", "1"},
            Slot{"png/ui/weapons/spade.png", "2"},
            Slot{"png/ui/weapons/block.png", "3"},
            Slot{"prefabs/prefab_barricade.png", "4", true, 211.0},
            Slot{"prefabs/prefab_bunker_wall.png", "5", true, 211.0},
        };
        const double center_y = height * 0.70;
        const int crop_top = static_cast<int>(center_y) - 120;
        const int crop_height = 200;
        const frontend::GameHudPresentation presentation;
        for (std::size_t selected{}; selected < slots.size(); ++selected) {
            for (const bool after : {false, true}) {
                ui::DrawList list;
                if (after) {
                    frontend::GameHudModel model;
                    model.set_inventory_state(slots, selected, true);
                    list = presentation.build(
                        model, frontend::GameHudPresentationContext{{width, height}, 1'000U});
                } else {
                    list = legacy_strip(slots, selected, width, height);
                }
                Canvas canvas{width, height};
                for (const auto& command : list.commands()) {
                    if (!strip_command(command, crop_top, crop_top + crop_height)) continue;
                    if (const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
                        if (const auto* image = assets.get(sprite->asset_id)) {
                            draw_sprite(canvas, *image, sprite->destination);
                        }
                    } else if (const auto* label = std::get_if<ui::TextDrawCommand>(&command)) {
                        draw_text(canvas, fonts, *label);
                    }
                }
                const auto path = (out / ((after ? "strip-after-" : "strip-before-") +
                                          std::to_string(selected + 1U) + ".png"))
                                      .string();
                write_png(canvas, crop_top, crop_height, path);
                std::cout << "wrote " << path << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "hud_strip_capture: " << error.what() << '\n';
        return 1;
    }
}
