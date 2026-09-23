#include "battlespades/core/command_line.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace battlespades::core {
namespace {

constexpr std::uint64_t nanoseconds_per_second{1'000'000'000U};
constexpr std::uint64_t maximum_tick_rate{1'000U};
#if AOS_ENABLE_DEVELOPER_TOOLS
constexpr std::uint64_t maximum_tool_id{64U};
#endif

[[nodiscard]] std::optional<std::uint64_t> parse_positive_integer(std::string_view text) {
    std::uint64_t value{};
    const auto* const begin = text.data();
    const auto* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);

    if (result.ec != std::errc{} || result.ptr != end || value == 0U) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] ParseResult failure(std::string message) {
    return ParseResult{.options = std::nullopt, .error = std::move(message)};
}

#if AOS_ENABLE_DEVELOPER_TOOLS
/**
 * Accepts only a plain asset basename.
 *
 * These options are joined onto an asset directory, so anything that could
 * escape it -- a separator, a parent reference, a drive letter -- is rejected
 * rather than sanitised.
 */
[[nodiscard]] bool safe_asset_stem(std::string_view text) noexcept {
    if (text.empty() || text.size() > 64U) {
        return false;
    }
    return std::ranges::all_of(text, [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '_' || character == '-' ||
               character == '.';
    }) && text.find("..") == std::string_view::npos;
}

[[nodiscard]] std::optional<std::array<double, 3U>> parse_triplet(std::string_view text) {
    std::array<double, 3U> values{};
    std::size_t begin{};
    for (std::size_t axis{}; axis < values.size(); ++axis) {
        const auto comma = text.find(',', begin);
        const bool last = axis + 1U == values.size();
        if ((last && comma != std::string_view::npos) ||
            (!last && comma == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto piece =
            text.substr(begin, last ? std::string_view::npos : comma - begin);
        if (piece.empty()) {
            return std::nullopt;
        }
        const auto* const end = piece.data() + piece.size();
        const auto result = std::from_chars(piece.data(), end, values[axis]);
        if (result.ec != std::errc{} || result.ptr != end ||
            !std::isfinite(values[axis])) {
            return std::nullopt;
        }
        begin = comma + 1U;
    }
    return values;
}

[[nodiscard]] std::optional<std::array<double, 2U>> parse_pair(std::string_view text) {
    const auto comma = text.find(',');
    if (comma == std::string_view::npos ||
        text.find(',', comma + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    std::array<double, 2U> values{};
    const std::array<std::string_view, 2U> pieces{text.substr(0U, comma),
                                                  text.substr(comma + 1U)};
    for (std::size_t axis{}; axis < values.size(); ++axis) {
        if (pieces[axis].empty()) {
            return std::nullopt;
        }
        const auto* const end = pieces[axis].data() + pieces[axis].size();
        const auto result = std::from_chars(pieces[axis].data(), end, values[axis]);
        if (result.ec != std::errc{} || result.ptr != end ||
            !std::isfinite(values[axis])) {
            return std::nullopt;
        }
    }
    return values;
}
#endif

} // namespace

ParseResult parse_command_line(std::span<const std::string_view> arguments) {
    LaunchOptions options{};

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];

        if (argument == "--help" || argument == "-h") {
            options.action = LaunchAction::show_help;
            continue;
        }
        if (argument == "--version") {
            options.action = LaunchAction::show_version;
            continue;
        }
        if (argument == "--headless") {
            options.headless = true;
            continue;
        }
        if (argument == "--steam-only") {
            options.steam_only = true;
            continue;
        }
        if (argument == "--pace") {
            options.runtime.pace_to_wall_clock = true;
            continue;
        }
        if (argument == "--run-forever") {
            options.runtime.tick_limit.reset();
            options.runtime.pace_to_wall_clock = true;
            options.runtime_lifetime_explicit = true;
            continue;
        }
#if AOS_ENABLE_DEVELOPER_TOOLS
        if (argument == "--tutorial-cosmetic") {
            if (++index>=arguments.size() || !safe_asset_stem(arguments[index]))
                return failure("--tutorial-cosmetic requires a bundled cosmetic id");
            options.tutorial_debug_cosmetic=std::string{arguments[index]};
            continue;
        }
        if (argument == "--tutorial-tool") {
            if (++index >= arguments.size()) {
                return failure("--tutorial-tool requires an integer from 0 to 64");
            }
            std::uint64_t value{};
            const auto text = arguments[index];
            const auto result =
                std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} ||
                result.ptr != text.data() + text.size() ||
                value > maximum_tool_id) {
                return failure("--tutorial-tool requires an integer from 0 to 64");
            }
            options.tutorial_debug_tool = static_cast<std::uint8_t>(value);
            continue;
        }
        if (argument == "--tutorial-aim") {
            options.tutorial_debug_aim = true;
            continue;
        }
        if (argument == "--tutorial-map") {
            if (++index >= arguments.size() || !safe_asset_stem(arguments[index])) {
                return failure("--tutorial-map requires a map basename");
            }
            options.tutorial_map = std::string{arguments[index]};
            continue;
        }
        if (argument == "--tutorial-skydome") {
            if (++index >= arguments.size() || !safe_asset_stem(arguments[index])) {
                return failure("--tutorial-skydome requires a skydome basename");
            }
            options.tutorial_skydome = std::string{arguments[index]};
            continue;
        }
        if (argument == "--tutorial-spawn") {
            if (++index >= arguments.size()) {
                return failure("--tutorial-spawn requires X,Y,Z");
            }
            const auto parsed = parse_triplet(arguments[index]);
            if (!parsed.has_value()) {
                return failure("--tutorial-spawn requires X,Y,Z");
            }
            options.tutorial_spawn = *parsed;
            continue;
        }
        if (argument == "--tutorial-stand") {
            if (++index >= arguments.size()) {
                return failure("--tutorial-stand requires X,Y");
            }
            const auto parsed = parse_pair(arguments[index]);
            if (!parsed.has_value()) {
                return failure("--tutorial-stand requires X,Y");
            }
            options.tutorial_stand = *parsed;
            continue;
        }
        if (argument == "--tutorial-look") {
            if (++index >= arguments.size()) {
                return failure("--tutorial-look requires X,Y,Z");
            }
            const auto parsed = parse_triplet(arguments[index]);
            if (!parsed.has_value()) {
                return failure("--tutorial-look requires X,Y,Z");
            }
            options.tutorial_look = *parsed;
            continue;
        }
#endif
        if (argument == "--shader-quality") {
            // Validated for shape only. The set of legal tier names lives with
            // the settings enum, and duplicating it here is exactly the drift
            // that once rewrote Ultra as "medium" on save.
            if (++index >= arguments.size() || arguments[index].empty() ||
                arguments[index].size() > 16U) {
                return failure("--shader-quality requires a tier name");
            }
            options.shader_quality = std::string{arguments[index]};
            continue;
        }
        if (argument == "--connect") {
            if (++index >= arguments.size() || arguments[index].empty()) {
                return failure("--connect requires HOST:PORT or steam:STEAMID");
            }
            options.startup_endpoint = std::string{arguments[index]};
            continue;
        }
#if AOS_ENABLE_DEVELOPER_TOOLS
        if (argument == "--debug-vfx") {
            if (++index >= arguments.size()) {
                return failure("--debug-vfx requires corpse|grave|grenade|rocket");
            }
            const auto value = arguments[index];
            if (value != "corpse" && value != "grave" && value != "grenade" &&
                value != "rocket") {
                return failure("--debug-vfx requires corpse|grave|grenade|rocket");
            }
            options.debug_vfx = std::string{value};
            continue;
        }
        if (argument == "--debug-vfx-age") {
            if (++index >= arguments.size()) {
                return failure("--debug-vfx-age requires seconds from 0 to 1.95");
            }
            double value{};
            const auto text = arguments[index];
            const auto result =
                std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
                !std::isfinite(value) || value < 0.0 || value > 1.95) {
                return failure("--debug-vfx-age requires seconds from 0 to 1.95");
            }
            options.debug_vfx_age = value;
            continue;
        }
        if (argument == "--debug-ui") {
            if (++index >= arguments.size()) {
                return failure(
                    "--debug-ui requires leaderboard|scoreboard|endgame|chat|vote|inventory|inventory/empty|inventory/crates|inventory/packs|inventory/reveal|inventory/stg44|create_match|create_match/notice|friends|friends/requests|friends/invites|friends/lobby|friends/offline|friends/busy|loading/map|loading/mode|loading/scores|loading/hosting|loading/error|ugc_lobby|ugc_browser|ugc_loadout");
            }
            const auto value = arguments[index];
            if (value != "leaderboard" && value != "scoreboard" &&
                value != "endgame" && value != "chat" && value != "vote" &&
                value != "inventory" && value != "inventory/crates" && value != "inventory/packs" &&
                value != "inventory/reveal" && value != "inventory/stg44" &&
                value != "inventory/empty" && value != "create_match" && value != "create_match/notice" &&
                value != "friends" && value != "friends/requests" && value != "friends/invites" &&
                value != "friends/lobby" && value != "friends/offline" && value != "friends/busy" &&
                value != "loading/map" && value != "loading/mode" && value != "loading/scores" &&
                value != "loading/hosting" && value != "loading/error" &&
                value != "ugc_lobby" && value != "ugc_browser" && value != "ugc_loadout") {
                return failure(
                    "--debug-ui requires leaderboard|scoreboard|endgame|chat|vote|inventory|inventory/empty|inventory/crates|inventory/packs|inventory/reveal|inventory/stg44|create_match|create_match/notice|friends|friends/requests|friends/invites|friends/lobby|friends/offline|friends/busy|loading/map|loading/mode|loading/scores|loading/hosting|loading/error|ugc_lobby|ugc_browser|ugc_loadout");
            }
            options.debug_ui = std::string{value};
            continue;
        }
        if (argument == "--ui-editor") {
            options.ui_layout_editor = true;
            continue;
        }
#endif
        if (argument == "--ticks") {
            if (++index >= arguments.size()) {
                return failure("--ticks requires a positive integer");
            }
            const auto value = parse_positive_integer(arguments[index]);
            if (!value.has_value()) {
                return failure("--ticks requires a positive integer");
            }
            options.runtime.tick_limit = *value;
            options.runtime_lifetime_explicit = true;
            continue;
        }
        if (argument == "--tick-rate") {
            if (++index >= arguments.size()) {
                return failure("--tick-rate requires an integer from 1 to 1000");
            }
            const auto value = parse_positive_integer(arguments[index]);
            if (!value.has_value() || *value > maximum_tick_rate) {
                return failure("--tick-rate requires an integer from 1 to 1000");
            }

            const auto period = nanoseconds_per_second / *value;
            options.runtime.fixed_delta =
                std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(period)};
            continue;
        }

