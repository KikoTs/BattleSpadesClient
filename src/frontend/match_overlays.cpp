#include "battlespades/frontend/match_overlays.hpp"
#include "battlespades/frontend/player_progression.hpp"
#include "battlespades/world/map_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <set>
#include <utility>

namespace battlespades::frontend {

std::optional<std::string_view> match_result_audio_stem(
    std::int32_t winner_team, std::uint8_t local_team) noexcept {
    if ((winner_team != 2U && winner_team != 3U) ||
        (local_team != 2U && local_team != 3U)) {
        return std::nullopt;
    }
    return winner_team == local_team
               ? std::optional<std::string_view>{"mu_win_game"}
               : std::optional<std::string_view>{"mu_lose_game"};
}

std::int32_t match_result_winner_from_scores(
    const ChangeTeamServerState& state) noexcept {
    if (state.team1_score > state.team2_score) return 2;
    if (state.team2_score > state.team1_score) return 3;
    return 0;
}
namespace {

constexpr ui::ColorRgba8 menu_gold{244U, 236U, 187U, 255U};
constexpr ui::ColorRgba8 retail_team1_ui{44U, 117U, 179U, 255U};
constexpr ui::ColorRgba8 retail_team2_ui{137U, 179U, 44U, 255U};
constexpr ui::ColorRgba8 retail_list_dark{10U, 10U, 10U, 255U};
constexpr ui::ColorRgba8 retail_list_light{35U, 35U, 35U, 255U};
constexpr double retail_global_scale{0.64};
constexpr double retail_list_blend{0.2};
constexpr double rank_level_scale_from{0.3};
constexpr double rank_level_scale_to{3.0};
constexpr double rank_level_scale_up_seconds{0.0};
constexpr double rank_level_scale_down_seconds{0.4};

/** aoslib.strings.language_requires_tuffy, using retail language ordinals. */
[[nodiscard]] constexpr bool chat_language_requires_tuffy(
    std::uint8_t local_language) noexcept {
    return local_language == 6U || local_language == 7U ||
           local_language == 8U;
}

/** Exact ViewGameStats.draw_rank_ups Font.scale curve (A1075..A1078). */
[[nodiscard]] constexpr double rank_level_geometric_scale(
    double level_up_timer) noexcept {
    if (level_up_timer <= rank_level_scale_up_seconds) {
        return rank_level_scale_from;
    }
    const auto down_progress =
        (level_up_timer - rank_level_scale_up_seconds) /
        rank_level_scale_down_seconds;
    return std::min(rank_level_scale_to,
                    rank_level_scale_from + rank_level_scale_to -
                        down_progress * rank_level_scale_to);
}

[[nodiscard]] ui::SpriteDrawCommand sprite(
    std::string asset, ui::DrawRect bounds,
    ui::DrawSpace space = ui::DrawSpace::design_pixels,
    ui::ColorRgba8 tint = {}, std::uint16_t opacity = 1'000U,
    double source_scale = 0.5,
    ui::TextureAnchor source_anchor = ui::TextureAnchor::top_left) {
    return {std::move(asset), bounds, space, ui::TextureFilter::linear,
            source_anchor, source_scale,
            ui::SpriteSizing::stretch,
            ui::ColorModulation{tint, 1'000U, opacity}};
}

[[nodiscard]] ui::TextDrawCommand text(
    std::string value, ui::DrawRect bounds, double size,
    ui::DrawSpace space = ui::DrawSpace::design_pixels,
    ui::HorizontalTextAlignment alignment =
        ui::HorizontalTextAlignment::left,
    ui::ColorRgba8 tint = {},
    std::string font = "fonts/A750-Sans-Medium.ttf",
    ui::TextTransform transform = ui::TextTransform::preserve,
    ui::TextFit fit = ui::TextFit::retail_width_scale,
    ui::VerticalTextAlignment vertical =
        ui::VerticalTextAlignment::center) {
    return {std::move(value), std::move(font), bounds, space, size, 0.0, 1U,
            alignment, vertical, transform, fit,
            ui::ColorModulation{tint, 1'000U, 1'000U}};
}

/**
 * Append retail draw_stroked output in its exact eight-neighbour order.
 *
 * Pyglet applied these offsets in a bottom-left coordinate system. DrawList is
 * top-left based, therefore the Y component is inverted here and nowhere else.
 */
void append_stroked_text(ui::DrawList& list, std::string_view value,
                         ui::DrawRect bounds, ui::ColorRgba8 color,
                         std::string_view font, double font_size,
                         ui::HorizontalTextAlignment alignment =
                             ui::HorizontalTextAlignment::left,
                         ui::VerticalTextAlignment vertical =
                             ui::VerticalTextAlignment::top,
                         double line_spacing = 0.0,
                         std::uint8_t maximum_lines = 1U) {
    static constexpr std::array<std::pair<double, double>, 8U> offsets{
        std::pair{1.0, 1.0},   std::pair{1.0, -1.0},
        std::pair{-1.0, -1.0}, std::pair{-1.0, 1.0},
        std::pair{-1.0, 0.0},  std::pair{1.0, 0.0},
        std::pair{0.0, 1.0},   std::pair{0.0, -1.0},
    };
    const auto command = [&](ui::DrawRect destination,
                             ui::ColorRgba8 tint) {
        return ui::TextDrawCommand{
            std::string{value}, std::string{font}, destination,
            ui::DrawSpace::window_pixels, font_size, line_spacing,
            maximum_lines, alignment, vertical, ui::TextTransform::preserve,
            ui::TextFit::none,
            ui::ColorModulation{tint, 1'000U, 1'000U}};
    };
    for (const auto& [x_offset, y_offset] : offsets) {
        auto shadow_bounds = bounds;
        shadow_bounds.x += x_offset;
        shadow_bounds.y += y_offset;
        list.push(command(shadow_bounds,
                          {64U, 64U, 64U, color.alpha}));
    }
    list.push(command(bounds, color));
}

[[nodiscard]] bool printable(std::string_view value) {
    return std::ranges::all_of(value, [](unsigned char character) {
        return character >= 0x20U && character != 0x7FU;
    });
}

[[nodiscard]] std::string_view trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return value;
}

struct RetailLiteral final {
    enum class Kind : std::uint8_t {
        none,
        string,
        scalar,
        sequence,
    };

    Kind kind{Kind::none};
    std::string value;
    std::vector<RetailLiteral> children;
};

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7FU) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
        output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU &&
               !(code_point >= 0xD800U && code_point <= 0xDFFFU)) {
        output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0x10FFFFU) {
        output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        output.push_back(
            static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

class RetailLiteralParser final {
public:
    explicit RetailLiteralParser(std::string_view input) : input_(input) {}

    [[nodiscard]] std::optional<RetailLiteral> parse() {
        auto value = parse_value(0U);
        skip_space();
        if (!value.has_value() || cursor_ != input_.size()) return std::nullopt;
        return value;
    }

private:
    static constexpr std::size_t maximum_depth{8U};
    static constexpr std::size_t maximum_elements{32U};

    void skip_space() {
        while (cursor_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[cursor_])) != 0) {
            ++cursor_;
        }
    }

    [[nodiscard]] static int hex_digit(char value) noexcept {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    }

    [[nodiscard]] std::optional<std::uint32_t> parse_hex(std::size_t count) {
        if (count > input_.size() - cursor_) return std::nullopt;
        std::uint32_t result{};
        for (std::size_t index{}; index < count; ++index) {
            const auto digit = hex_digit(input_[cursor_ + index]);
            if (digit < 0) return std::nullopt;
            result = (result << 4U) | static_cast<std::uint32_t>(digit);
        }
        cursor_ += count;
        return result;
    }

    [[nodiscard]] std::optional<RetailLiteral> parse_string() {
        if (cursor_ < input_.size() &&
            (input_[cursor_] == 'u' || input_[cursor_] == 'U' ||
             input_[cursor_] == 'b' || input_[cursor_] == 'B')) {
            if (cursor_ + 1U >= input_.size() ||
                (input_[cursor_ + 1U] != '\'' && input_[cursor_ + 1U] != '"')) {
                return std::nullopt;
            }
            ++cursor_;
        }
        if (cursor_ >= input_.size() ||
            (input_[cursor_] != '\'' && input_[cursor_] != '"')) {
            return std::nullopt;
        }
        const auto quote = input_[cursor_++];
        std::string decoded;
        decoded.reserve(input_.size() - cursor_);
        while (cursor_ < input_.size()) {
            const auto current = input_[cursor_++];
            if (current == quote) {
                return RetailLiteral{RetailLiteral::Kind::string,
                                     std::move(decoded), {}};
            }
            if (current != '\\') {
                decoded.push_back(current);
                continue;
            }
            if (cursor_ >= input_.size()) return std::nullopt;
            const auto escaped = input_[cursor_++];
            switch (escaped) {
            case '\\':
            case '\'':
            case '"':
                decoded.push_back(escaped);
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case 'x': {
                const auto point = parse_hex(2U);
                if (!point.has_value()) return std::nullopt;
                append_utf8(decoded, *point);
                break;
            }
            case 'u': {
                const auto point = parse_hex(4U);
                if (!point.has_value()) return std::nullopt;
                append_utf8(decoded, *point);
                break;
            }
            case 'U': {
                const auto point = parse_hex(8U);
                if (!point.has_value()) return std::nullopt;
                append_utf8(decoded, *point);
                break;
            }
            default:
                // Python preserves an unknown escape's backslash.
                decoded.push_back('\\');
                decoded.push_back(escaped);
                break;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RetailLiteral>
    parse_sequence(std::size_t depth) {
        const auto open = input_[cursor_++];
        const auto close = open == '(' ? ')' : ']';
        RetailLiteral result{RetailLiteral::Kind::sequence, {}, {}};
        skip_space();
        if (cursor_ < input_.size() && input_[cursor_] == close) {
            ++cursor_;
            return result;
        }
        while (cursor_ < input_.size() &&
               result.children.size() < maximum_elements) {
            auto child = parse_value(depth + 1U);
            if (!child.has_value()) return std::nullopt;
            result.children.push_back(std::move(*child));
            skip_space();
            if (cursor_ >= input_.size()) return std::nullopt;
            if (input_[cursor_] == close) {
                ++cursor_;
                return result;
            }
            if (input_[cursor_++] != ',') return std::nullopt;
            skip_space();
            if (cursor_ < input_.size() && input_[cursor_] == close) {
                ++cursor_;
                return result;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<RetailLiteral>
    parse_scalar() {
        const auto begin = cursor_;
        while (cursor_ < input_.size()) {
            const auto value = input_[cursor_];
            if (value == ',' || value == ')' || value == ']' ||
                std::isspace(static_cast<unsigned char>(value)) != 0) {
                break;
            }
            ++cursor_;
        }
        if (cursor_ == begin) return std::nullopt;
        const auto token = input_.substr(begin, cursor_ - begin);
        const auto valid_named = token == "None" || token == "True" ||
                                 token == "False";
        if (!valid_named) {
            char* end{};
            const std::string copy{token};
            (void)std::strtod(copy.c_str(), &end);
            if (end != copy.c_str() + copy.size()) return std::nullopt;
        }
        return RetailLiteral{RetailLiteral::Kind::scalar,
                             std::string{token}, {}};
    }

    [[nodiscard]] std::optional<RetailLiteral>
    parse_value(std::size_t depth) {
        if (depth > maximum_depth) return std::nullopt;
        skip_space();
        if (cursor_ >= input_.size()) return std::nullopt;
        const auto value = input_[cursor_];
        if (value == '(' || value == '[') return parse_sequence(depth);
        if (value == '\'' || value == '"' || value == 'u' || value == 'U' ||
            value == 'b' || value == 'B') {
            return parse_string();
        }
        return parse_scalar();
    }

    std::string_view input_;
    std::size_t cursor_{};
};

[[nodiscard]] std::string_view retail_english_string(std::string_view id) {
    // Exact English values recovered from aoslib/strings/english.py. Unknown
    // IDs are deliberately returned unchanged, matching strings.get_by_id.
    static constexpr std::array values{
        std::pair<std::string_view, std::string_view>{"VOTE_MAP_TITLE",
                                                      "VOTE MAP"},
        std::pair<std::string_view, std::string_view>{
            "VOTE_MAP_DESCRIPTION",
            "Please select the map you would like to play next:"},
        std::pair<std::string_view, std::string_view>{"VOTE_TO_KICK_TITLE",
                                                      "Vote Kick"},
        std::pair<std::string_view, std::string_view>{
            "VOTE_TO_KICK_DESCRIPTION",
            "Vote to Kick {0} for {1}? Vote initiated by {2}"},
        std::pair<std::string_view, std::string_view>{"KICK_PLAYER",
                                                      "Kick Player"},
        std::pair<std::string_view, std::string_view>{"KICK_YES", "Yes"},
        std::pair<std::string_view, std::string_view>{"KICK_NO", "No"},
        std::pair<std::string_view, std::string_view>{"KICK_REASON_GRIEFING",
                                                      "Griefing"},
        std::pair<std::string_view, std::string_view>{"KICK_REASON_HACKING",
                                                      "Hacking"},
        std::pair<std::string_view, std::string_view>{"KICK_REASON_ABUSE",
                                                      "Abuse"},
    };
    const auto found = std::ranges::find_if(values, [id](const auto& item) {
        return item.first == id;
    });
    return found == values.end() ? id : found->second;
}

[[nodiscard]] std::string literal_argument(const RetailLiteral& value) {
    if (value.kind == RetailLiteral::Kind::string ||
        value.kind == RetailLiteral::Kind::scalar) {
        return value.value;
    }
    return {};
}

[[nodiscard]] std::optional<std::size_t>
literal_index(const RetailLiteral& value) {
    if (value.kind != RetailLiteral::Kind::scalar || value.value.empty() ||
        !std::ranges::all_of(value.value, [](unsigned char character) {
            return std::isdigit(character) != 0;
        })) {
        return std::nullopt;
    }
    char* end{};
    const auto parsed = std::strtoull(value.value.c_str(), &end, 10);
    if (end != value.value.c_str() + value.value.size() ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] std::string format_retail_string(
    std::string_view format, const std::vector<std::string>& arguments) {
    std::string output;
    output.reserve(format.size() + 32U);
    for (std::size_t cursor{}; cursor < format.size();) {
        if (format[cursor] == '{') {
            if (cursor + 1U < format.size() && format[cursor + 1U] == '{') {
                output.push_back('{');
                cursor += 2U;
                continue;
            }
            const auto close = format.find('}', cursor + 1U);
            if (close != std::string_view::npos) {
                const auto field = format.substr(cursor + 1U,
                                                 close - cursor - 1U);
                const auto separator = field.find_first_of("!:");
                const auto index_text = field.substr(0U, separator);
                if (!index_text.empty() &&
                    std::ranges::all_of(index_text,
                                        [](unsigned char character) {
                                            return std::isdigit(character) != 0;
                                        })) {
                    const std::string copy{index_text};
                    char* end{};
                    const auto index = std::strtoull(copy.c_str(), &end, 10);
                    if (end == copy.c_str() + copy.size() &&
                        index < arguments.size()) {
                        output += arguments[static_cast<std::size_t>(index)];
                        cursor = close + 1U;
                        continue;
                    }
                }
            }
        } else if (format[cursor] == '}' && cursor + 1U < format.size() &&
                   format[cursor + 1U] == '}') {
            output.push_back('}');
            cursor += 2U;
            continue;
        }
        output.push_back(format[cursor++]);
    }
    return output;
}

/** shared.common.blend_color: int(a*(1-f) + b*f), truncated per channel. */
[[nodiscard]] ui::ColorRgba8 retail_blend_color(
    ui::ColorRgba8 first, ui::ColorRgba8 second, double factor) noexcept {
    const auto blend = [factor](std::uint8_t left, std::uint8_t right) {
        return static_cast<std::uint8_t>(
            static_cast<double>(left) * (1.0 - factor) +
            static_cast<double>(right) * factor);
    };
    return {blend(first.red, second.red), blend(first.green, second.green),
            blend(first.blue, second.blue), 255U};
}

} // namespace

std::string_view retail_game_stat_award_label(
    std::int32_t stat_type) noexcept {
    // shared/constants.py GAME_STAT_TYPES maps packet ordinals 0..29 to
    // localization identifiers; strings/english.py supplies these authored
    // display values. These deliberately are not generic descriptions.
    static constexpr std::array<std::string_view, 30U> labels{
        "Just out for a stroll",
        "Barely touched the ground",
        "Hypochondriac",
        "Pancho Villa Resupply Award",
        "I <3 Block Crates Award",
        "Most Kills Award",
        "Healthbar Schmealthbar Award",
        "Milk And Two Teabagging Award",
        "Right Between the Eyes Award",
        "Bricklayer Award",
        "Destroy All The Things Award",
        "Longest Streaker Award",
        "Timber! Award for Destruction",
        "Eat My Draw Distance Range Award",
        "Up Close And Personal Melee Award",
        "Scholar by Proxy Brain Eating Award",
        "Attention Seeker Award",
        "You Shoot Him I Shoot You Defence Award",
        "\"Sir I Believe This is Your Kill\" Assist Award",
        "Invisible Helmet Airstrike Survival Kit",
        "Why Did I Buy A Bullet Magnet Award",
        "King of the Castle Highest Block Award",
        "Right in the Face Headshot Receiver Award",
        "Snipe THIS Counter-Sniper Award",
        "I Might Need Them Later Bullet Miser Award",
        "Didn't Get It Right First Time Suicide Award",
        "YOINK! Most Kill-Steals Award",
        "AAAAAAAAAAAAAAAAAA! Time on Fire Award",
        "Banana! Banana! Most Dominated",
        "Safety Word is Banana Domination",
    };
    if (stat_type < 0 ||
        static_cast<std::size_t>(stat_type) >= labels.size()) {
        return "Match Award";
    }
    return labels[static_cast<std::size_t>(stat_type)];
}

std::string_view retail_scoreboard_mode_title(std::uint8_t mode_type,
                                              bool classic) noexcept {
    // shared.constants_gamemode.MODE_TITLE owns ordinals 1..10/12. Classic
    // CTF is the deliberate exception: retail reconstructs it from the
    // InitialInfo.classic byte because MODE_MODE_IDS aliases cctf to CTF.
    switch (mode_type) {
    case 1U:
        return "Demolition!";
    case 2U:
        return "Zombie!";
    case 3U:
        return "Multi-Hill!";
    case 4U:
        return "Occupation!";
    case 5U:
        return "Diamond Mine!";
    case 6U:
        return "Team Deathmatch!";
    case 7U:
        return "VIP";
    case 8U:
        return classic ? "Classic CTF" : "Capture the Flag";
    case 9U:
        return "Territory Control";
    case 10U:
        return "Tutorial";
    case 11U:
        // MODE_CCTF exists in frontend tables even though the stock wire map
        // normally represents it as MODE_CTF + InitialInfo.classic.
        return "Classic CTF";
    case 12U:
        return "Map Creator";
    default:
        return "Scores";
    }
}

void GameChatModel::add(std::string message, ui::ColorRgba8 color) {
    if (message.empty()) return;
    if (message.size() > 200U) message.resize(200U);
    if (entries_.size() == maximum_entries) entries_.pop_back();
    // Retail inserts at zero: index zero is always the newest chat line.
    entries_.insert(entries_.begin(),
                    ChatFeedEntry{std::move(message), color,
                                  entry_lifetime_seconds, {}, false});
}

void GameChatModel::add_player_message(std::string sender,
                                       ui::ColorRgba8 sender_color,
                                       std::string message,
                                       bool team_message,
                                       std::uint8_t local_language) {
    if (sender.empty() || message.empty()) return;
    if (sender.size() > 64U) sender.resize(64U);
    if (message.size() > 200U) message.resize(200U);
    const auto prefix = sender + ": ";
    const auto body_color = team_message
                                ? retail_blend_color(
                                      sender_color,
                                      {255U, 255U, 255U, 255U}, 0.4)
                                : ui::ColorRgba8{255U, 255U, 255U, 255U};
    if (entries_.size() == maximum_entries) entries_.pop_back();
    entries_.insert(entries_.begin(),
                    ChatFeedEntry{prefix + message,
                                  body_color,
                                  entry_lifetime_seconds,
                                  {{prefix, sender_color},
                                   {std::move(message), body_color}},
                                  chat_language_requires_tuffy(local_language)});
}

void GameChatModel::tick(double elapsed_seconds) noexcept {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0) return;
    for (auto& entry : entries_) {
        entry.time_to_live_seconds -= elapsed_seconds;
    }
}

void GameChatModel::begin(ChatChannel channel) {
    channel_ = channel;
    input_.clear();
    active_ = true;
}

void GameChatModel::cancel() noexcept {
    input_.clear();
    active_ = false;
}

bool GameChatModel::append_text(std::string_view utf8) {
    if (!active_ || utf8.empty() ||
        input_.size() + utf8.size() > maximum_input_bytes ||
        utf8.find('\0') != std::string_view::npos ||
        utf8.find('\r') != std::string_view::npos ||
        utf8.find('\n') != std::string_view::npos) {
        return false;
    }
    input_.append(utf8);
    return true;
}

bool GameChatModel::erase_code_point() noexcept {
    if (!active_ || input_.empty()) return false;
    auto offset = input_.size() - 1U;
    while (offset > 0U &&
           (static_cast<unsigned char>(input_[offset]) & 0xC0U) == 0x80U) {
        --offset;
    }
    input_.erase(offset);
    return true;
}

std::optional<std::pair<ChatChannel, std::string>> GameChatModel::submit() {
    if (!active_) return std::nullopt;
    active_ = false;
    auto value = std::move(input_);
    input_.clear();
    if (value.empty()) return std::nullopt;
    return std::pair{channel_, std::move(value)};
}

ui::DrawList GameChatPresentation::build(
    const GameChatModel& model, ui::PixelExtent window,
    std::function<double(std::string_view, double,
                         std::string_view)> measure_text) const {
    ui::DrawList list;
    list.reserve(GameChatModel::shown_lines * 9U + 18U);
    // Direct probes of retail's bundled A750-Sans-Medium 12 wrapper return a
    // 13px line height. ChatLine stores that exact metric as content_height;
    // rows therefore advance by 13 + MSG_PAD * 2 = 23 pixels.
    constexpr double line_height{13.0};
    constexpr double line_pitch{23.0};
    constexpr double left{12.0};
    constexpr double bottom{65.0}; // MSG_BOTTOM_MARGIN + MSG_PAD.
    constexpr std::string_view standard_font{"fonts/A750-Sans-Medium.ttf"};
    constexpr std::string_view tuffy_font{"fonts/Tuffy_Bold.ttf"};
    const auto width = std::max(
        0.0, static_cast<double>(window.width) - left * 2.0);

    // entries()[0] is newest. HUD.draw_chat translates to the 12/65 baseline,
    // and ChatLine draws through FTGL's baseline-oriented Font.draw path.
    // Older lines climb by one measured 13px line plus two 5px pads.
    std::size_t rendered{};
    for (const auto& entry : model.entries()) {
        if (rendered == GameChatModel::shown_lines) break;
        if (!model.active() && entry.time_to_live_seconds <= 0.0) continue;

        const auto alpha_fraction = model.active()
                                        ? 1.0
                                        : std::clamp(
                                              entry.time_to_live_seconds /
                                                      GameChatModel::entry_lifetime_seconds *
                                                      2.0,
                                              0.0, 1.0);
        if (alpha_fraction <= 0.0) continue;
        const auto font = entry.use_tuffy_font ? tuffy_font : standard_font;
        const auto baseline = static_cast<double>(window.height) - bottom -
                              static_cast<double>(rendered) * line_pitch;
        const auto append_run = [&](std::string_view value,
                                    ui::ColorRgba8 run_color,
                                    double x) {
            run_color.alpha = static_cast<std::uint8_t>(std::clamp(
                alpha_fraction * static_cast<double>(run_color.alpha),
                0.0, 255.0));
            append_stroked_text(
                list, value,
                {x, baseline, std::max(0.0, width - (x - left)), line_height},
                run_color, font, 12.0, ui::HorizontalTextAlignment::left,
                ui::VerticalTextAlignment::baseline);
        };
        if (entry.runs.empty()) {
            append_run(entry.text, entry.color, left);
        } else {
            double x = left;
            for (const auto& run : entry.runs) {
                append_run(run.text, run.color, x);
                const auto measured = measure_text
                                          ? measure_text(run.text, 12.0, font)
                                          : static_cast<double>(run.text.size()) * 6.0;
                x += std::max(0.0, measured);
            }
        }
        ++rendered;
    }

    if (model.active()) {
        // Retail has two independent stroked labels and no fabricated input
        // panel or underscore cursor. input_label sits below chat_label.
        const auto input_baseline = static_cast<double>(window.height) - 15.0;
        const auto channel_baseline = static_cast<double>(window.height) - 40.0;
        append_stroked_text(list, model.input(),
                            {left, input_baseline, width, line_height},
                            {255U, 255U, 255U, 255U}, standard_font, 12.0,
                            ui::HorizontalTextAlignment::left,
                            ui::VerticalTextAlignment::baseline);
        const auto channel = model.channel() == ChatChannel::team
                                 ? std::string_view{"Team chat:"}
                                 : std::string_view{"Global chat:"};
        append_stroked_text(list, channel,
                            {left, channel_baseline, width, line_height},
                            {255U, 255U, 255U, 255U}, standard_font, 12.0,
                            ui::HorizontalTextAlignment::left,
                            ui::VerticalTextAlignment::baseline);
    }
    return list;
}

std::string GenericVotingModel::decode_retail_literal(
    std::string_view encoded) {
    encoded = trim(encoded);
    if (encoded.empty()) return {};
    const auto literal = RetailLiteralParser{encoded}.parse();
    if (literal.has_value() &&
        literal->kind == RetailLiteral::Kind::sequence &&
        literal->children.size() == 2U &&
        literal->children[1U].kind == RetailLiteral::Kind::sequence) {
        std::string identifier;
        std::set<std::size_t> localized_argument_indexes;
        const auto& identifier_field = literal->children[0U];
        if (identifier_field.kind == RetailLiteral::Kind::string) {
            identifier = identifier_field.value;
        } else if (identifier_field.kind == RetailLiteral::Kind::sequence &&
                   identifier_field.children.size() == 2U &&
                   identifier_field.children[0U].kind ==
                       RetailLiteral::Kind::string &&
                   identifier_field.children[1U].kind ==
                       RetailLiteral::Kind::sequence) {
            identifier = identifier_field.children[0U].value;
            for (const auto& item : identifier_field.children[1U].children) {
                if (const auto index = literal_index(item); index.has_value()) {
                    localized_argument_indexes.insert(*index);
                }
            }
        }
        if (!identifier.empty() && identifier.size() <= 128U &&
            printable(identifier)) {
            std::vector<std::string> arguments;
            arguments.reserve(literal->children[1U].children.size());
            for (std::size_t index{};
                 index < literal->children[1U].children.size(); ++index) {
                auto argument =
                    literal_argument(literal->children[1U].children[index]);
                if (localized_argument_indexes.contains(index)) {
                    argument = retail_english_string(argument);
                }
                arguments.push_back(std::move(argument));
            }
            return format_retail_string(retail_english_string(identifier),
                                        arguments);
        }
    }
    // Retail raised here. The port preserves printable operator text as inert
    // text instead, keeping malformed or legacy servers from terminating the
    // render loop while never evaluating the payload.
    if (encoded.size() <= 128U && printable(encoded)) {
        return std::string{encoded};
    }
    return "Vote option";
}

void GenericVotingModel::apply(
    const network::GenericVoteMessagePacket& packet) {
    const auto replace_candidates = [this, &packet]() {
        std::vector<VoteChoice> replacement;
        replacement.reserve(std::min<std::size_t>(packet.candidates.size(), 3U));
        for (const auto& candidate : packet.candidates) {
            if (replacement.size() == 3U) break;
            replacement.push_back(
                {candidate.name, decode_retail_literal(candidate.name),
                 candidate.votes});
        }
        choices_ = std::move(replacement);
        // set_candidates_to_vote resets both fields even for a count-only
        // UPDATE; this is a non-obvious retail invariant, not an optimization.
        voted_index_.reset();
        result_text_.clear();
    };

    switch (packet.message_type) {
    case network::GenericVoteMessagePacket::start:
        // process_packet_generic_vote_message calls reset, then setters, then
        // show(True) in this exact order before saving the scene-level flags.
        clear();
        can_vote_ = packet.can_vote;
        title_ = decode_retail_literal(packet.title);
        description_ = decode_retail_literal(packet.description);
        replace_candidates();
        allow_revote_ = packet.allow_revote;
        hide_after_vote_ = packet.hide_after_vote;
        visible_ = true;
        break;
    case network::GenericVoteMessagePacket::update:
        // Retail changes only candidate data/counts here. Packet flags, title,
        // description, and visibility remain owned by the START transition.
        replace_candidates();
        break;
    case network::GenericVoteMessagePacket::closed:
        can_vote_ = false;
        visible_ = false;
        // CLOSED immediately replaces show(False) with a six-second,
        // server-authored result message. It does not clear the candidate list.
        result_text_ = decode_retail_literal(packet.title);
        voted_message_seconds_ = closed_result_seconds;
        visible_ = true;
        break;
    case network::GenericVoteMessagePacket::cast:
    default:
        // CAST is client -> server. An inbound CAST has no retail HUD branch.
        break;
    }
}

void GenericVotingModel::tick(double elapsed_seconds) noexcept {
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0) return;

    // GenericVotingHUD.update uses if/elif: result lifetime always owns the
    // tick before a delayed hide-after-vote timer can advance.
    if (voted_message_seconds_.has_value()) {
        *voted_message_seconds_ -= elapsed_seconds;
        if (*voted_message_seconds_ < 0.0) {
            visible_ = false;
            voted_message_seconds_.reset();
        }
    } else if (hide_menu_seconds_.has_value()) {
        *hide_menu_seconds_ -= elapsed_seconds;
        if (*hide_menu_seconds_ < 0.0) {
            visible_ = false;
            hide_menu_seconds_.reset();
        }
    }
}

void GenericVotingModel::clear() noexcept {
    choices_.clear();
    title_.clear();
    description_.clear();
    result_text_.clear();
    voted_index_.reset();
    voted_message_seconds_.reset();
    hide_menu_seconds_.reset();
    visible_ = false;
    can_vote_ = false;
    allow_revote_ = false;
    hide_after_vote_ = false;
}

std::optional<std::string_view> GenericVotingModel::cast(
    std::size_t candidate_index) {
    if (!visible_ || !can_vote_) return std::nullopt;

    // GameScene first calls on_vote_cast(index, hide_after_vote, 5.0). The HUD
    // therefore remembers even an invalid index and schedules (rather than
    // immediately performing) hide-after-vote.
    voted_index_ = candidate_index;
    if (hide_after_vote_) hide_menu_seconds_ = hide_after_cast_seconds;
    if (candidate_index >= choices_.size()) return std::nullopt;

    // GameScene disables further input only after retrieving a valid candidate
    // and only when allow_vote_changing is false.
    if (!allow_revote_) can_vote_ = false;
    return choices_[candidate_index].wire_name;
}

ui::DrawList GenericVotingPresentation::build(
    const GenericVotingModel& model, ui::PixelExtent window,
    const std::array<std::string, 3U>& keys) const {
    ui::DrawList list;
    if (!model.visible()) return list;
    list.reserve(16U);
    constexpr ui::DrawSpace space{ui::DrawSpace::window_pixels};
    constexpr double x{10.0};
    constexpr double width{300.0};
    constexpr double height{200.0};
    const auto window_height = static_cast<double>(window.height);
    const auto y = window_height * 0.5 - height * 0.5;
    const auto top_baseline = [window_height](double retail_bottom_baseline) {
        return window_height - retail_bottom_baseline;
    };
    if (model.showing_result()) {
        constexpr double result_height{80.0};
        const auto result_y = y + 55.0;
        list.push(sprite("png/high/white.png",
                         {x + 5.0,
                          window_height - (result_y + result_height),
                          width - 10.0, result_height},
                         space,
                         {0U, 0U, 0U, 100U}, 1'000U, 1.0));
        auto result = text(std::string{model.result_text()},
                           {x + 5.0, top_baseline(result_y), width - 5.0,
                            result_height},
                           26.0,
                           space, ui::HorizontalTextAlignment::center,
                           {255U, 255U, 255U, 255U}, "fonts/Spades.ttf",
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale,
                           ui::VerticalTextAlignment::baseline);
        result.maximum_lines = 0U;
        result.line_spacing_pixels = 4.0;
        result.layout = ui::TextLayout::retail_wrapped_lines;
        list.push(std::move(result));
        return list;
    }

    list.push(sprite("png/high/white.png", {x, y, width - 10.0, height},
                     space, {0U, 0U, 0U, 100U}, 1'000U, 1.0));
    const auto title_y = y + height - 60.0;
    list.push(text(std::string{model.title()},
                   {x, top_baseline(title_y + 70.0 / 3.0), width - 10.0, 70.0},
                   26.0, space, ui::HorizontalTextAlignment::center,
                   {255U, 255U, 255U, 255U}, "fonts/Spades.ttf",
                   ui::TextTransform::preserve,
                   ui::TextFit::retail_width_scale,
                   ui::VerticalTextAlignment::baseline));
    const auto description_y = title_y - 10.0;
    auto description = text(
        std::string{model.description()},
        {x + 5.0, top_baseline(description_y), width - 5.0, 80.0}, 26.0,
        space, ui::HorizontalTextAlignment::center,
        {255U, 255U, 255U, 255U}, "fonts/Spades.ttf",
        ui::TextTransform::preserve, ui::TextFit::retail_width_scale,
        ui::VerticalTextAlignment::baseline);
    description.maximum_lines = 0U;
    description.line_spacing_pixels = 4.0;
    description.layout = ui::TextLayout::retail_wrapped_lines;
    list.push(std::move(description));

    // GenericVotingHUD draws in bottom-origin window coordinates. Candidate
    // rows start at description_y + (count - 1) * 30 - 115 and then descend by
    // 30. This deliberately keeps the first candidate highest after conversion
    // to our top-origin renderer.
    const auto candidate_count = static_cast<double>(model.choices().size());
    auto candidate_y = description_y +
                       std::max(0.0, candidate_count - 1.0) * 30.0 - 115.0;
    const auto candidate_span_width = width - width / 5.0;
    const auto candidate_x = x + (width - candidate_span_width) / 2.0;
    const auto candidate_text_width = width / 2.0;
    for (std::size_t index{}; index < model.choices().size(); ++index) {
        const auto row_baseline = top_baseline(candidate_y);
        double candidate_offset{};
        if (model.can_vote() && index < keys.size()) {
            list.push(text("[" + keys[index] + "] ",
                           {candidate_x, row_baseline, 25.0, 30.0}, 26.0,
                           space, ui::HorizontalTextAlignment::left,
                           {255U, 255U, 255U, 255U}, "fonts/Spades.ttf",
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale,
                           ui::VerticalTextAlignment::baseline));
            candidate_offset = 50.0;
        }
        list.push(text(model.choices()[index].display_text,
                       {candidate_x + candidate_offset, row_baseline,
                        candidate_text_width, 30.0},
                       26.0, space, ui::HorizontalTextAlignment::left,
                       {255U, 255U, 255U, 255U}, "fonts/Spades.ttf",
                       ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale,
                       ui::VerticalTextAlignment::baseline));
        const auto count_color =
            model.voted_index().has_value() && *model.voted_index() == index
                ? ui::ColorRgba8{0U, 255U, 0U, 255U}
                : ui::ColorRgba8{255U, 255U, 255U, 255U};
        list.push(text("[" + std::to_string(model.choices()[index].votes) + "]",
                       {width - 50.0, row_baseline, 25.0, 30.0}, 26.0,
                       space, ui::HorizontalTextAlignment::left, count_color,
                       "fonts/Spades.ttf", ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale,
                       ui::VerticalTextAlignment::baseline));
        candidate_y -= 30.0;
    }
    return list;
}

void MatchResultsModel::apply(const network::GameStatsPacket& packet) {
    // GameScene.process_packet_game_stats appends into game_stats_team1 or
    // game_stats_team2 based on packet.team_id. Official servers therefore
    // send two packet-67 records before packet 53. Clearing here silently
    // discarded the first team, while treating team_id as a winner made the
    // second packet announce a false green victory.
    if (awards_.empty()) {
        // A fresh result batch invalidates incomplete reset evidence from a
        // prior malformed/stale SetScore stream.  The second official team
        // packet appends to the same batch and therefore preserves the mask.
        unshown_score_reset_mask_ = 0U;
    }
    if (packet.team_id == 2 || packet.team_id == 3) {
        observed_stats_teams_ |= static_cast<std::uint8_t>(
            1U << static_cast<unsigned>(packet.team_id - 2));
    }
    constexpr std::size_t maximum_awards{256U};
    awards_.reserve(std::min(maximum_awards,
                             awards_.size() + packet.entries.size()));
    for (const auto& entry : packet.entries) {
        if (awards_.size() >= maximum_awards) break;
        awards_.push_back({static_cast<std::uint8_t>(entry.player_id),
                           entry.stat_type,
                           static_cast<std::uint8_t>(packet.team_id)});
    }
}

void MatchResultsModel::observe_team_score(std::uint8_t wire_team,
                                           std::int32_t score) noexcept {
    if (visible_ || awards_.empty()) {
        unshown_score_reset_mask_ = 0U;
        return;
    }
    if (wire_team != 2U && wire_team != 3U) {
        return;
    }
    if (score != 0) {
        // A normal score update between GameStats and ShowGameStats is not a
        // round boundary.  Require a new complete pair of zero resets.
        unshown_score_reset_mask_ = 0U;
        return;
    }
    unshown_score_reset_mask_ |= static_cast<std::uint8_t>(
        1U << static_cast<unsigned>(wire_team - 2U));
    if (unshown_score_reset_mask_ == 0x03U) {
        clear();
    }
}

void MatchResultsModel::on_map_ended(
    std::int32_t score_winner_team) noexcept {
    // LoadingMenu.start_pressed in the preserved client routes both
    // state_data.has_map_ended and game_statistics_active through
    // show_game_statistics(True).  Packet 52 freezes that scene; it does not
    // erase the GameStats/RankUps/ShowTextMessage data already attached to it.
    if (!visible_) {
        show(score_winner_team);
    }
}

void apply_map_ended_overlay_boundary(
    MatchResultsModel& results, GameChatModel& chat,
    GenericVotingModel& vote, std::int32_t score_winner_team) noexcept {
    results.on_map_ended(score_winner_team);
    chat.cancel();
    // GenericVotingHUD belongs to the frozen GameScene. Clearing it here
    // erased an active/just-closed map ballot before the terminal frame could
    // render it. Keep the explicit parameter so this non-action remains a
    // reviewed protocol invariant instead of an accidental omission.
    static_cast<void>(vote);
}

void MatchResultsModel::show(std::int32_t score_winner_team) noexcept {
    winner_team_ = score_winner_team == 2 || score_winner_team == 3
                       ? score_winner_team
                       : 0;
    if (winner_team_ == 0) {
        // Compatibility for the older revival server's one mixed award packet,
        // which used packet.team_id as its winner. Never use this fallback
        // after both retail team lists have been observed.
        if (observed_stats_teams_ == 0x01U) winner_team_ = 2;
        if (observed_stats_teams_ == 0x02U) winner_team_ = 3;
    }
    visible_ = true;
    unshown_score_reset_mask_ = 0U;
}

void MatchResultsModel::apply(const network::RankUpsPacket& packet) {
    // Retail appends every RankUps(66) entry and later consumes list.pop().
    // Clearing here loses an earlier packet when the backend splits a batch.
    rank_ups_.reserve(rank_ups_.size() + packet.entries.size());
    for (const auto& entry : packet.entries) {
        rank_ups_.push_back({entry.score_reason, entry.old_score, entry.new_score});
    }
    display_rank_ups_ = true;
}

void MatchResultsModel::apply(
    const network::ShowTextMessagePacket& packet) noexcept {
    // Retail receives duration too, but the optimized GameScene implementation
    // calls set_message(message_id) only. Retaining the selector until
    // MapEnded mirrors both ViewScores and ViewGameStats without a made-up
    // native timeout.
    message_id_ = packet.message_id;
}

void MatchResultsModel::tick(double elapsed_seconds) noexcept {
    if (!visible_ || !display_rank_ups_ || !std::isfinite(elapsed_seconds) ||
        elapsed_seconds <= 0.0) {
        return;
    }

    // ViewGameStats.update checks the old timer before adding dt. Starting at
    // 4.25 - 5.5 = -1.25 therefore yields the exact 5.5-second opening hold,
    // with activation on the following update rather than in the same frame.
    if (rank_up_timer_seconds_ < rank_up_total_seconds) {
        rank_up_timer_seconds_ += elapsed_seconds;
        if (level_up_) {
            level_up_timer_seconds_ += elapsed_seconds;
            // Retail compares `level_up_timer > A1077 + A1078` and writes
            // self.level_up = False. A1077 is zero and A1078 is 0.4 seconds.
            if (level_up_timer_seconds_ > rank_level_scale_up_seconds +
                                              rank_level_scale_down_seconds) {
                level_up_ = false;
            }
        }

        const auto frame = rank_up_frame();
        if (frame.has_value() && frame->level > displayed_rank_level_) {
            displayed_rank_level_ = frame->level;
            level_up_timer_seconds_ = 0.0;
            level_up_ = true;
        }
        return;
    }

    if (rank_ups_.empty()) return;
    rank_up_timer_seconds_ = 0.0;
    current_rank_up_ = rank_ups_.back();
    rank_ups_.pop_back();
    level_up_ = false;
    level_up_timer_seconds_ = 0.0;
    last_rank_up_ = rank_ups_.empty();

    displayed_rank_level_ = 1U;
    if (current_rank_up_->score_reason >= 0) {
        const auto old_level = retail_progression_for_stat(
            static_cast<std::uint32_t>(current_rank_up_->score_reason),
            static_cast<double>(current_rank_up_->old_score));
        if (old_level.has_value()) displayed_rank_level_ = old_level->level;
    }
}

std::optional<MatchRankUpFrame>
MatchResultsModel::rank_up_frame() const noexcept {
    if (!display_rank_ups_ || !current_rank_up_.has_value() ||
        current_rank_up_->score_reason < 0 || rank_up_timer_seconds_ < 0.0 ||
        rank_up_timer_seconds_ >= rank_up_total_seconds) {
        return std::nullopt;
    }

    const auto interpolation_timer = std::clamp(
        rank_up_timer_seconds_, rank_up_before_delay_seconds,
        rank_up_before_delay_seconds + rank_up_interpolation_seconds);
    const auto progress =
        (interpolation_timer - rank_up_before_delay_seconds) /
        rank_up_interpolation_seconds;
    const auto old_score = static_cast<double>(current_rank_up_->old_score);
    const auto new_score = static_cast<double>(current_rank_up_->new_score);
    const auto score = old_score + (new_score - old_score) * progress;
    const auto progression = retail_progression_for_stat(
        static_cast<std::uint32_t>(current_rank_up_->score_reason), score);
    if (!progression.has_value()) return std::nullopt;

    double opacity{1.0};
    if (rank_up_timer_seconds_ < rank_up_fade_seconds) {
        opacity = rank_up_timer_seconds_ / rank_up_fade_seconds;
    } else if (rank_up_timer_seconds_ >=
               rank_up_total_seconds - rank_up_fade_seconds) {
        opacity = 1.0 -
                  (rank_up_timer_seconds_ -
                   (rank_up_total_seconds - rank_up_fade_seconds)) /
                      rank_up_fade_seconds;
    }
    return MatchRankUpFrame{
        *current_rank_up_, score, progression->next_level_min,
        progression->next_level_max, progression->level,
        std::clamp(opacity, 0.0, 1.0), level_up_timer_seconds_, level_up_,
        last_rank_up_};
}

std::optional<std::string> retail_level_screenshot_asset(
    std::string map_name, std::size_t camera_index) {
    // InitialInfo is inconsistent across official hosts: some publish the VXL
    // stem (MayanJungle), while retail's screenshot table always uses the
    // authored display spelling (Mayan Jungle). Resolve only known stock maps;
    // unknown UGC names must never borrow another map's result art.
    if (const auto official = world::find_official_map_environment(map_name);
        official.has_value()) {
        map_name = world::map_display_name(official->stem);
    }
    // The retail VXL is named 20thCenturyTown, but its authored end-screen
    // camera set is the WW1 family (WW10.png ... WW13.png). This is the one
    // stock filename that cannot be recovered by CamelCase word splitting.
    // Accept both display and stem spellings because public Protocol 168
    // servers have shipped each form in InitialInfo.
    std::string compact_identity;
    compact_identity.reserve(map_name.size());
    for (const auto character : map_name) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            compact_identity.push_back(
                static_cast<char>(std::tolower(byte)));
        }
    }
    if (compact_identity == "20thcenturytown") map_name = "WW1";
    // The retail asset catalog spells only this map with a lower-case `of`,
    // while protocol/map metadata historically used both variants.
    if (map_name == "City Of Chicago") map_name = "City of Chicago";
    if (map_name.empty() || map_name.size() > 80U ||
        !std::ranges::all_of(map_name, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == ' ' ||
                   character == '-' || character == '_' || character == '\'';
        })) {
        return std::nullopt;
    }
    return "png/ui/level_screenshots/" + map_name +
           std::to_string(camera_index) + ".png";
}

std::optional<MatchResultCameraPose> resolve_match_result_camera(
    std::span<const std::array<double, 3U>> points,
    std::span<const std::array<double, 3U>> rotations,
    std::size_t camera_index, double pan_elapsed_seconds) noexcept {
    if (camera_index >= points.size() || camera_index >= rotations.size() ||
        !std::isfinite(pan_elapsed_seconds)) {
        return std::nullopt;
    }

    const auto& point = points[camera_index];
    const auto& rotation = rotations[camera_index];
    const auto finite_vector = [](const std::array<double, 3U>& value) {
        return std::ranges::all_of(value, [](double component) {
            return std::isfinite(component);
        });
    };
    if (!finite_vector(point) || !finite_vector(rotation)) {
        return std::nullopt;
    }
    if (std::ranges::all_of(point, [](double component) {
            return component == 0.0;
        }) &&
        std::ranges::all_of(rotation, [](double component) {
            return component == 0.0;
        })) {
        return std::nullopt;
    }

    constexpr double pan_seconds{5.0};
    constexpr double pan_distance{-5.0};
    constexpr double degrees_to_radians{std::numbers::pi / 180.0};
    const auto pitch = rotation[0U] * degrees_to_radians;
    const auto yaw = rotation[1U] * degrees_to_radians;
    // shared.common.pitch_yaw_to_direction_vector uses the retail VXL basis.
    const std::array direction{
        std::sin(yaw) * std::cos(pitch),
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
    };
    const auto progress =
        std::clamp(pan_elapsed_seconds / pan_seconds, 0.0, 1.0);

    MatchResultCameraPose pose;
    for (std::size_t axis{}; axis < pose.eye.size(); ++axis) {
        pose.eye[axis] =
            point[axis] + direction[axis] * pan_distance * progress;
    }
    const auto wrap_degrees = [](double angle) {
        auto wrapped = std::fmod(angle + 180.0, 360.0);
        if (wrapped < 0.0) wrapped += 360.0;
        return wrapped - 180.0;
    };
    // The native renderer's yaw-zero axis is retail yaw -90 with opposite
    // winding. This is the same transform used by its orientation vector.
    pose.yaw_degrees = wrap_degrees(-rotation[1U] - 90.0);
    pose.pitch_degrees = wrap_degrees(rotation[0U]);
    pose.roll_degrees = wrap_degrees(rotation[2U]);
    return pose;
}

void MatchResultsModel::clear() noexcept {
    awards_.clear();
    rank_ups_.clear();
    current_rank_up_.reset();
    rank_up_timer_seconds_ =
        rank_up_total_seconds - rank_up_initial_delay_seconds;
    level_up_timer_seconds_ = 0.0;
    displayed_rank_level_ = 1U;
    message_id_.reset();
    winner_team_ = 0;
    observed_stats_teams_ = 0U;
    display_rank_ups_ = false;
    level_up_ = false;
    last_rank_up_ = false;
    visible_ = false;
    unshown_score_reset_mask_ = 0U;
}

ui::DrawList MatchResultsPresentation::build(
    const MatchResultsModel& model, const ChangeTeamServerState& state,
    std::string mode_title, const network::Protocol168Roster& roster,
    ui::PixelExtent window, MatchScreenshotPreview preview) const {
    // The raw-pixel preview has fixed top-left coordinates after converting
    // retail's window.height-relative bottom-left expression.
    static_cast<void>(window);
    ui::DrawList list;
    if (!model.visible()) return list;
    list.reserve(32U);

    // ViewGameStats.draw_hud lines 267..275: GameScene draws a separately
    // framed 276x212 authored camera image at the upper-left whenever the
    // StateData camera arrays exist. Pyglet truncates all .64-scaled sizes.
    if (preview.visible) {
        if (auto screenshot = retail_level_screenshot_asset(
                std::move(preview.map_name), preview.camera_index);
            screenshot.has_value()) {
            // draw_hud runs under GameScene's raw window-pixel projection.
            // The bottom-left retail expression below therefore keeps the
            // authored frame 10 physical pixels from the top at every
            // resolution; it is not part of ViewGameStats' 800x600 content
            // canvas.
            // screenshot_frame.blit(10, window.height - frame.height - 10)
            list.push(sprite("png/ui/in_game_menus/screenshot_frame.png",
                             {10.0, 10.0, 197.0, 158.0},
                             ui::DrawSpace::window_pixels, {}, 1'000U,
                             retail_global_scale));
            // level_cameras_screenshots[current].blit(20, frame_y + 11)
            list.push(sprite(std::move(*screenshot),
                             {20.0, 22.0, 176.0, 135.0},
                             ui::DrawSpace::window_pixels, {}, 1'000U,
                             retail_global_scale));
        }
    }

    // ViewGameStats.draw is composited over the live GameScene. Retail does
    // not add the fabricated full-screen black veil that used to live here.
    std::string result_title = retail_match_result_message(
        state, model.message_id(), model.winner_team());
    // The 1098x344 frame is loaded at int(source * 0.64), centre anchored,
    // then blitted at retail bottom-left point (400, 110).
    constexpr double frame_x{49.0};
    constexpr double frame_y{380.0};
    constexpr double frame_width{702.0};
    constexpr double frame_height{220.0};
    list.push(sprite(
        "png/ui/in_game_menus/view_game_stats_content_framesy.png",
        {frame_x, frame_y, frame_width, frame_height},
        ui::DrawSpace::design_pixels, {}, 1'000U, retail_global_scale,
        ui::TextureAnchor::center));

    // Retail sends one packet-67 award list per team.  BattleSpades' older
    // compatibility stream instead sends one packet containing both rosters
    // (and uses team 0 for a draw).  Preserve the packet-owned columns when
    // both retail lists were observed, but split a demonstrably mixed single
    // list through the already-authoritative CreatePlayer roster.  This keeps
    // official traffic exact while making the maintained server's end screen
    // complete without changing its wire protocol.
    std::uint8_t packet_team_mask{};
    std::uint8_t roster_team_mask{};
    for (const auto& award : model.awards()) {
        if (award.team_id == 2U || award.team_id == 3U) {
            packet_team_mask |= static_cast<std::uint8_t>(
                1U << static_cast<unsigned>(award.team_id - 2U));
        }
        if (const auto* player = roster.player(award.player_id);
            player != nullptr && (player->team == 2U || player->team == 3U)) {
            roster_team_mask |= static_cast<std::uint8_t>(
                1U << static_cast<unsigned>(player->team - 2U));
        }
    }
    const bool mixed_single_packet =
        (packet_team_mask == 0U || packet_team_mask == 0x01U ||
         packet_team_mask == 0x02U) &&
        roster_team_mask == 0x03U;

    std::array<std::vector<MatchAward>, 2U> team_awards;
    for (const auto& award : model.awards()) {
        auto column_team = award.team_id;
        const auto* player = roster.player(award.player_id);
        if ((mixed_single_packet || (column_team != 2U && column_team != 3U)) &&
            player != nullptr && (player->team == 2U || player->team == 3U)) {
            column_team = player->team;
        }
        if (column_team != 2U && column_team != 3U) continue;
        auto& destination = team_awards[column_team - 2U];
        if (destination.size() < 3U) destination.push_back(award);
    }
    const auto append_team = [&](bool second_team,
                                 std::span<const MatchAward> awards) {
        // draw_game_stats(...): left (78,121,314,48,285), right
        // (410,121,314,48,410). All values below are their top-left conversion.
        constexpr double width{314.0};
        const double x = second_team ? 410.0 : 78.0;
        const auto tint = second_team ? retail_team2_ui : retail_team1_ui;
        const auto& name = second_team ? state.team2_name : state.team1_name;
        const auto score = second_team ? state.team2_score : state.team1_score;
        const bool show_score = second_team ? state.team2_show_score
                                            : state.team1_show_score;
        const bool show_max_score = second_team
                                        ? state.team2_show_max_score
                                        : state.team1_show_max_score;

        const auto suffix = second_team ? "1" : "2";
        const double head_x = second_team ? 678.0 : 78.0;
        list.push(sprite("png/ui/icons/deuce_head_" + std::string{suffix} +
                             ".png",
                         {head_x, 445.0, 46.0, 44.0},
                         ui::DrawSpace::design_pixels, {}, 1'000U,
                         retail_global_scale, ui::TextureAnchor::center));
        list.push(sprite("png/ui/icons/deuce_head_colour_" +
                             std::string{suffix} + ".png",
                         {head_x, 445.0, 46.0, 44.0},
                         ui::DrawSpace::design_pixels, tint, 1'000U,
                         retail_global_scale, ui::TextureAnchor::center));

        // A hidden score gives the team name the extra 25px used by retail.
        list.push(text(name, {x, 455.0, width + (show_score ? 0.0 : 25.0), 30.0},
                       32.0, ui::DrawSpace::design_pixels,
                       ui::HorizontalTextAlignment::center, tint,
                       "fonts/Spades.ttf", ui::TextTransform::uppercase,
                       ui::TextFit::retail_width_scale,
                       ui::VerticalTextAlignment::retail_center));
        if (show_score) {
            auto score_text = std::to_string(score);
            if (show_max_score) {
                score_text += "/" + std::to_string(state.score_limit);
            }
            list.push(text(std::move(score_text),
                           {second_team ? 410.0 : 285.0, 455.0, 100.0, 30.0},
                           32.0, ui::DrawSpace::design_pixels,
                            second_team ? ui::HorizontalTextAlignment::left
                                        : ui::HorizontalTextAlignment::right,
                            tint, "fonts/Spades.ttf",
                            ui::TextTransform::preserve,
                            ui::TextFit::retail_width_scale,
                            ui::VerticalTextAlignment::retail_center));
        }

        for (std::size_t index{}; index < std::min<std::size_t>(awards.size(), 3U);
             ++index) {
            const auto* player = roster.player(awards[index].player_id);
            if (player == nullptr) continue;
            const auto player_tint = player->team == 3U ? retail_team2_ui
                                                         : retail_team1_ui;
            const auto background = retail_blend_color(
                index % 2U == 0U ? retail_list_light : retail_list_dark,
                player_tint, retail_list_blend);
            const double row_y = 494.0 + static_cast<double>(index) * 16.0;
            const double text_baseline =
                506.0 + static_cast<double>(index) * 16.0;
            list.push(sprite("png/high/white.png", {x, row_y, width, 16.0},
                             ui::DrawSpace::design_pixels, background, 1'000U,
                             1.0));
            list.push(text(player->name,
                           {x + 13.14, text_baseline, 86.06, 0.0}, 11.0,
                           ui::DrawSpace::design_pixels,
                           ui::HorizontalTextAlignment::left, player_tint,
                           "fonts/A750-Sans-Medium.ttf",
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale,
                           ui::VerticalTextAlignment::baseline));
            list.push(text(std::string{retail_game_stat_award_label(
                               awards[index].stat_type)},
                           {x + 104.2, text_baseline, 204.8, 0.0}, 11.0,
                           ui::DrawSpace::design_pixels,
                           ui::HorizontalTextAlignment::left, player_tint,
                           "fonts/A750-Sans-Medium.ttf",
                           ui::TextTransform::preserve,
                           ui::TextFit::retail_width_scale,
                           ui::VerticalTextAlignment::baseline));
        }
    };
    append_team(false, team_awards[0U]);
    append_team(true, team_awards[1U]);

    // ViewGameStats.draw paints the authored frame and both team columns
    // before these labels. Drawing the frame after the labels hid them below
    // its opaque centre even though every individual rectangle was correct.
    // These are direct Font.draw calls, not the rectangle-fitting helpers used
    // by the team headings below.  Retail's coordinates are bottom-left
    // baselines; converting them to our top-left canvas gives y=435 and y=420.
    // A zero-width centered destination reproduces Font.draw(center=True)
    // without introducing a fabricated fit box that can shrink the glyphs.
    // score_text_font.draw(text_box_text.upper(), 400, 165, ..., center=True)
    list.push(text(std::move(result_title), {400.0, 435.0, 0.0, 0.0}, 14.0,
                   ui::DrawSpace::design_pixels,
                   ui::HorizontalTextAlignment::center, menu_gold,
                   "fonts/Spades.ttf", ui::TextTransform::uppercase,
                   ui::TextFit::none, ui::VerticalTextAlignment::baseline));
    // title_font.draw(mode_text, 400, 180, ..., center=True)
    list.push(text(std::move(mode_title), {400.0, 420.0, 0.0, 0.0}, 46.0,
                   ui::DrawSpace::design_pixels,
                   ui::HorizontalTextAlignment::center, menu_gold,
                   "fonts/Spades.ttf", ui::TextTransform::uppercase,
                   ui::TextFit::none, ui::VerticalTextAlignment::baseline));

    const auto rank_up = model.rank_up_frame();
    if (!rank_up.has_value()) {
        list.push(text("Press TAB to show scores",
                       {300.0, 557.0, 200.0, 18.0}, 18.0,
                       ui::DrawSpace::design_pixels,
                       ui::HorizontalTextAlignment::center,
                       menu_gold, "fonts/Spades.ttf",
                       ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale,
                       ui::VerticalTextAlignment::retail_center));
        return list;
    }

    // draw_rank_ups calls draw_progress_bar(74,23,653,18,...) on retail's
    // bottom-left 800x600 canvas. The authored stroke is loaded at int(.64*x)
    // and blitted at (71,20); DrawList stores the converted top-left bounds.
    const auto opacity = static_cast<std::uint16_t>(std::clamp(
        std::lround(rank_up->opacity * 1'000.0), 0L, 1'000L));
    constexpr ui::ColorRgba8 rank_text_color{104U, 173U, 87U, 255U};
    constexpr ui::ColorRgba8 rank_flash_color{192U, 255U, 132U, 255U};
    constexpr ui::DrawRect bar_bounds{74.0, 559.0, 653.0, 18.0};
    list.push(sprite("png/high/white.png", bar_bounds,
                     ui::DrawSpace::design_pixels,
                     {8U, 10U, 5U, 255U}, opacity, 1.0));
    const auto denominator =
        rank_up->next_level_max - rank_up->next_level_min;
    const auto fill_ratio = denominator > 0.0
                                ? std::clamp((rank_up->interpolated_score -
                                              rank_up->next_level_min) /
                                                 denominator,
                                             0.0, 1.0)
                                : 0.0;
    if (fill_ratio > 0.0) {
        list.push(sprite("png/high/white.png",
                         {bar_bounds.x, bar_bounds.y,
                          bar_bounds.width * fill_ratio, bar_bounds.height},
                         ui::DrawSpace::design_pixels,
                         rank_up->level_up ? rank_flash_color
                                           : rank_text_color,
                         opacity, 1.0));
    }
    list.push(sprite(
        "png/ui/in_game_menus/view_game_stats_rankup_bar_stroke.png",
        {71.0, 556.0, 658.0, 24.0}, ui::DrawSpace::design_pixels, {},
        opacity, retail_global_scale, ui::TextureAnchor::top_left));

    auto score_reason = text(
        retail_score_reason_label(
            static_cast<std::uint32_t>(rank_up->rank_up.score_reason)),
        {300.0, 557.0, 200.0, 18.0}, 14.0,
        ui::DrawSpace::design_pixels,
        ui::HorizontalTextAlignment::center, rank_text_color,
        "fonts/Spades.ttf", ui::TextTransform::preserve,
        ui::TextFit::retail_width_scale,
        ui::VerticalTextAlignment::retail_center);
    score_reason.modulation.opacity_per_mille = opacity;
    list.push(std::move(score_reason));

    const auto shown_score =
        static_cast<std::int64_t>(rank_up->interpolated_score);
    const auto next_level =
        static_cast<std::int64_t>(rank_up->next_level_max);
    auto score = text(std::to_string(shown_score) + " / " +
                          std::to_string(next_level),
                      {81.0, 557.0, 98.0, 18.0}, 14.0,
                       ui::DrawSpace::design_pixels,
                       ui::HorizontalTextAlignment::left, rank_text_color,
                       "fonts/Spades.ttf", ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale,
                       ui::VerticalTextAlignment::retail_center);
    score.modulation.opacity_per_mille = opacity;
    list.push(std::move(score));

    // Retail builds this label from strings.LEVEL (English: "Level") and
    // right-aligns it inside draw_progress_bar's 150 px label box.
    auto level = text("Level " + std::to_string(rank_up->level),
                      {567.0, 553.0, 150.0, 20.0}, 14.0,
                       ui::DrawSpace::design_pixels,
                       ui::HorizontalTextAlignment::right, rank_text_color,
                       "fonts/Spades.ttf", ui::TextTransform::preserve,
                       ui::TextFit::retail_width_scale,
                       ui::VerticalTextAlignment::retail_center);
    level.modulation.opacity_per_mille = opacity;
    list.push(std::move(level));

    if (rank_up->level_up) {
        // Retail uses independent 0.3-second up/down fades for the authored
        // content-stroke glow.
        constexpr double glow_fade_seconds{0.3};
        const auto glow_unit = rank_up->level_up_timer < glow_fade_seconds
                                   ? rank_up->level_up_timer /
                                         glow_fade_seconds
                                   : 1.0 -
                                         (rank_up->level_up_timer -
                                          glow_fade_seconds) /
                                             glow_fade_seconds;
        const auto glow_opacity = static_cast<std::uint16_t>(std::clamp(
            std::lround(std::clamp(glow_unit, 0.0, 1.0) *
                        rank_up->opacity * 1'000.0),
            0L, 1'000L));
        if (glow_opacity > 0U) {
            list.push(sprite(
                "png/ui/in_game_menus/view_game_stats_content_stroke_glow.png",
                {68.0, 553.0, 664.0, 30.0},
                ui::DrawSpace::design_pixels, rank_flash_color, glow_opacity,
                retail_global_scale, ui::TextureAnchor::top_left));
        }

        // Windows hud.pyd and macOS hud.so both set the cached 72px Spades
        // font's geometric scale, then call draw(str(rank_up.level), 714, 26,
        // RANKUP_TEXT_COLOUR, center=False). Font._draw translates before it
        // scales, so the glyph grows about this exact bottom-left baseline.
        auto level_up_number = text(
            std::to_string(rank_up->level), {714.0, 574.0, 0.0, 0.0}, 72.0,
            ui::DrawSpace::design_pixels,
            ui::HorizontalTextAlignment::left, rank_text_color,
            "fonts/Spades.ttf", ui::TextTransform::preserve,
            ui::TextFit::none, ui::VerticalTextAlignment::baseline);
        level_up_number.geometric_scale =
            rank_level_geometric_scale(rank_up->level_up_timer);
        list.push(std::move(level_up_number));
    }
    return list;
}

} // namespace battlespades::frontend
