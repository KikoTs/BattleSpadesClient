#include "battlespades/settings/client_settings.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace battlespades::settings {
namespace {

constexpr std::uint32_t minimum_resolution_width{320U};
constexpr std::uint32_t minimum_resolution_height{240U};
constexpr std::uint32_t maximum_resolution_dimension{16'384U};
constexpr std::uint32_t maximum_keyboard_scancode{511U};
constexpr std::uint32_t maximum_mouse_button{32U};

constexpr std::uint32_t scancode_a{4U};
constexpr std::uint32_t scancode_c{6U};
constexpr std::uint32_t scancode_d{7U};
constexpr std::uint32_t scancode_e{8U};
constexpr std::uint32_t scancode_h{11U};
constexpr std::uint32_t scancode_k{14U};
constexpr std::uint32_t scancode_m{16U};
constexpr std::uint32_t scancode_q{20U};
constexpr std::uint32_t scancode_r{21U};
constexpr std::uint32_t scancode_s{22U};
constexpr std::uint32_t scancode_t{23U};
constexpr std::uint32_t scancode_v{25U};
constexpr std::uint32_t scancode_w{26U};
constexpr std::uint32_t scancode_x{27U};
constexpr std::uint32_t scancode_y{28U};
constexpr std::uint32_t scancode_z{29U};
constexpr std::uint32_t scancode_1{30U};
constexpr std::uint32_t scancode_0{39U};
constexpr std::uint32_t scancode_escape{41U};
constexpr std::uint32_t scancode_tab{43U};
constexpr std::uint32_t scancode_space{44U};
constexpr std::uint32_t scancode_comma{54U};
constexpr std::uint32_t scancode_period{55U};
constexpr std::uint32_t scancode_f1{58U};
constexpr std::uint32_t scancode_f2{59U};
constexpr std::uint32_t scancode_f3{60U};
constexpr std::uint32_t scancode_f10{67U};
constexpr std::uint32_t scancode_right{79U};
constexpr std::uint32_t scancode_left{80U};
constexpr std::uint32_t scancode_down{81U};
constexpr std::uint32_t scancode_up{82U};
constexpr std::uint32_t scancode_left_control{224U};
constexpr std::uint32_t scancode_left_shift{225U};
constexpr std::uint32_t mouse_button_right{3U};

constexpr std::array<std::string_view, control_action_count> action_names{
    "forward",      "backward",      "left",          "right",        "sneak",
    "crouch",       "sprint",        "jump",          "aim",          "reload",
    "team_chat",    "global_chat",   "show_map",      "view_scores",  "change_team",
    "change_class", "menu",          "weapon_custom", "map_vote_1",   "map_vote_2",
    "map_vote_3",   "kick_player",   "toggle_hud",    "ugc_settings", "tool_help",
    "palette_left", "palette_right", "palette_up",    "palette_down", "cancel_prefab_placement",
    "carve_prefab", "hover",         "quick_save",
};

struct NamedCode final {
    std::string_view name;
    std::uint32_t code;
};

constexpr std::array<NamedCode, 22U> named_keyboard_codes{{
    {"escape", scancode_escape},
    {"tab", scancode_tab},
    {"space", scancode_space},
    {"comma", scancode_comma},
    {"period", scancode_period},
    {"f1", scancode_f1},
    {"f2", scancode_f2},
    {"f3", scancode_f3},
    {"f10", scancode_f10},
    {"right", scancode_right},
    {"left", scancode_left},
    {"down", scancode_down},
    {"up", scancode_up},
    {"left_ctrl", scancode_left_control},
    {"left_shift", scancode_left_shift},
    {"a", scancode_a},
    {"c", scancode_c},
    {"d", scancode_d},
    {"e", scancode_e},
    {"h", scancode_h},
    {"k", scancode_k},
    {"m", scancode_m},
}};

constexpr std::array<NamedCode, 12U> remaining_keyboard_codes{{
    {"q", scancode_q},
    {"r", scancode_r},
    {"s", scancode_s},
    {"t", scancode_t},
    {"v", scancode_v},
    {"w", scancode_w},
    {"x", scancode_x},
    {"y", scancode_y},
    {"z", scancode_z},
    {"1", scancode_1},
    {"0", scancode_0},
    {"backquote", 53U},
}};

[[nodiscard]] constexpr std::size_t action_index(ControlAction action) noexcept {
    return static_cast<std::size_t>(action);
}

[[nodiscard]] constexpr bool valid_action(ControlAction action) noexcept {
    return action_index(action) < control_action_count;
}

[[nodiscard]] constexpr bool valid_quality(QualityLevel value) noexcept {
    switch (value) {
    case QualityLevel::low:
    case QualityLevel::medium:
    case QualityLevel::high:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid_draw_distance(DrawDistance value) noexcept {
    switch (value) {
    case DrawDistance::low:
    case DrawDistance::medium:
    case DrawDistance::high:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid_antialiasing(Antialiasing value) noexcept {
    switch (value) {
    case Antialiasing::off:
    case Antialiasing::samples_2:
    case Antialiasing::samples_4:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid_graphics_api(GraphicsApi value) noexcept {
    switch (value) {
    case GraphicsApi::automatic:
    case GraphicsApi::direct3d11:
    case GraphicsApi::direct3d12:
    case GraphicsApi::vulkan:
    case GraphicsApi::opengl:
    case GraphicsApi::metal:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool valid_shader_quality(ShaderQuality value) noexcept {
    switch (value) {
    case ShaderQuality::compatibility:
    case ShaderQuality::low:
    case ShaderQuality::medium:
    case ShaderQuality::high:
    case ShaderQuality::ultra:
        return true;
    }
    return false;
}

[[nodiscard]] double normalized_unit_value(double value, double fallback) noexcept {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] bool valid_language(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 32U &&
           std::ranges::all_of(value, [](unsigned char character) {
               return std::isalnum(character) != 0 || character == '-';
           });
}

[[nodiscard]] bool valid_audio_device(std::string_view value) noexcept {
    return value.size() <= 512U && std::ranges::none_of(value,
        [](unsigned char byte) { return byte < 0x20U || byte == 0x7FU; });
}

[[nodiscard]] Resolution normalized_resolution(Resolution value) noexcept {
    value.width = std::clamp(value.width, minimum_resolution_width, maximum_resolution_dimension);
    value.height =
        std::clamp(value.height, minimum_resolution_height, maximum_resolution_dimension);
    return value;
}

template <std::size_t Size>
[[nodiscard]] constexpr std::optional<std::uint32_t>
code_for_name(const std::array<NamedCode, Size>& codes, std::string_view name) noexcept {
    for (const auto& entry : codes) {
        if (entry.name == name) {
            return entry.code;
        }
    }
    return std::nullopt;
}

template <std::size_t Size>
[[nodiscard]] constexpr std::optional<std::string_view>
name_for_code(const std::array<NamedCode, Size>& codes, std::uint32_t code) noexcept {
    for (const auto& entry : codes) {
        if (entry.code == code) {
            return entry.name;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string ascii_lower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (character >= 'A' && character <= 'Z') {
            result.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::uint32_t> parse_unsigned(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint32_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<std::uint32_t> keyboard_code_from_name(std::string_view name) noexcept {
    if (const auto known = code_for_name(named_keyboard_codes, name); known.has_value()) {
        return known;
    }
    if (const auto known = code_for_name(remaining_keyboard_codes, name); known.has_value()) {
        return known;
    }
    if (name.size() == 1U && name.front() >= '2' && name.front() <= '9') {
        return scancode_1 + static_cast<std::uint32_t>(name.front() - '1');
    }
    constexpr std::string_view prefix{"scancode-"};
    if (name.starts_with(prefix)) {
        return parse_unsigned(name.substr(prefix.size()));
    }
    return std::nullopt;
}

[[nodiscard]] std::string keyboard_name(std::uint32_t code) {
    if (const auto known = name_for_code(named_keyboard_codes, code); known.has_value()) {
        return std::string{*known};
    }
    if (const auto known = name_for_code(remaining_keyboard_codes, code); known.has_value()) {
        return std::string{*known};
    }
    if (code >= scancode_1 && code < scancode_0) {
        return std::string(1U, static_cast<char>('1' + (code - scancode_1)));
    }
    return "scancode-" + std::to_string(code);
}

[[nodiscard]] bool binding_in_use(const ControlsSettings& settings,
                                  InputBinding binding,
                                  std::size_t before_index) noexcept {
    if (binding.is_unbound()) {
        return false;
    }
    for (std::size_t index{}; index < before_index; ++index) {
        if (settings.bindings[index] == binding) {
            return true;
        }
    }
    return false;
}

} // namespace

InputBinding ControlsSettings::binding(ControlAction action) const noexcept {
    if (!valid_action(action)) {
        return InputBinding::unbound();
    }
    return bindings[action_index(action)];
}

bool ControlsSettings::set_binding(ControlAction action, InputBinding value) noexcept {
    if (!valid_action(action)) {
        return false;
    }
    bindings[action_index(action)] = value;
    return true;
}

ClientSettings retail_default_settings() noexcept {
    ClientSettings settings{};
    auto& bindings = settings.controls.bindings;
    bindings[action_index(ControlAction::forward)] = InputBinding::keyboard(scancode_w);
    bindings[action_index(ControlAction::backward)] = InputBinding::keyboard(scancode_s);
    bindings[action_index(ControlAction::left)] = InputBinding::keyboard(scancode_a);
    bindings[action_index(ControlAction::right)] = InputBinding::keyboard(scancode_d);
    bindings[action_index(ControlAction::sneak)] = InputBinding::keyboard(scancode_v);
    bindings[action_index(ControlAction::crouch)] = InputBinding::keyboard(scancode_left_control);
    bindings[action_index(ControlAction::sprint)] = InputBinding::keyboard(scancode_left_shift);
    bindings[action_index(ControlAction::jump)] = InputBinding::keyboard(scancode_space);
    bindings[action_index(ControlAction::aim)] = InputBinding::mouse(mouse_button_right);
    bindings[action_index(ControlAction::reload)] = InputBinding::keyboard(scancode_r);
    bindings[action_index(ControlAction::team_chat)] = InputBinding::keyboard(scancode_y);
    bindings[action_index(ControlAction::global_chat)] = InputBinding::keyboard(scancode_t);
    bindings[action_index(ControlAction::show_map)] = InputBinding::keyboard(scancode_m);
    bindings[action_index(ControlAction::view_scores)] = InputBinding::keyboard(scancode_tab);
    bindings[action_index(ControlAction::change_team)] = InputBinding::keyboard(scancode_period);
    bindings[action_index(ControlAction::change_class)] = InputBinding::keyboard(scancode_comma);
    bindings[action_index(ControlAction::menu)] = InputBinding::keyboard(scancode_escape);
    bindings[action_index(ControlAction::weapon_custom)] = InputBinding::keyboard(scancode_e);
    bindings[action_index(ControlAction::map_vote_1)] = InputBinding::keyboard(scancode_f1);
    bindings[action_index(ControlAction::map_vote_2)] = InputBinding::keyboard(scancode_f2);
    bindings[action_index(ControlAction::map_vote_3)] = InputBinding::keyboard(scancode_f3);
    bindings[action_index(ControlAction::kick_player)] = InputBinding::keyboard(scancode_k);
    bindings[action_index(ControlAction::toggle_hud)] = InputBinding::unbound();
    bindings[action_index(ControlAction::ugc_settings)] = InputBinding::keyboard(scancode_x);
    bindings[action_index(ControlAction::tool_help)] = InputBinding::keyboard(scancode_h);
    bindings[action_index(ControlAction::palette_left)] = InputBinding::keyboard(scancode_left);
    bindings[action_index(ControlAction::palette_right)] = InputBinding::keyboard(scancode_right);
    bindings[action_index(ControlAction::palette_up)] = InputBinding::keyboard(scancode_up);
    bindings[action_index(ControlAction::palette_down)] = InputBinding::keyboard(scancode_down);
    bindings[action_index(ControlAction::cancel_prefab_placement)] =
        InputBinding::keyboard(scancode_q);
    bindings[action_index(ControlAction::carve_prefab)] = InputBinding::keyboard(scancode_c);
    bindings[action_index(ControlAction::hover)] = InputBinding::keyboard(scancode_z);
    bindings[action_index(ControlAction::quick_save)] = InputBinding::keyboard(scancode_f10);
    return settings;
}

ClientSettings normalize_settings(const ClientSettings& source) noexcept {
    const auto defaults = retail_default_settings();
    auto result = source;
    result.schema_version = current_settings_schema_version;

    result.main.master_volume =
        normalized_unit_value(source.main.master_volume, defaults.main.master_volume);
    result.main.music_volume =
        normalized_unit_value(source.main.music_volume, defaults.main.music_volume);
    result.main.language = valid_language(source.main.language) ? source.main.language
                                                                : defaults.main.language;
    result.main.audio_device = valid_audio_device(source.main.audio_device)
                                   ? source.main.audio_device : std::string{};
    result.graphics.resolution = normalized_resolution(source.graphics.resolution);
    result.graphics.graphics_api = valid_graphics_api(source.graphics.graphics_api)
                                       ? source.graphics.graphics_api
                                       : defaults.graphics.graphics_api;
    result.graphics.antialiasing = valid_antialiasing(source.graphics.antialiasing)
                                       ? source.graphics.antialiasing
                                       : defaults.graphics.antialiasing;
    result.graphics.effect_quality = valid_quality(source.graphics.effect_quality)
                                         ? source.graphics.effect_quality
                                         : defaults.graphics.effect_quality;
    result.graphics.draw_distance = valid_draw_distance(source.graphics.draw_distance)
                                        ? source.graphics.draw_distance
                                        : defaults.graphics.draw_distance;
    result.graphics.shader_quality = valid_shader_quality(source.graphics.shader_quality)
                                         ? source.graphics.shader_quality
                                         : defaults.graphics.shader_quality;
    result.graphics.texture_quality = valid_quality(source.graphics.texture_quality)
                                          ? source.graphics.texture_quality
                                          : defaults.graphics.texture_quality;
    result.graphics.model_quality = valid_quality(source.graphics.model_quality)
                                        ? source.graphics.model_quality
                                        : defaults.graphics.model_quality;
    result.controls.mouse_sensitivity = normalized_unit_value(source.controls.mouse_sensitivity,
                                                              defaults.controls.mouse_sensitivity);

    // Keep the first valid occurrence. Later invalid/conflicting rows restore
    // their own retail default when possible, otherwise become unbound.
    result.controls.bindings.fill(InputBinding::unbound());
    for (std::size_t index{}; index < control_action_count; ++index) {
        auto candidate = source.controls.bindings[index];
        if (!valid_binding(candidate) || is_reserved_inventory_binding(candidate) ||
            binding_in_use(result.controls, candidate, index)) {
            candidate = defaults.controls.bindings[index];
        }
        if (!valid_binding(candidate) || is_reserved_inventory_binding(candidate) ||
            binding_in_use(result.controls, candidate, index)) {
            candidate = InputBinding::unbound();
        }
        result.controls.bindings[index] = candidate;
    }

    return result;
}

SettingsValidationResult validate_settings(const ClientSettings& settings) {
    if (settings.schema_version != current_settings_schema_version) {
        return {false, "unsupported settings schema version"};
    }
    const auto unit_value_valid = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (!unit_value_valid(settings.main.master_volume) ||
        !unit_value_valid(settings.main.music_volume)) {
        return {false, "volume must be finite and between 0 and 1"};
    }
    if (!valid_language(settings.main.language)) {
        return {false, "language must be a BCP-47-style locale tag"};
    }
    if (!valid_audio_device(settings.main.audio_device)) {
        return {false, "audio device must be at most 512 bytes with no control characters"};
    }
    if (settings.graphics.resolution.width < minimum_resolution_width ||
        settings.graphics.resolution.height < minimum_resolution_height ||
        settings.graphics.resolution.width > maximum_resolution_dimension ||
        settings.graphics.resolution.height > maximum_resolution_dimension) {
        return {false, "resolution is outside the supported range"};
    }
    if (!valid_graphics_api(settings.graphics.graphics_api) ||
        !valid_antialiasing(settings.graphics.antialiasing) ||
        !valid_quality(settings.graphics.effect_quality) ||
        !valid_draw_distance(settings.graphics.draw_distance) ||
        !valid_shader_quality(settings.graphics.shader_quality) ||
        !valid_quality(settings.graphics.texture_quality) ||
        !valid_quality(settings.graphics.model_quality)) {
        return {false, "graphics option contains an unknown enum value"};
    }
    if (!unit_value_valid(settings.controls.mouse_sensitivity)) {
        return {false, "mouse sensitivity must be finite and between 0 and 1"};
    }
    for (std::size_t index{}; index < control_action_count; ++index) {
        const auto binding = settings.controls.bindings[index];
        if (!valid_binding(binding)) {
            return {false, "control binding is malformed"};
        }
        if (is_reserved_inventory_binding(binding)) {
            return {false, "number keys 0-9 are reserved for inventory slots"};
        }
        if (binding_in_use(settings.controls, binding, index)) {
            return {false, "control binding conflicts with an earlier action"};
        }
    }
    return {};
}

bool valid_binding(InputBinding binding) noexcept {
    switch (binding.kind) {
    case BindingKind::unbound:
        return binding.code == 0U;
    case BindingKind::keyboard_scancode:
        return binding.code > 0U && binding.code <= maximum_keyboard_scancode;
    case BindingKind::mouse_button:
        return binding.code > 0U && binding.code <= maximum_mouse_button;
    }
    return false;
}

bool is_reserved_inventory_binding(InputBinding binding) noexcept {
    // The retail duplicate guard checked the complete 0-9 key range even
    // though its visible helper text said "1-9".
    return binding.kind == BindingKind::keyboard_scancode && binding.code >= scancode_1 &&
           binding.code <= scancode_0;
}

std::string_view control_action_name(ControlAction action) noexcept {
    if (!valid_action(action)) {
        return {};
    }
    return action_names[action_index(action)];
}

std::optional<ControlAction> control_action_from_name(std::string_view name) noexcept {
    for (std::size_t index{}; index < action_names.size(); ++index) {
        if (action_names[index] == name) {
            return static_cast<ControlAction>(index);
        }
    }
    return std::nullopt;
}

std::string_view retail_key_name_id(std::uint32_t scancode) noexcept {
    // SDL/USB scancode -> pyglet key.symbol_string() after translate_key()'s
    // '_'/'NUM_' stripping and KEY_TRANSLATIONS.
    static constexpr std::array<std::string_view, 26U> letters{
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static constexpr std::array<std::string_view, 10U> digits{
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    static constexpr std::array<std::string_view, 12U> function_keys{
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"};
    static constexpr std::array<std::string_view, 12U> upper_function_keys{
        "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};
    if (scancode >= 4U && scancode <= 29U) return letters[scancode - 4U];
    if (scancode >= 30U && scancode <= 39U) return digits[scancode - 30U];
    if (scancode >= 58U && scancode <= 69U) return function_keys[scancode - 58U];
    if (scancode >= 104U && scancode <= 115U) return upper_function_keys[scancode - 104U];
    if (scancode >= 89U && scancode <= 97U) return digits[scancode - 89U]; // NUM_1..NUM_9
    switch (scancode) {
    case 40U: return "RETURN";
    case 41U: return "ESCAPE";
    case 42U: return "BACKSPACE";
    case 43U: return "TAB";
    case 44U: return "SPACE";
    case 45U: return "MINUS";
    case 46U: return "EQUAL";
    case 47U: return "BRACKETLEFT";
    case 48U: return "BRACKETRIGHT";
    case 49U: return "BACKSLASH";
    case 50U: return "HASH";
    case 51U: return "SEMICOLON";
    case 52U: return "APOSTROPHE";
    case 53U: return "GRAVE";
    case 54U: return "COMMA";
    case 55U: return "PERIOD";
    case 56U: return "SLASH";
    case 57U: return "CAPS_LOCK";
    case 70U: return "PRINT";
    case 71U: return "SCROLLLOCK";
    case 72U: return "PAUSE";
    case 73U: return "INSERT";
    case 74U: return "HOME";
    case 75U: return "PAGE_UP";
    case 76U: return "DELETE";
    case 77U: return "END";
    case 78U: return "PAGE_DOWN";
    case 79U: return "RIGHT";
    case 80U: return "LEFT";
    case 81U: return "DOWN";
    case 82U: return "UP";
    case 83U: return "NUM_LOCK";
    case 84U: return "DIVIDE";
    case 85U: return "MULTIPLY";
    case 86U: return "SUBTRACT";
    case 87U: return "ADD";
    case 88U: return "ENTER";
    case 98U: return "0";
    case 99U: return "DECIMAL";
    case 100U: return "BACKSLASH";
    case 101U: return "MENU";
    case 224U: return "CTRL";
    case 225U: return "SHIFT";
    case 226U: return "LALT";
    case 227U: return "LWINDOWS";
    case 228U: return "RCTRL";
    case 229U: return "RSHIFT";
    case 230U: return "RALT";
    case 231U: return "RWINDOWS";
    default: return {};
    }
}

std::string retail_binding_name_id(InputBinding binding) {
    if (!valid_binding(binding) || binding.is_unbound()) return "NONE";
    if (binding.kind == BindingKind::keyboard_scancode) {
        return std::string{retail_key_name_id(binding.code)};
    }
    switch (binding.code) {
    case 1U: return "LMB";
    case 2U: return "MMB";
    case 3U: return "RMB";
    default: return "MOUSE" + std::to_string(binding.code);
    }
}

std::string binding_to_string(InputBinding binding) {
    if (!valid_binding(binding) || binding.is_unbound()) {
        return "unbound";
    }
    if (binding.kind == BindingKind::keyboard_scancode) {
        return "keyboard:" + keyboard_name(binding.code);
    }
    switch (binding.code) {
    case 1U:
        return "mouse:left";
    case 2U:
        return "mouse:middle";
    case 3U:
        return "mouse:right";
    case 4U:
        return "mouse:x1";
    case 5U:
        return "mouse:x2";
    default:
        return "mouse:button-" + std::to_string(binding.code);
    }
}

std::optional<InputBinding> binding_from_string(std::string_view value) noexcept {
    try {
        const auto normalized = ascii_lower(value);
        if (normalized == "unbound") {
            return InputBinding::unbound();
        }

        constexpr std::string_view keyboard_prefix{"keyboard:"};
        if (normalized.starts_with(keyboard_prefix)) {
            const auto code = keyboard_code_from_name(
                std::string_view{normalized}.substr(keyboard_prefix.size()));
            if (!code.has_value()) {
                return std::nullopt;
            }
            const auto binding = InputBinding::keyboard(*code);
            return valid_binding(binding) ? std::optional{binding} : std::nullopt;
        }

        constexpr std::string_view mouse_prefix{"mouse:"};
        if (!normalized.starts_with(mouse_prefix)) {
            return std::nullopt;
        }
        const auto button = std::string_view{normalized}.substr(mouse_prefix.size());
        std::uint32_t code{};
        if (button == "left") {
            code = 1U;
        } else if (button == "middle") {
            code = 2U;
        } else if (button == "right") {
            code = 3U;
        } else if (button == "x1") {
            code = 4U;
        } else if (button == "x2") {
            code = 5U;
        } else {
            constexpr std::string_view button_prefix{"button-"};
            if (!button.starts_with(button_prefix)) {
                return std::nullopt;
            }
            const auto parsed = parse_unsigned(button.substr(button_prefix.size()));
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            code = *parsed;
        }
        const auto binding = InputBinding::mouse(code);
        return valid_binding(binding) ? std::optional{binding} : std::nullopt;
    } catch (...) {
        // Public parsing is noexcept because configuration input is untrusted.
        return std::nullopt;
    }
}

} // namespace battlespades::settings
