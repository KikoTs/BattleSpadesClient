#include "battlespades/frontend/ugc_publish_menu.hpp"
#include "battlespades/frontend/ugc_publish_presentation.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::frontend;
using battlespades::ui::DrawList;
using battlespades::ui::DrawSpace;
using battlespades::ui::InputAction;
using battlespades::ui::InputEvent;
using battlespades::ui::InputPhase;
using battlespades::ui::SpriteDrawCommand;
using battlespades::ui::TextDrawCommand;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] InputEvent pressed(InputAction action) noexcept {
    return InputEvent{action, InputPhase::pressed};
}

[[nodiscard]] UgcLocalMapRecord map(std::string uid,
                                    std::string title,
                                    bool publishable,
                                    UgcLocalMapState state = UgcLocalMapState::unpublished,
                                    std::string preview = {}) {
    UgcLocalMapRecord result;
    result.uid = std::move(uid);
    result.title = std::move(title);
    result.preview_asset = std::move(preview);
    result.state = state;
    result.modes.push_back(UgcPublishModeStatus{"ctf",
                                                "CAPTURE_THE_FLAG",
                                                publishable,
                                                publishable
                                                    ? std::string{}
                                                    : std::string{"UGC_OBJECTIVE_TEAM1_ZONE_MIN"}});
    return result;
}

