#pragma once

#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/ui/widget.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

enum class FriendsLobbyTab : std::uint8_t {
    friends,
    requests,
    invitations,
};

enum class FriendsLobbyControl : std::uint8_t { search, primary, secondary, back };

enum class FriendsLobbyPhase : std::uint8_t {
    idle,
    starting,
    waiting_for_host,
    joining,
    reconnecting,
    error,
};

enum class FriendsLobbyActionKind : std::uint8_t {
    back,
    search,
    send_friend_request,
    accept_friend_request,
    decline_friend_request,
    remove_friend,
    create_lobby,
    open_lobby,
    accept_lobby_invite,
    decline_lobby_invite,
    join_friend_lobby,
    invite_friend,
    leave_lobby,
    start_lobby,
    join_game,
    retry,
};

struct FriendsLobbyFriend final {
    std::string id;
    std::string name;
    std::string presence{"offline"};
    std::string relationship{"accepted"};
    std::string direction;
    std::string current_lobby_id;
    std::string current_server_id;

    [[nodiscard]] friend bool operator==(const FriendsLobbyFriend&,
                                         const FriendsLobbyFriend&) = default;
};

struct FriendsLobbyInvitation final {
    std::string id;
    std::string lobby_id;
    std::string lobby_name;
    std::string inviter_name;

    [[nodiscard]] friend bool operator==(const FriendsLobbyInvitation&,
                                         const FriendsLobbyInvitation&) = default;
};

struct FriendsLobbyMember final {
    std::string id;
    std::string name;
    std::string presence{"online"};
    bool in_game{};

    [[nodiscard]] friend bool operator==(const FriendsLobbyMember&,
                                         const FriendsLobbyMember&) = default;
};

struct FriendsLobby final {
    std::string id;
    std::string owner_id;
    std::string name;
    std::string state{"idle"};
    std::string server_id;
    std::size_t maximum_members{24U};
    std::vector<FriendsLobbyMember> members;

    [[nodiscard]] friend bool operator==(const FriendsLobby&,
                                         const FriendsLobby&) = default;
};

struct FriendsLobbySnapshot final {
    std::string cursor{"0"};
    std::vector<FriendsLobbyFriend> friends;
    std::vector<FriendsLobbyInvitation> invitations;
    std::optional<FriendsLobby> lobby;
};

struct FriendsLobbyIntent final {
    FriendsLobbyActionKind kind{FriendsLobbyActionKind::back};
    std::string target_id;
    std::string text;

    [[nodiscard]] friend bool operator==(const FriendsLobbyIntent&,
                                         const FriendsLobbyIntent&) = default;
};

struct FriendsLobbyButton final {
    std::string_view label;
    std::optional<FriendsLobbyIntent> intent;
};

struct FriendsLobbyOperation final {
    std::uint64_t generation{};
    FriendsLobbyIntent intent;
    std::chrono::steady_clock::time_point deadline{};
    /** Membership this operation must affect, captured before snapshots change. */
    std::string expected_lobby_id;
};

struct FriendsLobbyLayout final {
    ui::Rect frame{};
    ui::Rect title{};
    ui::Rect tabs{};
    ui::Rect search_field{};
    ui::Rect search_button{};
    ui::Rect friends_list{};
    ui::Rect lobby_panel{};
    ui::Rect primary_button{};
    ui::Rect secondary_button{};
    ui::Rect back_button{};
};

/**
 * Stateful, renderer-neutral friends and squad-lobby controller.
 *
 * The model accepts only validated snapshots on the UI thread. Selection is
 * retained by stable account/invitation identifiers; transient operations are
 * generation checked and time bounded, so a late HTTP response cannot revive
 * a closed menu or overwrite a newer action.
 */
class FriendsLobbyMenuModel final {
public:
    static constexpr std::int32_t subpixels_per_pixel{
        MainMenuModel::subpixels_per_pixel};
    static constexpr std::size_t maximum_search_code_points{64U};
    static constexpr std::size_t maximum_rows{512U};
    static constexpr std::size_t visible_rows{9U};
    static constexpr std::size_t visible_invitation_rows{7U};

    FriendsLobbyMenuModel();

    [[nodiscard]] FriendsLobbyTab tab() const noexcept;
    [[nodiscard]] FriendsLobbyPhase phase() const noexcept;
    [[nodiscard]] std::string_view local_account_id() const noexcept;
    [[nodiscard]] std::string_view search_text() const noexcept;
    [[nodiscard]] std::string_view selected_friend_id() const noexcept;
    [[nodiscard]] std::string_view selected_invitation_id() const noexcept;
    [[nodiscard]] std::string_view error() const noexcept;
    [[nodiscard]] std::string_view service_status() const noexcept;
    [[nodiscard]] bool service_available() const noexcept;
    [[nodiscard]] bool search_focused() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool is_host() const noexcept;
    [[nodiscard]] const FriendsLobbySnapshot& snapshot() const noexcept;
    [[nodiscard]] const FriendsLobbyLayout& layout() const noexcept;
    [[nodiscard]] std::span<const std::size_t> visible_friend_indices() const noexcept;
    [[nodiscard]] std::span<const std::size_t> visible_invitation_indices() const noexcept;
    [[nodiscard]] std::size_t first_visible_friend_row() const noexcept;
    [[nodiscard]] std::size_t first_visible_invitation_row() const noexcept;
    [[nodiscard]] std::optional<FriendsLobbyOperation> operation() const noexcept;
    [[nodiscard]] FriendsLobbyButton primary_button() const;
    [[nodiscard]] FriendsLobbyButton secondary_button() const;
    [[nodiscard]] bool control_hovered(FriendsLobbyControl control) const noexcept;
    [[nodiscard]] bool control_pressed(FriendsLobbyControl control) const noexcept;

