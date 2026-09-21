#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** Status text and color selected by retail `UGCMapListItem`. */
enum class UgcLocalMapState : std::uint8_t {
    published,
    unpublished,
    data_required,
    changed_since_publish,
};

/** One mode row rendered in the selected-map preview panel. */
struct UgcPublishModeStatus final {
    std::string mode_id;
    std::string display_key;
    bool publishable{};
    /** Localized objective/error key; empty uses the generic state label. */
    std::string reason_key;

    [[nodiscard]] friend bool operator==(const UgcPublishModeStatus&,
                                         const UgcPublishModeStatus&) = default;
};

/**
 * Immutable metadata supplied by the local UGC repository adapter.
 *
 * `uid` is opaque: the frontend never joins it to a path or deletes a file.
 * `preview_asset` may be empty when the local PNG is absent or unreadable.
 */
struct UgcLocalMapRecord final {
    std::string uid;
    std::string title;
    std::string preview_asset;
    UgcLocalMapState state{UgcLocalMapState::unpublished};
    std::vector<UgcPublishModeStatus> modes;

    [[nodiscard]] bool has_publishable_mode() const noexcept;
};

enum class UgcPublishPage : std::uint8_t {
    map_list,
    name_map,
};

enum class UgcPublishDialog : std::uint8_t {
    none,
    confirm_publish,
    uploading,
    upload_error,
    confirm_delete,
    deleting,
    delete_complete,
    operation_error,
};

/** Side effects that the renderer-neutral model asks its application to own. */
enum class UgcPublishEffectKind : std::uint8_t {
    show_name_map,
    show_map_list,
    show_publish_confirmation,
    publish_requested,
    show_delete_confirmation,
    delete_requested,
    return_to_ugc_select,
    dialog_dismissed,
    publish_succeeded,
    publish_failed,
    delete_succeeded,
    delete_failed,
};

struct UgcPublishRequest final {
    std::string local_uid;
    std::string workshop_title;

    [[nodiscard]] friend bool operator==(const UgcPublishRequest&,
                                         const UgcPublishRequest&) = default;
};

struct UgcDeleteRequest final {
    std::string local_uid;

    [[nodiscard]] friend bool operator==(const UgcDeleteRequest&,
                                         const UgcDeleteRequest&) = default;
};

struct UgcPublishEffect final {
    UgcPublishEffectKind kind{UgcPublishEffectKind::show_map_list};
    std::string_view sound_asset;
    std::optional<UgcPublishRequest> publish_request;
    std::optional<UgcDeleteRequest> delete_request;
    std::optional<std::string> external_url;
};

/**
 * Renderer-neutral reconstruction of `UGCPublishMenu` and its visible panels.
 *
 * Local discovery, image decoding, deletion, Steam upload, browser overlays,
 * audio, and scene routing are explicit adapters. This model only owns list
 * selection, panel/dialog state, title editing, and immutable requests.
 */
class UgcPublishMenuModel final {
public:
    static constexpr std::size_t maximum_local_maps{4'096U};
    static constexpr std::size_t maximum_title_code_points{200U};
    static constexpr std::size_t visible_map_rows{13U};
    static constexpr std::uint32_t retail_steam_app_id{224'540U};

    /**
     * Replaces the repository snapshot, deduplicating opaque UIDs and
     * preserving selection when possible. Empty input is a supported state.
     */
    void replace_local_maps(std::vector<UgcLocalMapRecord> maps);

    [[nodiscard]] std::span<const UgcLocalMapRecord> local_maps() const noexcept;
    [[nodiscard]] const UgcLocalMapRecord* selected_map() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] std::size_t first_visible_row() const noexcept;
    [[nodiscard]] bool select_map(std::size_t index) noexcept;
    [[nodiscard]] bool select_visible_row(std::size_t row) noexcept;
    void scroll_rows(int delta) noexcept;

    [[nodiscard]] UgcPublishPage page() const noexcept;
    [[nodiscard]] UgcPublishDialog dialog() const noexcept;
    [[nodiscard]] std::string_view publish_title() const noexcept;
    [[nodiscard]] std::string_view primary_localization_key() const noexcept;
    [[nodiscard]] bool primary_enabled() const noexcept;
    [[nodiscard]] bool input_blocked() const noexcept;

    /** Accepts valid UTF-8 up to the retail 200-code-point input limit. */
    [[nodiscard]] bool set_publish_title(std::string_view title);

    /** Preview on page one; confirmation request on page two. */
    [[nodiscard]] std::optional<UgcPublishEffect> activate_primary();
    [[nodiscard]] std::optional<UgcPublishEffect> request_delete();
    [[nodiscard]] std::optional<UgcPublishEffect> confirm_dialog();
    [[nodiscard]] std::optional<UgcPublishEffect> cancel_dialog();
    [[nodiscard]] std::optional<UgcPublishEffect> back();
    [[nodiscard]] std::optional<UgcPublishEffect> handle(ui::InputEvent event);

    /** Completes a previously emitted asynchronous upload request. */
    [[nodiscard]] std::optional<UgcPublishEffect> finish_publish(bool success, std::string item_url = {});

    /**
     * Completes the matching opaque deletion request. A mismatched UID is
     * rejected so a stale callback cannot remove a newly selected map.
     */
    [[nodiscard]] std::optional<UgcPublishEffect>
    finish_delete(std::string_view local_uid, bool success);

    /** Dismisses informational error/success dialogs. */
    [[nodiscard]] std::optional<UgcPublishEffect> acknowledge_dialog();

    [[nodiscard]] static std::string workshop_url();

private:
    [[nodiscard]] std::optional<std::size_t> index_for_uid(std::string_view uid) const noexcept;
    void reveal_selection() noexcept;
    void repair_after_repository_change();
    [[nodiscard]] static bool contains_non_whitespace(std::string_view value) noexcept;

    std::vector<UgcLocalMapRecord> local_maps_{};
    std::optional<std::string> selected_uid_{};
    std::size_t first_visible_row_{};
    UgcPublishPage page_{UgcPublishPage::map_list};
    UgcPublishDialog dialog_{UgcPublishDialog::none};
    std::string publish_title_{};
    std::optional<UgcPublishRequest> pending_publish_{};
    std::optional<UgcDeleteRequest> pending_delete_{};

};

/** Localized row status exposed to presentations without duplicating mapping. */
[[nodiscard]] std::string_view
ugc_map_state_localization_key(UgcLocalMapState state) noexcept;

} // namespace battlespades::frontend
