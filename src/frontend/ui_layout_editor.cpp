#include "battlespades/frontend/ui_layout_editor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::uint32_t schema_version{1U};
constexpr std::uintmax_t maximum_file_bytes{2U * 1'024U * 1'024U};
constexpr std::size_t maximum_overrides{8'192U};
constexpr double minimum_extent{1.0};
constexpr double resize_handle{8.0};

[[nodiscard]] bool finite_rect(const ui::DrawRect& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.width) && std::isfinite(value.height) &&
           value.width >= minimum_extent && value.height >= minimum_extent &&
           std::abs(value.x) <= 100'000.0 && std::abs(value.y) <= 100'000.0 &&
           value.width <= 100'000.0 && value.height <= 100'000.0;
}

[[nodiscard]] std::string component(std::string_view value) {
    std::string result;
    result.reserve(std::min<std::size_t>(value.size(), 96U));
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        const bool safe = (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') ||
                          character == '_' || character == '-' || character == '.';
        result.push_back(safe ? static_cast<char>(character) : '_');
        if (result.size() == 96U) break;
    }
    if (result.empty()) result = "unnamed";
    return result;
}

[[nodiscard]] std::string command_base(const ui::DrawCommand& command) {
    if (const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
        return "sprite." + component(sprite->asset_id);
    }
    if (const auto* text = std::get_if<ui::TextDrawCommand>(&command)) {
        return "text." + component(text->localization_key);
    }
    return "nameplate";
}

[[nodiscard]] std::optional<ui::DrawRect> command_bounds(const ui::DrawCommand& command) {
    if (const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
        return sprite->destination;
    }
    if (const auto* text = std::get_if<ui::TextDrawCommand>(&command)) {
        return text->destination;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ui::DrawSpace> command_space(const ui::DrawCommand& command) {
    if (const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
        return sprite->space;
    }
    if (const auto* text = std::get_if<ui::TextDrawCommand>(&command)) {
        return text->space;
    }
    return std::nullopt;
}

void set_command_bounds(ui::DrawCommand& command, ui::DrawRect bounds) {
    if (auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command)) {
        sprite->destination = bounds;
    } else if (auto* text = std::get_if<ui::TextDrawCommand>(&command)) {
        text->destination = bounds;
    }
}

[[nodiscard]] std::vector<UiLayoutElement>
enumerate(std::string_view screen_id, const ui::DrawList& list) {
    std::vector<UiLayoutElement> result;
    result.reserve(list.size());
    std::unordered_map<std::string, std::size_t> occurrences;
    for (std::size_t index{}; index < list.commands().size(); ++index) {
        const auto& command = list.commands()[index];
        const auto bounds = command_bounds(command);
        const auto space = command_space(command);
        if (!bounds.has_value() || !space.has_value() ||
            *space != ui::DrawSpace::design_pixels || !finite_rect(*bounds)) {
            continue;
        }
        auto base = command_base(command);
        const auto occurrence = occurrences[base]++;
        auto id = component(screen_id) + '/' + std::move(base) + '#' +
                  std::to_string(occurrence);
        result.push_back(UiLayoutElement{std::move(id), *bounds, *space, index});
    }
    return result;
}

[[nodiscard]] bool contains(const ui::DrawRect& bounds, ui::Point point) noexcept {
    const auto x = static_cast<double>(point.x);
    const auto y = static_cast<double>(point.y);
    return x >= bounds.x && y >= bounds.y && x < bounds.x + bounds.width &&
           y < bounds.y + bounds.height;
}

[[nodiscard]] ui::ColorModulation editor_color(ui::ColorRgba8 color) noexcept {
    return {color, 1'000U, 1'000U};
}

void solid(ui::DrawList& list, ui::DrawRect bounds, ui::ColorRgba8 color) {
    list.push(ui::SpriteDrawCommand{
        "png/high/white.png", bounds, ui::DrawSpace::design_pixels,
        ui::TextureFilter::nearest, ui::TextureAnchor::top_left, 1.0,
        ui::SpriteSizing::stretch, editor_color(color)});
}

void label(ui::DrawList& list, std::string value, ui::DrawRect bounds,
           double size, ui::ColorRgba8 color) {
    list.push(ui::TextDrawCommand{
        std::move(value), "fonts/Tuffy_Bold.ttf", bounds,
        ui::DrawSpace::design_pixels, size, 0.0, 1U,
        ui::HorizontalTextAlignment::left, ui::VerticalTextAlignment::center,
        ui::TextTransform::preserve, ui::TextFit::shrink_to_fit,
        editor_color(color)});
}

} // namespace