[[nodiscard]] bool has_text(const DrawList& list, std::string_view key) {
    for (const auto& command : list.commands()) {
        if (const auto* text = std::get_if<TextDrawCommand>(&command);
            text != nullptr && text->localization_key == key) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_sprite(const DrawList& list, std::string_view asset) {
    for (const auto& command : list.commands()) {
        if (const auto* sprite = std::get_if<SpriteDrawCommand>(&command);
            sprite != nullptr && sprite->asset_id == asset) {
            return true;
        }
    }
    return false;
}

void empty_repository_is_safe_and_never_invents_a_map() {
    UgcPublishMenuModel model;
    model.replace_local_maps({});
    expect(model.local_maps().empty() && model.selected_map() == nullptr,
           "an empty repository must remain empty without placeholder records");
    expect(!model.primary_enabled() && !model.activate_primary().has_value() &&
               !model.request_delete().has_value(),
           "Preview and Delete must fail closed with no local map");
    const auto back = model.back();
    expect(back.has_value() && back->kind == UgcPublishEffectKind::return_to_ugc_select &&
               back->sound_asset == main_menu_assets::back_sound,
           "Back must remain available from the empty root screen");

    const auto layer = UgcPublishPresentation{}.build_layer(model);
    expect(has_text(layer, "NO_LOCAL_UGC_MAPS") && !has_text(layer, "DELETE"),
           "empty presentation must state reality and omit Delete");
}

void repository_snapshot_deduplicates_and_preserves_selection() {
    UgcPublishMenuModel model;
    model.replace_local_maps({map("one.vxl", "One", true),
                              map("one.vxl", "Duplicate", true),
                              map("two.vxl", "Two", true),
                              map("", "Invalid", true)});
    expect(model.local_maps().size() == 2U && model.selected_index() == 0U &&
               model.selected_map()->title == "One",
           "first unique local UID must auto-select like retail populate_list");
    expect(model.select_map(1U), "second local map should select");
    model.replace_local_maps(
        {map("zero.vxl", "Zero", true), map("two.vxl", "Two Updated", true)});
    expect(model.selected_map() != nullptr && model.selected_map()->uid == "two.vxl" &&
               model.selected_map()->title == "Two Updated",
           "refresh must preserve the selected opaque UID while accepting new metadata");
}

void preview_name_confirmation_and_upload_are_transactional() {
    UgcPublishMenuModel model;
    model.replace_local_maps(
        {map("blocked.vxl", "Blocked", false), map("ready.vxl", "Ready Map", true)});
    expect(!model.primary_enabled(), "a map with no publishable mode must disable Preview");
    expect(model.select_map(1U) && model.primary_enabled(),
           "a valid selected map must enable Preview");

    const auto preview = model.activate_primary();
    expect(preview.has_value() && preview->kind == UgcPublishEffectKind::show_name_map &&
               model.page() == UgcPublishPage::name_map &&
               model.publish_title() == "Ready Map" &&
               model.primary_localization_key() == "PUBLISH",
           "first activation must open Name Map with the selected title");
    expect(model.set_publish_title("My Workshop Map"), "valid UTF-8 title should update");

    const auto confirmation = model.activate_primary();
    expect(confirmation.has_value() &&
               confirmation->kind == UgcPublishEffectKind::show_publish_confirmation &&
               model.dialog() == UgcPublishDialog::confirm_publish,
           "second activation must show confirmation without uploading yet");
    const auto request = model.confirm_dialog();
    expect(request.has_value() && request->kind == UgcPublishEffectKind::publish_requested &&
               request->publish_request ==
                   UgcPublishRequest{"ready.vxl", "My Workshop Map"} &&
               model.dialog() == UgcPublishDialog::uploading,
           "confirmation must emit one immutable upload request");
    expect(!model.back().has_value() && !model.activate_primary().has_value(),
           "uploading dialog must block navigation and duplicate requests");

    const auto completed = model.finish_publish(true);
    expect(completed.has_value() &&
               completed->kind == UgcPublishEffectKind::publish_succeeded &&
               completed->external_url ==
                   "http://steamcommunity.com/workshop/browse/?appid=224540" &&
               model.dialog() == UgcPublishDialog::none,
           "successful callback must emit the recovered Workshop handoff once");
    expect(!model.finish_publish(true).has_value(),
           "stale duplicate callbacks must be rejected");
}

void back_and_dialog_cancel_preserve_retail_panel_order() {
    UgcPublishMenuModel model;
    model.replace_local_maps({map("ready.vxl", "Ready", true)});
    static_cast<void>(model.activate_primary());
    const auto back_to_list = model.back();
    expect(back_to_list.has_value() &&
               back_to_list->kind == UgcPublishEffectKind::show_map_list &&
               model.page() == UgcPublishPage::map_list,
           "Back from Name Map must restore Map List instead of closing the scene");

    static_cast<void>(model.activate_primary());
    static_cast<void>(model.activate_primary());
    const auto cancel = model.cancel_dialog();
    expect(cancel.has_value() && cancel->kind == UgcPublishEffectKind::dialog_dismissed &&
               cancel->sound_asset == main_menu_assets::back_sound &&
               model.page() == UgcPublishPage::name_map,
           "No on the confirmation must remain on Name Map with menu_backA");
}

void deletion_uses_opaque_uid_and_rejects_stale_callbacks() {
    UgcPublishMenuModel model;
    model.replace_local_maps(
        {map("first.vxl", "First", true), map("second.vxl", "Second", true)});
    const auto confirmation = model.request_delete();
    expect(confirmation.has_value() &&
               confirmation->kind == UgcPublishEffectKind::show_delete_confirmation &&
               model.dialog() == UgcPublishDialog::confirm_delete,
           "Delete must show confirmation before touching repository state");
    expect(!model.select_map(1U), "dialog must lock list selection");
    const auto request = model.confirm_dialog();
    expect(request.has_value() && request->delete_request == UgcDeleteRequest{"first.vxl"} &&
               model.local_maps().size() == 2U,
           "confirmation emits an opaque delete request without mutating local rows");
    expect(!model.finish_delete("second.vxl", true).has_value() &&
               model.local_maps().size() == 2U,
           "mismatched asynchronous delete callback must fail closed");
    const auto removed = model.finish_delete("first.vxl", true);
    expect(removed.has_value() && removed->kind == UgcPublishEffectKind::delete_succeeded &&
               model.local_maps().size() == 1U &&
               model.selected_map()->uid == "second.vxl" &&
               model.dialog() == UgcPublishDialog::delete_complete,
           "matching callback must remove only its row and repair selection");
}

void title_input_matches_retail_code_point_limit_and_fails_closed() {
    UgcPublishMenuModel model;
    model.replace_local_maps({map("ready.vxl", "Ready", true)});
    expect(!model.set_publish_title("too early"),
           "title adapter must not mutate hidden Name Map state");
    static_cast<void>(model.activate_primary());
    const std::string maximum(200U, 'a');
    expect(model.set_publish_title(maximum) && model.publish_title() == maximum,
           "exactly 200 UTF-8 code points must be accepted");
    const std::string too_long(201U, 'b');
    expect(!model.set_publish_title(too_long) && model.publish_title() == maximum,
           "201st code point must be rejected atomically");
    const std::string malformed{"bad\xFF", 4U};
    expect(!model.set_publish_title(malformed) && model.publish_title() == maximum,
           "malformed UTF-8 must not reach shaping or Workshop adapters");
    expect(model.set_publish_title("   ") && !model.primary_enabled(),
           "whitespace-only title remains editable but cannot be submitted");
}

void scrolling_and_semantic_input_keep_selection_visible() {
    UgcPublishMenuModel model;
    std::vector<UgcLocalMapRecord> maps;
    for (std::size_t index = 0U; index < 20U; ++index) {
        maps.push_back(map("map-" + std::to_string(index),
                           "Map " + std::to_string(index),
                           true));
    }
    model.replace_local_maps(std::move(maps));
    expect(model.select_map(15U) && model.first_visible_row() == 3U,
           "selection below the viewport must reveal the minimum row range");
    static_cast<void>(model.handle(pressed(InputAction::navigate_down)));
    expect(model.selected_index() == 16U && model.first_visible_row() == 4U,
           "keyboard Down must advance and keep the selected row visible");
    model.scroll_rows(50);
    expect(model.first_visible_row() == 7U,
           "scrolling must clamp to the last full thirteen-row page");
    expect(model.select_visible_row(12U) && model.selected_index() == 19U,
           "visible-row selection must map through the current scroll offset");
}

void presentation_recovers_root_geometry_and_both_visible_panels() {
    const auto layout = ugc_publish_classic_layout();
    expect(layout.frame == battlespades::ui::DrawRect{25.0, 5.0, 750.0, 589.0} &&
               layout.map_panel == battlespades::ui::DrawRect{56.0, 95.0, 340.0, 413.0} &&
               layout.preview_panel ==
                   battlespades::ui::DrawRect{401.0, 95.0, 340.0, 354.0} &&
               layout.primary_button ==
                   battlespades::ui::DrawRect{405.0, 456.0, 332.0, 50.0},
           "root ListPreview and panel geometry must match retail coordinates");

    UgcPublishMenuModel model;
    model.replace_local_maps({map("ready.vxl",
                                  "Ready",
                                  true,
                                  UgcLocalMapState::changed_since_publish,
                                  "ugc/ready.png")});
    UgcPublishPresentation presentation;
    const auto root = presentation.build_layer(model);
    expect(has_sprite(root, ugc_publish_assets::frame) && has_text(root, "PUBLISH") &&
               has_text(root, "MAP_LIST") && has_text(root, "Ready") &&
               has_text(root, "UGC_CHANGED_SINCE_PUBLISH") &&
               has_text(root, "DELETE") && has_text(root, "UGC_PREVIEW_PUBLISH"),
           "root layer must include the list, selection state, preview controls, and title");
    for (const auto& command : root.commands()) {
        const auto design_space = std::visit(
            [](const auto& value) {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::is_same_v<
                                  Value,
                                  battlespades::ui::PlayerNamePlateDrawRequest>) {
                    return false;
                } else {
                    return value.space == DrawSpace::design_pixels;
                }
            },
            command);
        expect(design_space, "slide layer may contain only translatable design-space commands");
    }

    static_cast<void>(model.activate_primary());
    const auto naming = presentation.build_layer(model);
    expect(has_text(naming, "NAME_MAP") && has_text(naming, "Ready") &&
               has_sprite(naming, "ugc/ready.png") && has_text(naming, "PUBLISH"),
           "Name Map layer must preserve selected metadata and local preview identity");
    static_cast<void>(model.activate_primary());
    const auto confirmation = presentation.build_layer(model);
    expect(has_sprite(confirmation, ugc_publish_assets::message_extended) &&
               has_text(confirmation, "UGC_PUBLISH_CONFIRMATION") &&
               has_text(confirmation, "STEMWORKS_LICENSE_MESSAGE"),
           "upload confirmation must use the recovered extended disclaimer panel");

    const auto complete = presentation.build(model);
    const auto* background = std::get_if<SpriteDrawCommand>(&complete.commands().front());
    expect(background != nullptr && background->asset_id == main_menu_assets::background &&
               background->space == DrawSpace::window_pixels,
           "standalone build must prepend one stationary covered background");
}

void presentation_rejects_invalid_context() {
    const UgcPublishMenuModel model;
    const UgcPublishPresentation presentation;
    bool rejected{};
    try {
        static_cast<void>(presentation.build(model, UgcPublishPresentationContext{{0, 600}, 1'000U}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected, "zero-sized UGC Publish context must fail before drawing");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"empty_repository_is_safe_and_never_invents_a_map",
         empty_repository_is_safe_and_never_invents_a_map},
        {"repository_snapshot_deduplicates_and_preserves_selection",
         repository_snapshot_deduplicates_and_preserves_selection},
        {"preview_name_confirmation_and_upload_are_transactional",
         preview_name_confirmation_and_upload_are_transactional},
        {"back_and_dialog_cancel_preserve_retail_panel_order",
         back_and_dialog_cancel_preserve_retail_panel_order},
        {"deletion_uses_opaque_uid_and_rejects_stale_callbacks",
         deletion_uses_opaque_uid_and_rejects_stale_callbacks},
        {"title_input_matches_retail_code_point_limit_and_fails_closed",
         title_input_matches_retail_code_point_limit_and_fails_closed},
        {"scrolling_and_semantic_input_keep_selection_visible",
         scrolling_and_semantic_input_keep_selection_visible},
        {"presentation_recovers_root_geometry_and_both_visible_panels",
         presentation_recovers_root_geometry_and_both_visible_panels},
        {"presentation_rejects_invalid_context", presentation_rejects_invalid_context},
    };

    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0U ? 0 : 1;
}
