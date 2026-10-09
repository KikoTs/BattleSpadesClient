#include "battlespades/settings/settings_store.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace battlespades::settings {
namespace {

constexpr std::uintmax_t maximum_settings_file_bytes{1U * 1'024U * 1'024U};
constexpr std::size_t maximum_settings_lines{4'096U};

enum class Section : std::uint8_t {
    root,
    main,
    graphics,
    controls,
    control_bindings,
    unknown,
};

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r' || value.front() == '\n')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                              value.back() == '\n')) {
        value.remove_suffix(1U);
    }
    return value;
}

[[nodiscard]] std::string_view without_comment(std::string_view value) noexcept {
    bool quoted{false};
    bool escaped{false};
    for (std::size_t index{}; index < value.size(); ++index) {
        const auto character = value[index];
        if (quoted && escaped) {
            escaped = false;
            continue;
        }
        if (quoted && character == '\\') {
            escaped = true;
            continue;
        }
        if (character == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && character == '#') {
            return value.substr(0U, index);
        }
    }
    return value;
}

[[nodiscard]] std::optional<std::string> parse_string(std::string_view value) {
    value = trim(value);
    if (value.size() < 2U || value.front() != '"' || value.back() != '"') {
        return std::nullopt;
    }

    std::string result;
    result.reserve(value.size() - 2U);
    bool escaped{false};
    for (std::size_t index{1U}; index + 1U < value.size(); ++index) {
        const auto character = value[index];
        if (escaped) {
            switch (character) {
            case '\\':
            case '"':
                result.push_back(character);
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                return std::nullopt;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return std::nullopt;
        } else {
            result.push_back(character);
        }
    }
    if (escaped) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<bool> parse_bool(std::string_view value) noexcept {
    value = trim(value);
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> parse_unsigned(std::string_view value) noexcept {
    value = trim(value);
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<double> parse_double(std::string_view value) {
    std::istringstream stream{std::string{trim(value)}};
    stream.imbue(std::locale::classic());
    double result{};
    stream >> result;
    if (!stream || !stream.eof() || !std::isfinite(result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<Resolution> parse_resolution(std::string_view value) noexcept {
    const auto separator = value.find('x');
    if (separator == std::string_view::npos ||
        value.find('x', separator + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    const auto width = parse_unsigned(value.substr(0U, separator));
    const auto height = parse_unsigned(value.substr(separator + 1U));
    if (!width.has_value() || !height.has_value()) {
        return std::nullopt;
    }
    return Resolution{*width, *height};
}

[[nodiscard]] std::optional<QualityLevel> parse_quality(std::string_view value) noexcept {
    if (value == "low") {
        return QualityLevel::low;
    }
    if (value == "medium") {
        return QualityLevel::medium;
    }
    if (value == "high") {
        return QualityLevel::high;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<DrawDistance> parse_draw_distance(std::string_view value) noexcept {
    if (value == "low") {
        return DrawDistance::low;
    }
    if (value == "medium") {
        return DrawDistance::medium;
    }
    if (value == "high") {
        return DrawDistance::high;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Antialiasing> parse_antialiasing(std::string_view value) noexcept {
    if (value == "off") {
        return Antialiasing::off;
    }
    if (value == "2x") {
        return Antialiasing::samples_2;
    }
    if (value == "4x") {
        return Antialiasing::samples_4;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view quality_name(QualityLevel value) noexcept {
    switch (value) {
    case QualityLevel::low:
        return "low";
    case QualityLevel::medium:
        return "medium";
    case QualityLevel::high:
        return "high";
    }
    return "medium";
}

[[nodiscard]] constexpr std::string_view draw_distance_name(DrawDistance value) noexcept {
    switch (value) {
    case DrawDistance::low:
        return "low";
    case DrawDistance::medium:
        return "medium";
    case DrawDistance::high:
        return "high";
    }
    return "high";
}

[[nodiscard]] constexpr std::string_view antialiasing_name(Antialiasing value) noexcept {
    switch (value) {
    case Antialiasing::off:
        return "off";
    case Antialiasing::samples_2:
        return "2x";
    case Antialiasing::samples_4:
        return "4x";
    }
    return "off";
}

[[nodiscard]] constexpr std::string_view section_name(Section section) noexcept {
    switch (section) {
    case Section::root:
        return "root";
    case Section::main:
        return "main";
    case Section::graphics:
        return "graphics";
    case Section::controls:
        return "controls";
    case Section::control_bindings:
        return "controls.bindings";
    case Section::unknown:
        return "unknown";
    }
    return "unknown";
}

[[nodiscard]] Section parse_section(std::string_view value) noexcept {
    if (value == "main") {
        return Section::main;
    }
    if (value == "graphics") {
        return Section::graphics;
    }
    if (value == "controls") {
        return Section::controls;
    }
    if (value == "controls.bindings") {
        return Section::control_bindings;
    }
    return Section::unknown;
}

struct ParseState final {
    ClientSettings candidate{retail_default_settings()};
    Section section{Section::root};
    std::set<std::string, std::less<>> recognized_keys{};
    std::vector<std::string> ignored_keys{};
    std::string error{};
    // Pre-`window_mode` files: retail [main] fullscreen and the native
    // [graphics] fullscreen_mode, resolved once the whole file is read.
    std::optional<bool> legacy_fullscreen{};
    std::optional<bool> legacy_borderless{};
    bool window_mode_read{false};

    [[nodiscard]] bool fail(std::size_t line, std::string message) {
        error = "line " + std::to_string(line) + ": " + std::move(message);
        return false;
    }

    [[nodiscard]] bool remember(std::size_t line, std::string_view key) {
        auto qualified = std::string{section_name(section)} + '.' + std::string{key};
        if (!recognized_keys.emplace(qualified).second) {
            return fail(line, "duplicate setting '" + qualified + "'");
        }
        return true;
    }

    void ignore(std::string_view key) {
        ignored_keys.emplace_back(std::string{section_name(section)} + '.' + std::string{key});
    }
};

template <typename Value, typename Parser>
[[nodiscard]] bool assign_quoted(
    ParseState& state, std::size_t line, std::string_view raw, Value& destination, Parser parser) {
    const auto text = parse_string(raw);
    if (!text.has_value()) {
        return state.fail(line, "expected a quoted string");
    }
    const auto parsed = parser(*text);
    if (!parsed.has_value()) {
        return state.fail(line, "setting has an unsupported value");
    }
    destination = *parsed;
    return true;
}

/**
 * The native Graphics additions in [graphics]. Nothing when `key` is not one
 * of them; otherwise whether the value was accepted. Range checks are left to
 * validate_settings so a bad value fails the load like any other.
 */
[[nodiscard]] std::optional<bool> parse_native_graphics(ParseState& state,
                                                        std::size_t line,
                                                        std::string_view key,
                                                        std::string_view value) {
    auto& graphics = state.candidate.graphics;
    const auto number = [&](double& destination) {
        if (!state.remember(line, key)) {
            return false;
        }
        const auto parsed = parse_double(value);
        if (!parsed.has_value()) {
            return state.fail(line, std::string{key} + " must be a finite number");
        }
        destination = *parsed;
        return true;
    };
    const auto toggle = [&](bool& destination) {
        if (!state.remember(line, key)) {
            return false;
        }
        const auto parsed = parse_bool(value);
        if (!parsed.has_value()) {
            return state.fail(line, std::string{key} + " must be true or false");
        }
        destination = *parsed;
        return true;
    };
    const auto named = [&](const auto& names, auto& destination) {
        if (!state.remember(line, key)) {
            return false;
        }
        const auto text = parse_string(value);
        const auto parsed = text.has_value() ? parse_enum(names, *text) : std::nullopt;
        if (!parsed.has_value()) {
            return state.fail(line, std::string{key} + " has an unsupported value");
        }
        destination = *parsed;
        return true;
    };
    if (key == "field_of_view") return number(graphics.field_of_view);
    if (key == "render_scale") return number(graphics.render_scale);
    if (key == "sharpness") return number(graphics.sharpness);
    if (key == "brightness") return number(graphics.brightness);
    if (key == "gamma") return number(graphics.gamma);
    if (key == "low_latency") return toggle(graphics.low_latency);
    if (key == "show_fps") return toggle(graphics.show_fps);
    if (key == "anisotropic_filtering") return toggle(graphics.anisotropic_filtering);
    if (key == "smooth_textures") return toggle(graphics.smooth_textures);
    if (key == "frame_limit") return named(frame_limit_names, graphics.frame_limit);
    if (key == "upscale") return named(upscale_filter_names, graphics.upscale);
    if (key == "shadow_quality") return named(shadow_quality_names, graphics.shadow_quality);
    if (key == "shadow_distance") return named(shadow_distance_names, graphics.shadow_distance);
    if (key == "ambient_occlusion") return named(effect_level_names, graphics.ambient_occlusion);
    if (key == "bloom") return named(effect_level_names, graphics.bloom);
    if (key == "motion_blur") return named(effect_level_names, graphics.motion_blur);
    if (key == "color_vision") return named(color_vision_names, graphics.color_vision);
    if (key == "frame_rate_cap") {
        if (!state.remember(line, key)) {
            return false;
        }
        const auto parsed = parse_unsigned(value);
        if (!parsed.has_value() || *parsed > 0xFFFFU) {
            return state.fail(line, "frame_rate_cap must be a whole number");
        }
        graphics.frame_rate_cap = static_cast<std::uint16_t>(*parsed);
        return true;
    }
    return std::nullopt;
}

[[nodiscard]] bool parse_assignment(ParseState& state,
                                    std::size_t line,
                                    std::string_view key,
                                    std::string_view value) {
    if (key.empty()) {
        return state.fail(line, "setting key is empty");
    }

    if (state.section == Section::root) {
        if (key != "schema_version") {
            state.ignore(key);
            return true;
        }
        if (!state.remember(line, key)) {
            return false;
        }
        const auto version = parse_unsigned(value);
        if (!version.has_value() || *version != current_settings_schema_version) {
            return state.fail(line, "unsupported schema_version");
        }
        state.candidate.schema_version = *version;
        return true;
    }

    if (state.section == Section::main) {
        if (key == "classic_sky" || key == "classic_fog") {
            if (!state.remember(line, key)) return false;
            const auto parsed = parse_string(value);
            if (!parsed || (key == "classic_sky" ? classic_sky_option(*parsed) == nullptr
                                                 : !valid_classic_fog(*parsed)))
                return state.fail(line, "invalid Classic atmosphere preference");
            (key == "classic_sky" ? state.candidate.main.classic_sky
                                  : state.candidate.main.classic_fog) = *parsed;
            return true;
        }
        if (key == "classic_fog_red" || key == "classic_fog_green" || key == "classic_fog_blue") {
            if (!state.remember(line, key)) return false;
            const auto parsed = parse_unsigned(value);
            if (!parsed || *parsed > 255U) return state.fail(line, "fog channel must be 0..255");
            const auto channel = key == "classic_fog_red" ? 0U : key == "classic_fog_green" ? 1U : 2U;
            state.candidate.main.classic_fog_color[channel] = static_cast<std::uint8_t>(*parsed);
            return true;
        }
        if (key == "audio_device") {
            if (!state.remember(line, key)) return false;
            const auto parsed = parse_string(value);
            if (!parsed.has_value()) return state.fail(line, "audio_device must be a quoted name");
            state.candidate.main.audio_device = *parsed;
            return true;
        }
        if (key == "language") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_string(value);
            if (!parsed.has_value()) {
                return state.fail(line, "language must be a quoted locale tag");
            }
            state.candidate.main.language = *parsed;
            return true;
        }
        if (key == "master_volume" || key == "music_volume") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_double(value);
            if (!parsed.has_value()) {
                return state.fail(line, "volume must be a finite number");
            }
            if (key == "master_volume") {
                state.candidate.main.master_volume = *parsed;
            } else {
                state.candidate.main.music_volume = *parsed;
            }
            return true;
        }
        if (key == "fullscreen" || key == "invert_mouse" || key == "show_skins" ||
            key == "show_other_skins" || key == "weapon_motion" || key == "ability_hints" ||
            key == "fallback_music" || key == "ragdoll_corpses" || key == "blood_marks" ||
            key == "discord_presence" || key == "discord_join") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_bool(value);
            if (!parsed.has_value()) {
                return state.fail(line, "toggle must be true or false");
            }
            if (key == "fullscreen") {
                state.legacy_fullscreen = *parsed;
            } else if (key == "show_skins") {
                state.candidate.main.show_skins = *parsed;
            } else if (key == "show_other_skins") {
                state.candidate.main.show_other_skins = *parsed;
            } else if (key == "weapon_motion") {
                state.candidate.main.weapon_motion = *parsed;
            } else if (key == "ability_hints") {
                state.candidate.main.ability_hints = *parsed;
            } else if (key == "fallback_music") {
                state.candidate.main.fallback_music = *parsed;
            } else if (key == "blood_marks") {
                state.candidate.main.blood_marks = *parsed;
            } else if (key == "ragdoll_corpses") {
                state.candidate.main.ragdoll_corpses = *parsed;
            } else if (key == "discord_presence") {
                state.candidate.main.discord_presence = *parsed;
            } else if (key == "discord_join") {
                state.candidate.main.discord_join = *parsed;
            } else {
                state.candidate.main.invert_mouse = *parsed;
            }
            return true;
        }
        state.ignore(key);
        return true;
    }

    if (state.section == Section::graphics) {
        if (key == "vsync") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_bool(value);
            if (!parsed.has_value()) {
                return state.fail(line, "vsync must be true or false");
            }
            state.candidate.graphics.vsync = *parsed;
            return true;
        }
        if (key == "render_interpolation") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_bool(value);
            if (!parsed.has_value()) {
                return state.fail(line, "render_interpolation must be true or false");
            }
            state.candidate.graphics.render_interpolation = *parsed;
            return true;
        }
        if (key == "hud_scale") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_double(value);
            if (!parsed.has_value() || !std::isfinite(*parsed) ||
                !(*parsed == 0.0 || (*parsed >= 1.0 && *parsed <= 4.0))) {
                return state.fail(line, "hud_scale must be 0 (auto) or between 1.0 and 4.0");
            }
            state.candidate.graphics.hud_scale = *parsed;
            return true;
        }
        if (const auto handled = parse_native_graphics(state, line, key, value);
            handled.has_value()) {
            return *handled;
        }
        if (key == "fullscreen_mode") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_string(value);
            if (!parsed.has_value() || (*parsed != "borderless" && *parsed != "exclusive")) {
                return state.fail(line, "fullscreen_mode must be \"borderless\" or \"exclusive\"");
            }
            state.legacy_borderless = *parsed == "borderless";
            return true;
        }
        if (key == "window_mode") {
            if (!state.remember(line, key)) {
                return false;
            }
            const auto parsed = parse_string(value);
            const auto mode = parsed.has_value() ? parse_window_mode(*parsed) : std::nullopt;
            if (!mode.has_value()) {
                return state.fail(
                    line, "window_mode must be \"windowed\", \"borderless\" or \"exclusive\"");
            }
            state.candidate.graphics.window_mode = *mode;
            state.window_mode_read = true;
            return true;
        }
        if (!state.remember(line, key)) {
            return false;
        }
        if (key == "resolution") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.resolution, parse_resolution);
        }
        if (key == "graphics_api") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.graphics_api, parse_graphics_api);
        }
        if (key == "antialiasing") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.antialiasing, parse_antialiasing);
        }
        if (key == "effect_quality") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.effect_quality, parse_quality);
        }
        if (key == "draw_distance") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.draw_distance, parse_draw_distance);
        }
        if (key == "shader_quality") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.shader_quality, parse_shader_quality);
        }
        if (key == "texture_quality") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.texture_quality, parse_quality);
        }
        if (key == "model_quality") {
            return assign_quoted(
                state, line, value, state.candidate.graphics.model_quality, parse_quality);
        }
        // We remembered a graphics key before learning it was unknown; remove
        // it from duplicate tracking and preserve forward compatibility.
        state.recognized_keys.erase(std::string{section_name(state.section)} + '.' +
                                    std::string{key});
        state.ignore(key);
        return true;
    }

    if (state.section == Section::controls) {
        if (key != "mouse_sensitivity") {
            state.ignore(key);
            return true;
        }
        if (!state.remember(line, key)) {
            return false;
        }
        const auto parsed = parse_double(value);
        if (!parsed.has_value()) {
            return state.fail(line, "mouse_sensitivity must be a finite number");
        }
        state.candidate.controls.mouse_sensitivity = *parsed;
        return true;
    }

    if (state.section == Section::control_bindings) {
        const auto action = control_action_from_name(key);
        if (!action.has_value()) {
            state.ignore(key);
            return true;
        }
        if (!state.remember(line, key)) {
            return false;
        }
        const auto text = parse_string(value);
        if (!text.has_value()) {
            return state.fail(line, "binding must be a quoted string");
        }
        const auto binding = binding_from_string(*text);
        if (!binding.has_value()) {
            return state.fail(line, "binding has an unsupported value");
        }
        static_cast<void>(state.candidate.controls.set_binding(*action, *binding));
        return true;
    }

    state.ignore(key);
    return true;
}