UiLayoutStore::UiLayoutStore(std::filesystem::path path) : path_{std::move(path)} {}

const std::filesystem::path& UiLayoutStore::path() const noexcept {
    return path_;
}

bool UiLayoutStore::load() {
    last_error_.clear();
    if (path_.empty()) {
        last_error_ = "UI layout path is empty";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) {
        if (error) {
            last_error_ = "cannot inspect UI layout: " + error.message();
            return false;
        }
        overrides_.clear();
        loaded_write_time_.reset();
        dirty_ = false;
        return true;
    }
    const auto size = std::filesystem::file_size(path_, error);
    if (error || size > maximum_file_bytes) {
        last_error_ = error ? "cannot inspect UI layout size: " + error.message()
                            : "UI layout exceeds the 2 MiB safety limit";
        return false;
    }

    try {
        std::ifstream input{path_, std::ios::binary};
        if (!input) {
            last_error_ = "cannot open UI layout";
            return false;
        }
        const auto document = nlohmann::json::parse(input);
        if (!document.is_object() || document.value("schema_version", 0U) != schema_version ||
            !document.contains("overrides") || !document["overrides"].is_object()) {
            last_error_ = "UI layout must contain schema_version 1 and an overrides object";
            return false;
        }
        if (document["overrides"].size() > maximum_overrides) {
            last_error_ = "UI layout contains too many overrides";
            return false;
        }
        std::map<std::string, ui::DrawRect, std::less<>> parsed;
        for (const auto& [id, value] : document["overrides"].items()) {
            if (id.empty() || id.size() > 320U || !value.is_object()) {
                last_error_ = "UI layout contains an invalid element id";
                return false;
            }
            ui::DrawRect bounds{
                value.value("x", std::numeric_limits<double>::quiet_NaN()),
                value.value("y", std::numeric_limits<double>::quiet_NaN()),
                value.value("width", std::numeric_limits<double>::quiet_NaN()),
                value.value("height", std::numeric_limits<double>::quiet_NaN()),
            };
            if (!finite_rect(bounds)) {
                last_error_ = "UI layout contains an invalid rectangle for " + id;
                return false;
            }
            parsed.emplace(id, bounds);
        }
        overrides_ = std::move(parsed);
        loaded_write_time_ = std::filesystem::last_write_time(path_, error);
        if (error) loaded_write_time_.reset();
        dirty_ = false;
        return true;
    } catch (const nlohmann::json::exception& exception) {
        last_error_ = "invalid UI layout JSON: " + std::string{exception.what()};
        return false;
    }
}

bool UiLayoutStore::reload_if_changed() {
    if (dirty_ || path_.empty()) return true;
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) return !error;
    const auto current = std::filesystem::last_write_time(path_, error);
    if (error || (loaded_write_time_.has_value() && current == *loaded_write_time_)) {
        return !error;
    }
    return load();
}

bool UiLayoutStore::save() {
    last_error_.clear();
    if (path_.empty()) {
        last_error_ = "UI layout path is empty";
        return false;
    }
    nlohmann::ordered_json document;
    document["schema_version"] = schema_version;
    document["coordinate_space"] = "retail_design_pixels_800x600";
    document["overrides"] = nlohmann::ordered_json::object();
    for (const auto& [id, bounds] : overrides_) {
        document["overrides"][id] = {
            {"x", bounds.x}, {"y", bounds.y},
            {"width", bounds.width}, {"height", bounds.height},
        };
    }
    std::error_code error;
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path(), error);
        if (error) {
            last_error_ = "cannot create UI layout directory: " + error.message();
            return false;
        }
    }
    auto temporary = path_;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            last_error_ = "cannot create temporary UI layout";
            return false;
        }
        output << document.dump(2) << '\n';
        output.flush();
        if (!output) {
            last_error_ = "failed while writing UI layout";
            return false;
        }
    }
    std::filesystem::remove(path_, error);
    error.clear();
    std::filesystem::rename(temporary, path_, error);
    if (error) {
        last_error_ = "cannot replace UI layout: " + error.message();
        std::filesystem::remove(temporary, error);
        return false;
    }
    loaded_write_time_ = std::filesystem::last_write_time(path_, error);
    if (error) loaded_write_time_.reset();
    dirty_ = false;
    return true;
}