    void set_identity(std::string account_id);
    void enter(std::chrono::steady_clock::time_point now) noexcept;
    void leave() noexcept;
    void set_tab(FriendsLobbyTab tab) noexcept;
    void set_service_status(bool available, std::string status);
    void apply_snapshot(FriendsLobbySnapshot snapshot,
                        std::chrono::steady_clock::time_point now);
    /** Replace transient profile-search rows without mutating friendship state. */
    void set_search_results(std::vector<FriendsLobbyFriend> results);
    /** Apply only the still-current lookup; closing, timing out or editing invalidates it. */
    [[nodiscard]] bool apply_search_results(std::uint64_t generation,
                                            std::string_view query,
                                            std::vector<FriendsLobbyFriend> results,
                                            std::chrono::steady_clock::time_point now);
    void tick(std::chrono::steady_clock::time_point now) noexcept;

    [[nodiscard]] bool append_search_text(std::string_view utf8);
    [[nodiscard]] bool erase_search_code_point() noexcept;
    void clear_search() noexcept;
    /** Scroll the active stable-ID list without changing its selection. */
    [[nodiscard]] bool scroll_rows(std::int32_t rows) noexcept;
    /** Pointer coordinates use the same subpixels as pointer_move. */
    [[nodiscard]] bool scroll_rows_at(std::int32_t rows, std::optional<ui::Point> point) noexcept;
    /** Move the current list selection, reveal it, and leave search text entry. */
    [[nodiscard]] bool move_selection(std::int32_t direction);
    [[nodiscard]] bool cycle_tab(std::int32_t direction) noexcept;

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point,
                       std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] std::optional<FriendsLobbyIntent>
    pointer_release(std::optional<ui::Point> point,
                    std::chrono::steady_clock::time_point now) noexcept;

    /** Begin a network operation and return its unique generation token. */
    [[nodiscard]] std::optional<FriendsLobbyOperation>
    begin(FriendsLobbyIntent intent, std::chrono::steady_clock::time_point now);

    /** Complete only the current generation; stale completions are ignored. */
    [[nodiscard]] bool complete(std::uint64_t generation,
                                bool success,
                                std::string error,
                                std::chrono::steady_clock::time_point now) noexcept;

private:
    enum class HitKind : std::uint8_t {
        none,
        tab_friends,
        tab_requests,
        tab_invitations,
        search_field,
        search_button,
        friend_row,
        invitation_row,
        primary,
        secondary,
        back,
    };

    struct Hit final {
        HitKind kind{HitKind::none};
        std::size_t row{};

        [[nodiscard]] friend constexpr bool operator==(const Hit&, const Hit&) = default;
    };

    [[nodiscard]] Hit hit_test(ui::Point point) const noexcept;
    [[nodiscard]] std::optional<FriendsLobbyIntent>
    intent_for(Hit hit, std::chrono::steady_clock::time_point now) const;
    void rebuild_visible_rows();
    [[nodiscard]] bool scroll_list(std::int32_t rows, bool invitations) noexcept;
    void merge_search_results();
    void stabilize_selection();
    void update_phase_from_lobby() noexcept;
    [[nodiscard]] static std::chrono::milliseconds
    timeout_for(FriendsLobbyActionKind kind) noexcept;

    FriendsLobbySnapshot snapshot_;
    std::vector<FriendsLobbyFriend> authoritative_friends_;
    std::vector<FriendsLobbyFriend> search_results_;
    FriendsLobbyLayout layout_;
    std::vector<std::size_t> visible_friends_;
    std::vector<std::size_t> visible_invitations_;
    std::string local_account_id_;
    std::string search_;
    std::string selected_friend_id_;
    std::string selected_invitation_id_;
    std::string error_;
    std::string service_status_;
    FriendsLobbyTab tab_{FriendsLobbyTab::friends};
    FriendsLobbyPhase phase_{FriendsLobbyPhase::idle};
    std::optional<FriendsLobbyOperation> operation_;
    std::optional<Hit> hovered_;
    std::optional<Hit> pressed_;
    std::optional<FriendsLobbyIntent> pressed_intent_;
    std::uint64_t next_generation_{1U};
    std::size_t first_visible_friend_row_{};
    std::size_t first_visible_invitation_row_{};
    std::chrono::steady_clock::time_point controls_armed_after_{};
    bool service_available_{};
    bool search_focused_{};
    bool entered_{};
};

} // namespace battlespades::frontend