[[nodiscard]] std::string serialize(const ClientSettings& settings) {
    const auto decimal = [](double value) {
        // Floating-point to_chars requires macOS 13.3. Settings are saved
        // infrequently, so find the shortest round-tripping precision with
        // the locale-independent streams available on every supported OS.
        std::string text;
        for (int precision = 1; precision <= std::numeric_limits<double>::max_digits10; ++precision) {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(precision) << value;
            text = output.str();
            if (parse_double(text) == value) {
                break;
            }
        }
        return text;
    };

    const auto& g = settings.graphics;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "# BattleSpadesClient settings\n"
           << "# Generated atomically. Unknown keys are ignored for forward compatibility.\n"
           << "schema_version = " << settings.schema_version << "\n\n"
           << "[main]\n"
           << "language = \"" << settings.main.language << "\"\n"
           << "master_volume = " << decimal(settings.main.master_volume) << "\n"
           << "music_volume = " << decimal(settings.main.music_volume) << "\n"
           << "fallback_music = " << (settings.main.fallback_music ? "true" : "false") << "\n"
           << "ragdoll_corpses = " << (settings.main.ragdoll_corpses ? "true" : "false") << "\n"
           << "blood_marks = " << (settings.main.blood_marks ? "true" : "false") << "\n"
           << "discord_presence = " << (settings.main.discord_presence ? "true" : "false") << "\n"
           << "discord_join = " << (settings.main.discord_join ? "true" : "false") << "\n"
           << "classic_sky = " << std::quoted(settings.main.classic_sky) << "\n"
           << "classic_fog = " << std::quoted(settings.main.classic_fog) << "\n"
           << "classic_fog_red = " << static_cast<unsigned>(settings.main.classic_fog_color[0U]) << "\n"
           << "classic_fog_green = " << static_cast<unsigned>(settings.main.classic_fog_color[1U]) << "\n"
           << "classic_fog_blue = " << static_cast<unsigned>(settings.main.classic_fog_color[2U]) << "\n"
           << "audio_device = " << std::quoted(settings.main.audio_device) << "\n"
           << "# Legacy mirror of [graphics] window_mode for older builds; ignored here.\n"
           << "fullscreen = " << (is_fullscreen(settings.graphics.window_mode) ? "true" : "false")
           << "\n"
           << "invert_mouse = " << (settings.main.invert_mouse ? "true" : "false") << "\n"
           << "show_skins = " << (settings.main.show_skins ? "true" : "false") << "\n"
           << "show_other_skins = " << (settings.main.show_other_skins ? "true" : "false") << "\n"
           << "weapon_motion = " << (settings.main.weapon_motion ? "true" : "false") << "\n"
           << "ability_hints = " << (settings.main.ability_hints ? "true" : "false") << "\n\n"
           << "[graphics]\n"
           << "resolution = \"" << settings.graphics.resolution.width << 'x'
           << settings.graphics.resolution.height << "\"\n"
           << "graphics_api = \"" << graphics_api_name(settings.graphics.graphics_api) << "\"\n"
           << "antialiasing = \"" << antialiasing_name(settings.graphics.antialiasing) << "\"\n"
           << "effect_quality = \"" << quality_name(settings.graphics.effect_quality) << "\"\n"
           << "draw_distance = \"" << draw_distance_name(settings.graphics.draw_distance) << "\"\n"
           << "shader_quality = \"" << shader_quality_name(settings.graphics.shader_quality)
           << "\"\n"
           << "texture_quality = \"" << quality_name(settings.graphics.texture_quality) << "\"\n"
           << "model_quality = \"" << quality_name(settings.graphics.model_quality) << "\"\n"
           << "vsync = " << (settings.graphics.vsync ? "true" : "false") << "\n"
           << "# \"windowed\", \"borderless\" (desktop-sized, no display mode change) or\n"
           << "# \"exclusive\" (switches the display to `resolution`).\n"
           << "window_mode = \"" << window_mode_name(settings.graphics.window_mode) << "\"\n"
           << "# Legacy mirror of window_mode for older builds; ignored when window_mode is set.\n"
           << "fullscreen_mode = \""
           << (settings.graphics.window_mode == WindowMode::exclusive ? "exclusive" : "borderless")
           << "\"\n"
           << "# Native: render-only frames between 60 Hz ticks on high-refresh displays.\n"
           << "render_interpolation = "
           << (settings.graphics.render_interpolation ? "true" : "false") << "\n"
           << "# Native: in-game HUD magnification for high-DPI displays; 1.0 = retail\n"
           << "# raw pixels, 0 = auto (floor(height / 1080)).\n"
           << "hud_scale = " << decimal(settings.graphics.hud_scale) << "\n"
           << "# Native Graphics additions; each default matches the renderer before them.\n"
           << "field_of_view = " << decimal(g.field_of_view) << "\n"
           << "# \"display\" (one frame per refresh), \"custom\" (frame_rate_cap) or \"unlimited\".\n"
           << "frame_limit = \"" << enum_name(frame_limit_names, g.frame_limit) << "\"\n"
           << "frame_rate_cap = " << g.frame_rate_cap << "\n"
           << "low_latency = " << (g.low_latency ? "true" : "false") << "\n"
           << "show_fps = " << (g.show_fps ? "true" : "false") << "\n"
           << "render_scale = " << decimal(g.render_scale) << "\n"
           << "upscale = \"" << enum_name(upscale_filter_names, g.upscale) << "\"\n"
           << "sharpness = " << decimal(g.sharpness) << "\n"
           << "anisotropic_filtering = " << (g.anisotropic_filtering ? "true" : "false") << "\n"
           << "smooth_textures = " << (g.smooth_textures ? "true" : "false") << "\n"
           << "shadow_quality = \"" << enum_name(shadow_quality_names, g.shadow_quality) << "\"\n"
           << "shadow_distance = \"" << enum_name(shadow_distance_names, g.shadow_distance)
           << "\"\n"
           << "ambient_occlusion = \"" << enum_name(effect_level_names, g.ambient_occlusion)
           << "\"\n"
           << "bloom = \"" << enum_name(effect_level_names, g.bloom) << "\"\n"
           << "motion_blur = \"" << enum_name(effect_level_names, g.motion_blur) << "\"\n"
           << "brightness = " << decimal(g.brightness) << "\n"
           << "gamma = " << decimal(g.gamma) << "\n"
           << "color_vision = \"" << enum_name(color_vision_names, g.color_vision) << "\"\n\n"
           << "[controls]\n"
           << "mouse_sensitivity = " << decimal(settings.controls.mouse_sensitivity) << "\n\n"
           << "[controls.bindings]\n"
           << "# Values: unbound, keyboard:<name>, mouse:<button>.\n";
    for (std::size_t index{}; index < control_action_count; ++index) {
        const auto action = static_cast<ControlAction>(index);
        output << control_action_name(action) << " = \""
               << binding_to_string(settings.controls.bindings[index]) << "\"\n";
    }
    return output.str();
}