        return failure("unknown option: " + std::string{argument});
    }

#if AOS_ENABLE_DEVELOPER_TOOLS
    if (options.debug_vfx.has_value() && options.startup_endpoint.has_value()) {
        return failure("--debug-vfx is offline and cannot be combined with --connect");
    }
    if (options.debug_vfx.has_value() && options.headless) {
        return failure("--debug-vfx requires the graphical renderer");
    }
    if (options.debug_vfx_age.has_value() && !options.debug_vfx.has_value()) {
        return failure("--debug-vfx-age requires --debug-vfx");
    }
    if (options.debug_ui.has_value() && options.startup_endpoint.has_value()) {
        return failure("--debug-ui is offline and cannot be combined with --connect");
    }
    if (options.debug_ui.has_value() && options.headless) {
        return failure("--debug-ui requires the graphical renderer");
    }
    if (options.debug_ui.has_value() && options.debug_vfx.has_value()) {
        return failure("--debug-ui cannot be combined with --debug-vfx");
    }
    if (options.ui_layout_editor && options.headless) {
        return failure("--ui-editor requires the graphical renderer");
    }
    if (options.ui_layout_editor && options.startup_endpoint.has_value()) {
        return failure("--ui-editor is offline and cannot be combined with --connect");
    }
#endif

    return ParseResult{.options = options, .error = {}};
}