void UiLayoutStore::apply(std::string_view screen_id, ui::DrawList& list) const {
    const auto editable = enumerate(screen_id, list);
    auto commands = list.mutable_commands();
    for (const auto& element : editable) {
        const auto found = overrides_.find(element.id);
        if (found != overrides_.end() && element.command_index < commands.size()) {
            set_command_bounds(commands[element.command_index], found->second);
        }
    }
}

std::vector<UiLayoutElement>
UiLayoutStore::elements(std::string_view screen_id, const ui::DrawList& list) const {
    return enumerate(screen_id, list);
}

void UiLayoutStore::set(std::string id, ui::DrawRect bounds) {
    if (id.empty() || !finite_rect(bounds)) return;
    overrides_.insert_or_assign(std::move(id), bounds);
    dirty_ = true;
}

bool UiLayoutStore::erase(std::string_view id) {
    const auto found = overrides_.find(id);
    if (found == overrides_.end()) return false;
    overrides_.erase(found);
    dirty_ = true;
    return true;
}

void UiLayoutStore::reset_screen(std::string_view screen_id) {
    const auto prefix = component(screen_id) + '/';
    for (auto iterator = overrides_.begin(); iterator != overrides_.end();) {
        if (iterator->first.starts_with(prefix)) {
            iterator = overrides_.erase(iterator);
            dirty_ = true;
        } else {
            ++iterator;
        }
    }
}

bool UiLayoutStore::dirty() const noexcept { return dirty_; }
std::size_t UiLayoutStore::override_count() const noexcept { return overrides_.size(); }
std::string_view UiLayoutStore::last_error() const noexcept { return last_error_; }

UiLayoutEditor::UiLayoutEditor(UiLayoutStore& store) : store_{store} {}

void UiLayoutEditor::set_active(bool active) noexcept {
    active_ = active;
    drag_origin_.reset();
    if (!active_) status_.clear();
}

bool UiLayoutEditor::active() const noexcept { return active_; }
bool UiLayoutEditor::dragging() const noexcept { return drag_origin_.has_value(); }

void UiLayoutEditor::observe(std::string screen_id, const ui::DrawList& list) {
    if (!active_) return;
    screen_id_ = std::move(screen_id);
    elements_ = store_.elements(screen_id_, list);
    if (!selected_id_.empty() && selected_element() == nullptr) selected_id_.clear();
}

bool UiLayoutEditor::pointer_press(ui::Point point) {
    if (!active_) return false;
    const auto selected = std::find_if(elements_.rbegin(), elements_.rend(),
                                       [point](const UiLayoutElement& element) {
                                           return contains(element.bounds, point);
                                       });
    if (selected == elements_.rend()) {
        selected_id_.clear();
        drag_origin_.reset();
        status_ = "No element selected";
        return true;
    }
    selected_id_ = selected->id;
    drag_bounds_ = selected->bounds;
    drag_origin_ = point;
    const auto right = drag_bounds_.x + drag_bounds_.width;
    const auto bottom = drag_bounds_.y + drag_bounds_.height;
    drag_resize_ = static_cast<double>(point.x) >= right - resize_handle &&
                   static_cast<double>(point.y) >= bottom - resize_handle;
    status_ = drag_resize_ ? "Resize" : "Move";
    return true;
}

bool UiLayoutEditor::pointer_move(ui::Point point) {
    if (!active_) return false;
    if (!drag_origin_.has_value()) return true;
    auto* element = selected_element();
    if (element == nullptr) return true;
    const auto dx = static_cast<double>(point.x - drag_origin_->x);
    const auto dy = static_cast<double>(point.y - drag_origin_->y);
    auto bounds = drag_bounds_;
    if (drag_resize_) {
        bounds.width = std::max(minimum_extent, bounds.width + dx);
        bounds.height = std::max(minimum_extent, bounds.height + dy);
    } else {
        bounds.x += dx;
        bounds.y += dy;
    }
    element->bounds = bounds;
    store_.set(element->id, bounds);
    return true;
}