[[nodiscard]] std::filesystem::path temporary_path_for(const std::filesystem::path& target) {
    static std::atomic<std::uint64_t> sequence{};
    const auto stamp =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    auto temporary = target;
    temporary += ".tmp." + std::to_string(stamp) + '.' +
                 std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));
    return temporary;
}

[[nodiscard]] bool replace_atomically(const std::filesystem::path& source,
                                      const std::filesystem::path& destination,
                                      std::string& error) {
#if defined(_WIN32)
    if (::MoveFileExW(source.c_str(),
                      destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
    }
    error = "atomic replacement failed with Win32 error " + std::to_string(::GetLastError());
    return false;
#else
    std::error_code filesystem_error;
    std::filesystem::rename(source, destination, filesystem_error);
    if (!filesystem_error) {
        return true;
    }
    error = "atomic replacement failed: " + filesystem_error.message();
    return false;
#endif
}

} // namespace

TomlSettingsStore::TomlSettingsStore(std::filesystem::path path) : path_{std::move(path)} {}

const std::filesystem::path& TomlSettingsStore::path() const noexcept {
    return path_;
}

SettingsLoadResult TomlSettingsStore::load() const {
    SettingsLoadResult result{};
    if (path_.empty()) {
        result.error = "settings path is empty";
        return result;
    }

    std::error_code filesystem_error;
    const auto exists = std::filesystem::exists(path_, filesystem_error);
    if (filesystem_error) {
        result.error = "cannot inspect settings file: " + filesystem_error.message();
        return result;
    }
    if (!exists) {
        // Product decision D1 (RETAIL_PARITY_GAPS_2026-09-27): a new install
        // starts on the Retail look -- the audited Legacy lighting equations
        // with server fog -- and Medium is one toggle away. Existing files
        // keep whatever tier the player saved.
        result.settings.graphics.shader_quality = ShaderQuality::compatibility;
        result.success = true;
        return result;
    }
    result.file_found = true;

    const auto file_size = std::filesystem::file_size(path_, filesystem_error);
    if (filesystem_error) {
        result.error = "cannot determine settings file size: " + filesystem_error.message();
        return result;
    }
    if (file_size > maximum_settings_file_bytes) {
        result.error = "settings file exceeds the 1 MiB safety limit";
        return result;
    }

    std::ifstream input{path_, std::ios::binary};
    if (!input) {
        result.error = "cannot open settings file for reading";
        return result;
    }

    ParseState state{};
    std::string line_text;
    std::size_t line_number{};
    while (std::getline(input, line_text)) {
        ++line_number;
        if (line_number > maximum_settings_lines) {
            result.error = "settings file exceeds the 4096-line safety limit";
            return result;
        }
        auto line = trim(without_comment(line_text));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            if (line.size() < 3U || line.back() != ']' ||
                line.find('[', 1U) != std::string_view::npos ||
                line.find(']') != line.size() - 1U) {
                result.error = "line " + std::to_string(line_number) + ": malformed section";
                return result;
            }
            const auto name = trim(line.substr(1U, line.size() - 2U));
            state.section = parse_section(name);
            if (state.section == Section::unknown) {
                state.ignored_keys.emplace_back("section:" + std::string{name});
            }
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string_view::npos) {
            result.error = "line " + std::to_string(line_number) + ": expected key = value";
            return result;
        }
        const auto key = trim(line.substr(0U, separator));
        const auto value = trim(line.substr(separator + 1U));
        if (!parse_assignment(state, line_number, key, value)) {
            result.error = state.error;
            return result;
        }
    }
    if (!input.eof()) {
        result.error = "failed while reading settings file";
        return result;
    }

    if (!state.window_mode_read && (state.legacy_fullscreen.has_value() ||
                                    state.legacy_borderless.has_value())) {
        // Migration: Fullscreen OFF is windowed; ON keeps the kind the file
        // chose, where an unset fullscreen_mode was the native borderless
        // default. A file with neither key keeps the borderless default.
        state.candidate.graphics.window_mode =
            !state.legacy_fullscreen.value_or(true) ? WindowMode::windowed
            : state.legacy_borderless.value_or(true) ? WindowMode::borderless
                                                     : WindowMode::exclusive;
    }

    const auto validation = validate_settings(state.candidate);
    if (!validation) {
        result.error = "settings validation failed: " + validation.error;
        return result;
    }

    result.settings = state.candidate;
    result.ignored_keys = std::move(state.ignored_keys);
    result.success = true;
    return result;
}

SettingsSaveResult TomlSettingsStore::save(const ClientSettings& settings) const {
    if (path_.empty()) {
        return {false, "settings path is empty"};
    }
    const auto normalized = normalize_settings(settings);
    const auto validation = validate_settings(normalized);
    if (!validation) {
        return {false, "refusing to save invalid settings: " + validation.error};
    }

    std::error_code filesystem_error;
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            return {false, "cannot create settings directory: " + filesystem_error.message()};
        }
    }

    const auto temporary = temporary_path_for(path_);
    const auto content = serialize(normalized);
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            return {false, "cannot open temporary settings file for writing"};
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, filesystem_error);
            return {false, "failed while writing temporary settings file"};
        }
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, filesystem_error);
            return {false, "failed while closing temporary settings file"};
        }
    }

    std::string replace_error;
    if (!replace_atomically(temporary, path_, replace_error)) {
        std::filesystem::remove(temporary, filesystem_error);
        return {false, std::move(replace_error)};
    }
    return {true, {}};
}

} // namespace battlespades::settings