std::string_view command_line_usage() noexcept {
    return "Usage: BattleSpadesClient [options]\n"
           "  --headless          Run without platform or graphics backends\n"
           "  --ticks N           Stop after N fixed simulation ticks (default: 1)\n"
           "  --tick-rate N       Fixed simulation frequency from 1 to 1000 Hz\n"
           "  --pace              Pace fixed ticks against the monotonic wall clock\n"
           "  --run-forever       Run paced until externally stopped\n"
#if AOS_ENABLE_DEVELOPER_TOOLS
           "  --tutorial-tool ID  Equip tool 0..64 in the local Tutorial lab\n"
           "  --tutorial-aim      Start the Tutorial lab with aiming toggled on\n"
           "  --tutorial-cosmetic ID  Preview a bundled cosmetic in the local Tutorial lab\n"
           "  --tutorial-map N    Load map N from assets/original/maps instead\n"
           "  --tutorial-skydome N  Override the skydome, e.g. Tokyo.txt\n"
           "  --tutorial-spawn X,Y,Z  Spawn at a canonical voxel position\n"
           "  --tutorial-stand X,Y    Resolve a safe standing spot near this column\n"
           "  --tutorial-look X,Y,Z   Aim the camera at a canonical position\n"
           "  --debug-vfx NAME    Offline corpse|grave|grenade|rocket visual oracle\n"
           "  --debug-vfx-age S   Freeze the oracle at exact simulated age S\n"
           "  --debug-ui NAME     Offline leaderboard|scoreboard|endgame|chat|vote|inventory|inventory/empty|inventory/crates|inventory/packs|inventory/reveal|inventory/stg44|create_match|ugc_lobby|ugc_browser|ugc_loadout UI oracle\n"
           "                      Also loading/map|loading/mode|loading/scores|loading/hosting|loading/error|create_match/notice\n"
           "                      Also friends|friends/requests|friends/invites|friends/lobby|friends/offline|friends/busy\n"
           "  --ui-editor        Enable offline drag/resize layout editing (F11)\n"
#endif
           "  --shader-quality T  Force a tier: compatibility|low|medium|high|ultra\n"
           "  --connect ADDRESS   Open the live loader for HOST:PORT, or for\n"
           "                      steam:STEAMID to join a player-hosted match\n"
           "  --steam-only        Host a Local Match over Steam alone, with no\n"
           "                      AoSPlay relay lobby (testing the Steam path)\n"
           "  --version           Print build version and profile\n"
           "  --help, -h          Show this help\n";
}

} // namespace battlespades::core