bool UiLayoutEditor::pointer_release(ui::Point point) {
    if (!active_) return false;
    static_cast<void>(pointer_move(point));
    drag_origin_.reset();
    status_ = store_.dirty() ? "Unsaved changes - Ctrl+S" : "Ready";
    return true;
}

bool UiLayoutEditor::nudge(std::int32_t x, std::int32_t y, bool resize) {
    if (!active_) return false;
    auto* element = selected_element();
    if (element == nullptr) return true;
    auto bounds = element->bounds;
    if (resize) {
        bounds.width = std::max(minimum_extent, bounds.width + static_cast<double>(x));
        bounds.height = std::max(minimum_extent, bounds.height + static_cast<double>(y));
    } else {
        bounds.x += static_cast<double>(x);
        bounds.y += static_cast<double>(y);
    }
    element->bounds = bounds;
    store_.set(element->id, bounds);
    status_ = "Unsaved changes - Ctrl+S";
    return true;
}

bool UiLayoutEditor::save() {
    if (!active_) return false;
    const auto saved = store_.save();
    status_ = saved ? "Saved ui-layout.json" : std::string{store_.last_error()};
    return true;
}

bool UiLayoutEditor::reload() {
    if (!active_) return false;
    const auto loaded = store_.load();
    status_ = loaded ? "Reloaded ui-layout.json" : std::string{store_.last_error()};
    return true;
}

bool UiLayoutEditor::reset_selected() {
    if (!active_) return false;
    if (!selected_id_.empty()) {
        static_cast<void>(store_.erase(selected_id_));
        status_ = "Selected element reset - Ctrl+S";
    }
    return true;
}

void UiLayoutEditor::decorate(ui::DrawList& list) const {
    if (!active_) return;
    constexpr ui::ColorRgba8 panel{8U, 13U, 10U, 224U};
    constexpr ui::ColorRgba8 gold{255U, 211U, 56U, 255U};
    constexpr ui::ColorRgba8 cyan{46U, 210U, 255U, 255U};
    solid(list, {5.0, 5.0, 790.0, 54.0}, panel);
    label(list, "UI LAYOUT EDITOR  [F11 close]", {12.0, 8.0, 360.0, 18.0}, 13.0, gold);
    label(list,
          selected_id_.empty() ? "Click an element to select it" : selected_id_,
          {12.0, 26.0, 520.0, 14.0}, 10.0, cyan);
    label(list,
          status_.empty() ? "Drag move | lower-right resize | arrows nudge | Shift+arrows resize | Ctrl+S save | Ctrl+R reload | Delete reset"
                          : status_,
          {12.0, 41.0, 775.0, 13.0}, 9.0,
          status_.find("invalid") != std::string::npos ? ui::ColorRgba8{255U, 80U, 80U, 255U}
                                                       : ui::ColorRgba8{238U, 238U, 218U, 255U});

    const auto* selected = selected_element();
    if (selected == nullptr) return;
    const auto& bounds = selected->bounds;
    constexpr double stroke{2.0};
    solid(list, {bounds.x, bounds.y, bounds.width, stroke}, cyan);
    solid(list, {bounds.x, bounds.y + bounds.height - stroke, bounds.width, stroke}, cyan);
    solid(list, {bounds.x, bounds.y, stroke, bounds.height}, cyan);
    solid(list, {bounds.x + bounds.width - stroke, bounds.y, stroke, bounds.height}, cyan);
    solid(list, {bounds.x + bounds.width - resize_handle,
                 bounds.y + bounds.height - resize_handle,
                 resize_handle,
                 resize_handle}, gold);
}

std::string_view UiLayoutEditor::selected_id() const noexcept { return selected_id_; }
std::string_view UiLayoutEditor::status() const noexcept { return status_; }

UiLayoutElement* UiLayoutEditor::selected_element() noexcept {
    const auto found = std::ranges::find(elements_, selected_id_, &UiLayoutElement::id);
    return found == elements_.end() ? nullptr : &*found;
}

const UiLayoutElement* UiLayoutEditor::selected_element() const noexcept {
    const auto found = std::ranges::find(elements_, selected_id_, &UiLayoutElement::id);
    return found == elements_.end() ? nullptr : &*found;
}

} // namespace battlespades::frontend
