#include "battlespades/frontend/friends_lobby_menu.hpp"

#include "battlespades/core/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr std::int32_t scale{FriendsLobbyMenuModel::subpixels_per_pixel};

[[nodiscard]] constexpr ui::Rect rect(std::int32_t x,
                                      std::int32_t y,
                                      std::int32_t width,
                                      std::int32_t height) noexcept {
    return {x * scale, y * scale, width * scale, height * scale};
}

[[nodiscard]] bool contains(ui::Rect bounds, ui::Point point) noexcept {
    return point.x > bounds.x && point.y > bounds.y &&
           point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
}

[[nodiscard]] std::string lower_ascii(std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

[[nodiscard]] std::size_t previous_utf8_boundary(std::string_view value) noexcept {
    auto boundary = value.size();
    if (boundary == 0U) return 0U;
    --boundary;
    while (boundary > 0U &&
           (static_cast<unsigned char>(value[boundary]) & 0xC0U) == 0x80U) {
        --boundary;
    }
    return boundary;
}

template <typename Row, typename Key>
void validate_rows(std::vector<Row>& rows, Key key) {
    if (rows.size() > FriendsLobbyMenuModel::maximum_rows) {
        rows.resize(FriendsLobbyMenuModel::maximum_rows);
    }
    std::set<std::string, std::less<>> seen;
    std::erase_if(rows, [&](const Row& row) {
        const auto& id = key(row);
        return id.empty() || !seen.insert(id).second;
    });
}

/**
 * Decide whether a fresh authoritative poll proves that an in-flight write
 * committed even though its HTTP response was lost.
 *
 * Only outcomes that are unambiguous from the snapshot are accepted here.
 * Inviting a friend and sending chat, for example, have no caller-visible
 * durable row and therefore still require their direct response.
 */
[[nodiscard]] bool operation_converged(
    const FriendsLobbyOperation& operation,
    const FriendsLobbySnapshot& snapshot,
    std::string_view local_account_id) {
    const auto friend_row = [&]() -> const FriendsLobbyFriend* {
        const auto found = std::ranges::find(snapshot.friends,
                                             operation.intent.target_id,
                                             &FriendsLobbyFriend::id);
        return found == snapshot.friends.end() ? nullptr : &*found;
    };
    switch (operation.intent.kind) {
    case FriendsLobbyActionKind::send_friend_request: {
        const auto* row = friend_row();
        return row != nullptr &&
               (row->relationship == "accepted" ||
                (row->relationship == "pending" && row->direction == "outgoing"));
    }
    case FriendsLobbyActionKind::accept_friend_request: {
        const auto* row = friend_row();
        return row != nullptr && row->relationship == "accepted";
    }
    case FriendsLobbyActionKind::decline_friend_request:
    case FriendsLobbyActionKind::remove_friend:
        return friend_row() == nullptr;
    case FriendsLobbyActionKind::create_lobby:
        return snapshot.lobby.has_value() && snapshot.lobby->owner_id == local_account_id;
    case FriendsLobbyActionKind::accept_lobby_invite:
        return snapshot.lobby.has_value() && !operation.expected_lobby_id.empty() &&
               snapshot.lobby->id == operation.expected_lobby_id;
    case FriendsLobbyActionKind::join_friend_lobby:
        return snapshot.lobby.has_value() &&
               snapshot.lobby->id == operation.intent.target_id;
    case FriendsLobbyActionKind::decline_lobby_invite:
        return std::ranges::find(snapshot.invitations,
                                 operation.intent.target_id,
                                 &FriendsLobbyInvitation::id) ==
               snapshot.invitations.end();
    case FriendsLobbyActionKind::leave_lobby:
        return !snapshot.lobby.has_value() ||
               (!operation.expected_lobby_id.empty() &&
                snapshot.lobby->id != operation.expected_lobby_id);
    case FriendsLobbyActionKind::start_lobby:
        return snapshot.lobby.has_value() &&
               snapshot.lobby->id == operation.expected_lobby_id &&
               (snapshot.lobby->state == "starting" ||
                snapshot.lobby->state == "ready" ||
                snapshot.lobby->state == "in_game" ||
                !snapshot.lobby->server_id.empty());
    case FriendsLobbyActionKind::back:
    case FriendsLobbyActionKind::open_lobby:
    case FriendsLobbyActionKind::search:
    case FriendsLobbyActionKind::invite_friend:
    case FriendsLobbyActionKind::join_game:
    case FriendsLobbyActionKind::retry:
        return false;
    }
    return false;
}

} // namespace

FriendsLobbyMenuModel::FriendsLobbyMenuModel()
    : layout_{rect(25, 5, 750, 589),
              rect(120, 25, 560, 50),
              rect(56, 96, 340, 34),
              rect(56, 138, 254, 34),
              rect(314, 138, 82, 34),
              rect(56, 182, 340, 270),
              rect(404, 96, 338, 356),
              rect(404, 464, 162, 48),
              rect(580, 464, 162, 48),
              rect(54, 541, 190, 32)} {}

FriendsLobbyTab FriendsLobbyMenuModel::tab() const noexcept { return tab_; }
FriendsLobbyPhase FriendsLobbyMenuModel::phase() const noexcept { return phase_; }
std::string_view FriendsLobbyMenuModel::local_account_id() const noexcept {
    return local_account_id_;
}
std::string_view FriendsLobbyMenuModel::search_text() const noexcept { return search_; }
std::string_view FriendsLobbyMenuModel::selected_friend_id() const noexcept {
    return selected_friend_id_;
}
std::string_view FriendsLobbyMenuModel::selected_invitation_id() const noexcept {
    return selected_invitation_id_;
}
std::string_view FriendsLobbyMenuModel::error() const noexcept { return error_; }
std::string_view FriendsLobbyMenuModel::service_status() const noexcept {
    return service_status_;
}
bool FriendsLobbyMenuModel::service_available() const noexcept { return service_available_; }
bool FriendsLobbyMenuModel::search_focused() const noexcept { return search_focused_; }
bool FriendsLobbyMenuModel::busy() const noexcept { return operation_.has_value(); }
bool FriendsLobbyMenuModel::is_host() const noexcept {
    return snapshot_.lobby.has_value() && !local_account_id_.empty() &&
           snapshot_.lobby->owner_id == local_account_id_;
}
const FriendsLobbySnapshot& FriendsLobbyMenuModel::snapshot() const noexcept {
    return snapshot_;
}
const FriendsLobbyLayout& FriendsLobbyMenuModel::layout() const noexcept { return layout_; }
std::span<const std::size_t> FriendsLobbyMenuModel::visible_friend_indices() const noexcept {
    return visible_friends_;
}
std::span<const std::size_t>
FriendsLobbyMenuModel::visible_invitation_indices() const noexcept {
    return visible_invitations_;
}
std::size_t FriendsLobbyMenuModel::first_visible_friend_row() const noexcept {
    return first_visible_friend_row_;
}
std::size_t FriendsLobbyMenuModel::first_visible_invitation_row() const noexcept {
    return first_visible_invitation_row_;
}
std::optional<FriendsLobbyOperation> FriendsLobbyMenuModel::operation() const noexcept {
    return operation_;
}

FriendsLobbyButton FriendsLobbyMenuModel::primary_button() const {
    FriendsLobbyButton button{snapshot_.lobby ? "OPEN LOBBY" : "CREATE LOBBY", std::nullopt};
    const auto action = [&](std::string_view label, FriendsLobbyActionKind kind,
                            std::string target = {}) {
        return FriendsLobbyButton{label, service_available_ && !busy()
            ? std::optional{FriendsLobbyIntent{kind, std::move(target), {}}} : std::nullopt};
    };
    const auto found = std::ranges::find(snapshot_.friends, selected_friend_id_, &FriendsLobbyFriend::id);
    if (found != snapshot_.friends.end()) {
        if (found->id == local_account_id_) return {"YOUR PROFILE", std::nullopt};
        if (found->relationship == "pending") {
            return found->direction == "incoming"
                ? action("ACCEPT REQUEST", FriendsLobbyActionKind::accept_friend_request, found->id)
                : FriendsLobbyButton{"REQUEST SENT", std::nullopt};
        }
        if (found->relationship == "search" || found->relationship == "none")
            return action("ADD FRIEND", FriendsLobbyActionKind::send_friend_request, found->id);
        if (found->relationship != "accepted") return {"FRIEND SELECTED", std::nullopt};
        if (snapshot_.lobby &&
            (found->current_lobby_id == snapshot_.lobby->id ||
             std::ranges::any_of(snapshot_.lobby->members, [&](const auto& member) {
                 return member.id == found->id;
             }))) return {"ALREADY IN LOBBY", std::nullopt};
        if (!snapshot_.lobby && !found->current_lobby_id.empty())
            return action("JOIN LOBBY", FriendsLobbyActionKind::join_friend_lobby, found->current_lobby_id);
        if (!found->current_server_id.empty())
            return action("JOIN GAME", FriendsLobbyActionKind::join_game, found->current_server_id);
        if (snapshot_.lobby && found->presence != "offline") {
            if (snapshot_.lobby->members.size() >= snapshot_.lobby->maximum_members)
                return {"LOBBY FULL", std::nullopt};
            return action("INVITE TO LOBBY", FriendsLobbyActionKind::invite_friend, found->id);
        }
        if (!snapshot_.lobby)
            return action("CREATE + INVITE", FriendsLobbyActionKind::create_lobby, found->id);
        return {"FRIEND OFFLINE", std::nullopt};
    }
    if (!snapshot_.lobby) {
        if (!selected_invitation_id_.empty())
            return action("ACCEPT INVITE", FriendsLobbyActionKind::accept_lobby_invite, selected_invitation_id_);
        return action("CREATE LOBBY", FriendsLobbyActionKind::create_lobby);
    }
    if (!snapshot_.lobby->server_id.empty() &&
        (snapshot_.lobby->state == "ready" || snapshot_.lobby->state == "in_game"))
        return action("JOIN GAME", FriendsLobbyActionKind::join_game, snapshot_.lobby->server_id);
    button.intent = FriendsLobbyIntent{FriendsLobbyActionKind::open_lobby, snapshot_.lobby->id, {}};
    if (busy()) button.intent.reset();
    return button;
}

FriendsLobbyButton FriendsLobbyMenuModel::secondary_button() const {
    FriendsLobbyButton button{"DECLINE", std::nullopt};
    const auto found = std::ranges::find(snapshot_.friends, selected_friend_id_, &FriendsLobbyFriend::id);
    if (found != snapshot_.friends.end()) {
        if (found->relationship == "pending") {
            const auto incoming = found->direction == "incoming";
            button = {incoming ? "DECLINE REQUEST" : "CANCEL REQUEST",
                FriendsLobbyIntent{incoming ? FriendsLobbyActionKind::decline_friend_request
                                            : FriendsLobbyActionKind::remove_friend, found->id, {}}};
        } else if (found->relationship == "accepted") {
            button = {"REMOVE FRIEND", FriendsLobbyIntent{FriendsLobbyActionKind::remove_friend, found->id, {}}};
        }
    } else if (!selected_invitation_id_.empty()) {
        button.intent = FriendsLobbyIntent{FriendsLobbyActionKind::decline_lobby_invite, selected_invitation_id_, {}};
    }
    if (!service_available_ || busy()) button.intent.reset();
    return button;
}

bool FriendsLobbyMenuModel::control_hovered(FriendsLobbyControl control) const noexcept {
    const auto kind = control == FriendsLobbyControl::search ? HitKind::search_button
                    : control == FriendsLobbyControl::primary ? HitKind::primary
                    : control == FriendsLobbyControl::secondary ? HitKind::secondary
                                                               : HitKind::back;
    return hovered_ && hovered_->kind == kind;
}

bool FriendsLobbyMenuModel::control_pressed(FriendsLobbyControl control) const noexcept {
    return control_hovered(control) && pressed_ == hovered_ && pressed_intent_.has_value();
}

void FriendsLobbyMenuModel::set_identity(std::string account_id) {
    if (account_id == local_account_id_) return;
    local_account_id_ = core::utf8_code_point_prefix(account_id, 64U);
    snapshot_ = {};
    authoritative_friends_.clear();
    search_results_.clear();
    search_.clear();
    tab_ = FriendsLobbyTab::friends;
    hovered_.reset();
    pressed_.reset();
    pressed_intent_.reset();
    search_focused_ = false;
    operation_.reset();
    selected_friend_id_.clear();
    selected_invitation_id_.clear();
    first_visible_friend_row_ = 0U;
    first_visible_invitation_row_ = 0U;
    error_.clear();
    phase_ = FriendsLobbyPhase::idle;
    rebuild_visible_rows();
}

void FriendsLobbyMenuModel::enter(std::chrono::steady_clock::time_point now) noexcept {
    entered_ = true;
    hovered_.reset();
    pressed_.reset();
    pressed_intent_.reset();
    search_focused_ = false;
    if (operation_.has_value() && now >= operation_->deadline) operation_.reset();
    update_phase_from_lobby();
    controls_armed_after_ = now + std::chrono::milliseconds{150};
}

void FriendsLobbyMenuModel::leave() noexcept {
    entered_ = false;
    operation_.reset();
    phase_ = FriendsLobbyPhase::idle;
    hovered_.reset();
    pressed_.reset();
    pressed_intent_.reset();
    search_focused_ = false;
}

void FriendsLobbyMenuModel::set_tab(FriendsLobbyTab tab) noexcept {
    if (tab == tab_) return;
    tab_ = tab;
    pressed_.reset();
    pressed_intent_.reset();
    selected_friend_id_.clear();
    selected_invitation_id_.clear();
    first_visible_friend_row_ = 0U;
    first_visible_invitation_row_ = 0U;
    rebuild_visible_rows();
}

void FriendsLobbyMenuModel::set_service_status(bool available, std::string status) {
    service_available_ = available;
    service_status_ = core::utf8_code_point_prefix(status, 128U);
    if (!available && entered_ && !operation_) {
        phase_ = FriendsLobbyPhase::reconnecting;
    } else if (available && phase_ == FriendsLobbyPhase::reconnecting) {
        update_phase_from_lobby();
    }
}

void FriendsLobbyMenuModel::apply_snapshot(FriendsLobbySnapshot snapshot,
                                           std::chrono::steady_clock::time_point now) {
    validate_rows(snapshot.friends, [](const auto& row) -> const std::string& { return row.id; });
    validate_rows(snapshot.invitations,
                  [](const auto& row) -> const std::string& { return row.id; });
    if (snapshot.lobby.has_value()) {
        validate_rows(snapshot.lobby->members,
                      [](const auto& row) -> const std::string& { return row.id; });
        snapshot.lobby->maximum_members =
            std::clamp<std::size_t>(snapshot.lobby->maximum_members, 1U, 64U);
        if (snapshot.lobby->name.empty()) snapshot.lobby->name = "REVIVAL LOBBY";
    }
    const auto converged = operation_.has_value() &&
                           operation_converged(*operation_, snapshot, local_account_id_);
    authoritative_friends_ = snapshot.friends;
    // Search is a discovery overlay. A new authoritative snapshot may have
    // removed an accepted/pending friendship returned by an earlier lookup.
    for (auto& result : search_results_) {
        if (std::ranges::find(authoritative_friends_, result.id,
                              &FriendsLobbyFriend::id) == authoritative_friends_.end()) {
            result.relationship = "search";
            result.direction.clear();
        }
    }
    snapshot_ = std::move(snapshot);
    merge_search_results();
    if (converged) {
        operation_.reset();
        error_.clear();
        controls_armed_after_ = now;
    }
    const auto retain_action_error = !converged &&
                                     phase_ == FriendsLobbyPhase::error &&
                                     !error_.empty();
    if (!retain_action_error) error_.clear();
    rebuild_visible_rows();
    stabilize_selection();
    if (!retain_action_error) update_phase_from_lobby();
}

void FriendsLobbyMenuModel::set_search_results(std::vector<FriendsLobbyFriend> results) {
    validate_rows(results, [](const auto& row) -> const std::string& { return row.id; });
    for (auto& row : results) {
        if (row.relationship.empty() || row.relationship == "none") {
            row.relationship = "search";
        }
    }
    search_results_ = std::move(results);
    tab_ = FriendsLobbyTab::friends;
    merge_search_results();
    first_visible_friend_row_ = 0U;
    selected_friend_id_.clear();
    selected_invitation_id_.clear();
    rebuild_visible_rows();
}

bool FriendsLobbyMenuModel::apply_search_results(
    std::uint64_t generation, std::string_view query,
    std::vector<FriendsLobbyFriend> results,
    std::chrono::steady_clock::time_point now) {
    if (!operation_ || operation_->generation != generation ||
        operation_->intent.kind != FriendsLobbyActionKind::search ||
        operation_->intent.text != query || search_ != query) return false;
    if (now >= operation_->deadline) {
        tick(now);
        return false;
    }
    set_search_results(std::move(results));
    return true;
}

void FriendsLobbyMenuModel::tick(std::chrono::steady_clock::time_point now) noexcept {
    if (!operation_.has_value() || now < operation_->deadline) return;
    const auto kind = operation_->intent.kind;
    operation_.reset();
    error_ = kind == FriendsLobbyActionKind::start_lobby
                 ? "The host did not answer. You can retry safely."
                 : "AoSPlay did not answer. Please retry.";
    phase_ = FriendsLobbyPhase::error;
    controls_armed_after_ = now + std::chrono::milliseconds{250};
}

bool FriendsLobbyMenuModel::append_search_text(std::string_view utf8) {
    if (!search_focused_ || busy() || utf8.empty() ||
        utf8.find('\0') != std::string_view::npos) {
        return false;
    }
    const auto prefix = core::utf8_code_point_prefix(
        std::string{search_} + std::string{utf8}, maximum_search_code_points);
    if (prefix == search_) return false;
    search_ = prefix;
    search_results_.clear();
    merge_search_results();
    error_.clear();
    rebuild_visible_rows();
    stabilize_selection();
    return true;
}

bool FriendsLobbyMenuModel::erase_search_code_point() noexcept {
    if (!search_focused_ || busy() || search_.empty()) return false;
    search_.resize(previous_utf8_boundary(search_));
    search_results_.clear();
    merge_search_results();
    error_.clear();
    rebuild_visible_rows();
    stabilize_selection();
    return true;
}

void FriendsLobbyMenuModel::clear_search() noexcept {
    search_.clear();
    search_results_.clear();
    merge_search_results();
    first_visible_friend_row_ = 0U;
    rebuild_visible_rows();
    stabilize_selection();
}

bool FriendsLobbyMenuModel::scroll_rows(std::int32_t rows) noexcept {
    return scroll_list(rows, tab_ == FriendsLobbyTab::invitations);
}

bool FriendsLobbyMenuModel::scroll_rows_at(std::int32_t rows,
                                          std::optional<ui::Point> point) noexcept {
    if (!point) return scroll_rows(rows);
    if (contains(layout_.lobby_panel, *point)) return scroll_list(rows, true);
    if (tab_ != FriendsLobbyTab::invitations && contains(layout_.friends_list, *point))
        return scroll_list(rows, false);
    return false;
}

bool FriendsLobbyMenuModel::scroll_list(std::int32_t rows, bool invitations) noexcept {
    auto& first = invitations
                      ? first_visible_invitation_row_
                      : first_visible_friend_row_;
    const auto count = invitations
                           ? visible_invitations_.size()
                           : visible_friends_.size();
    const auto page_rows = invitations ? visible_invitation_rows : visible_rows;
    const auto maximum = count > page_rows ? count - page_rows : 0U;
    const auto previous = first;
    if (rows < 0) {
        const auto distance = static_cast<std::size_t>(-static_cast<std::int64_t>(rows));
        first = distance > first ? 0U : first - distance;
    } else {
        first = (std::min)(maximum, first + static_cast<std::size_t>(rows));
    }
    return previous != first;
}

bool FriendsLobbyMenuModel::move_selection(std::int32_t direction) {
    if (direction == 0) return false;
    search_focused_ = false;
    pressed_.reset();
    pressed_intent_.reset();
    hovered_.reset();
    const bool invitations = tab_ == FriendsLobbyTab::invitations || !selected_invitation_id_.empty();
    const auto& visible = invitations ? visible_invitations_ : visible_friends_;
    if (visible.empty()) return false;
    auto& selection = invitations ? selected_invitation_id_ : selected_friend_id_;
    const auto id_at = [&](std::size_t row) -> const std::string& {
        return invitations ? snapshot_.invitations[visible[row]].id : snapshot_.friends[visible[row]].id;
    };
    auto current = visible.size();
    for (std::size_t row{}; row < visible.size(); ++row) {
        if (id_at(row) == selection) { current = row; break; }
    }
    const auto next = current == visible.size() ? (direction > 0 ? 0U : visible.size() - 1U)
        : static_cast<std::size_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(current) + direction, 0,
            static_cast<std::int64_t>(visible.size() - 1U)));
    const bool changed = selection != id_at(next);
    selection = id_at(next);
    if (invitations) selected_friend_id_.clear();
    else selected_invitation_id_.clear();
    auto& first = invitations ? first_visible_invitation_row_ : first_visible_friend_row_;
    const auto page_rows = invitations ? visible_invitation_rows : visible_rows;
    if (next < first) first = next;
    else if (next >= first + page_rows) first = next - page_rows + 1U;
    return changed;
}

bool FriendsLobbyMenuModel::cycle_tab(std::int32_t direction) noexcept {
    if (direction == 0) return false;
    constexpr std::int64_t count{3};
    const auto index = (static_cast<std::int64_t>(tab_) + direction % count + count) % count;
    const auto next = static_cast<FriendsLobbyTab>(index);
    const bool changed = next != tab_;
    search_focused_ = false;
    set_tab(next);
    return changed;
}

void FriendsLobbyMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? std::optional{hit_test(*point)} : std::nullopt;
}

void FriendsLobbyMenuModel::pointer_press(std::optional<ui::Point> point,
                                          std::chrono::steady_clock::time_point now) noexcept {
    pointer_move(point);
    pressed_ = hovered_;
    pressed_intent_ = pressed_ ? intent_for(*pressed_, now) : std::nullopt;
    if (!point.has_value() || now < controls_armed_after_) return;
    search_focused_ = contains(layout_.search_field, *point);
}

std::optional<FriendsLobbyIntent> FriendsLobbyMenuModel::pointer_release(
    std::optional<ui::Point> point,
    std::chrono::steady_clock::time_point now) noexcept {
    pointer_move(point);
    std::optional<FriendsLobbyIntent> result;
    if (pressed_.has_value() && hovered_ == pressed_ && now >= controls_armed_after_) {
        const auto hit = *pressed_;
        if (hit.kind >= HitKind::tab_friends && hit.kind <= HitKind::tab_invitations) {
            set_tab(static_cast<FriendsLobbyTab>(
                static_cast<std::uint8_t>(hit.kind) -
                static_cast<std::uint8_t>(HitKind::tab_friends)));
        } else if (hit.kind == HitKind::friend_row && hit.row < visible_friends_.size()) {
            const auto row = first_visible_friend_row_ + hit.row;
            if (row < visible_friends_.size() && intent_for(hit, now) == pressed_intent_) {
                selected_friend_id_ = snapshot_.friends[visible_friends_[row]].id;
                selected_invitation_id_.clear();
            }
        } else if (hit.kind == HitKind::invitation_row &&
                   first_visible_invitation_row_ + hit.row < visible_invitations_.size() &&
                   intent_for(hit, now) == pressed_intent_) {
            selected_invitation_id_ = snapshot_.invitations[
                visible_invitations_[first_visible_invitation_row_ + hit.row]].id;
            selected_friend_id_.clear();
        } else {
            result = intent_for(hit, now);
            if (result != pressed_intent_) result.reset();
        }
    }
    pressed_.reset();
    pressed_intent_.reset();
    return result;
}

std::optional<FriendsLobbyOperation> FriendsLobbyMenuModel::begin(
    FriendsLobbyIntent intent,
    std::chrono::steady_clock::time_point now) {
    if (operation_.has_value() || intent.kind == FriendsLobbyActionKind::back ||
        intent.kind == FriendsLobbyActionKind::open_lobby ||
        !service_available_) {
        return std::nullopt;
    }
    FriendsLobbyOperation operation;
    operation.generation = next_generation_++;
    operation.intent = std::move(intent);
    operation.deadline = now + timeout_for(operation.intent.kind);
    if (operation.intent.kind == FriendsLobbyActionKind::accept_lobby_invite) {
        const auto invitation = std::ranges::find(snapshot_.invitations,
            operation.intent.target_id, &FriendsLobbyInvitation::id);
        if (invitation != snapshot_.invitations.end())
            operation.expected_lobby_id = invitation->lobby_id;
    } else if (snapshot_.lobby) {
        operation.expected_lobby_id = snapshot_.lobby->id;
    }
    operation_ = operation;
    error_.clear();
    switch (operation.intent.kind) {
    case FriendsLobbyActionKind::start_lobby:
        phase_ = FriendsLobbyPhase::starting;
        break;
    case FriendsLobbyActionKind::join_game:
    case FriendsLobbyActionKind::join_friend_lobby:
    case FriendsLobbyActionKind::accept_lobby_invite:
        phase_ = FriendsLobbyPhase::joining;
        break;
    default:
        break;
    }
    return operation;
}

bool FriendsLobbyMenuModel::complete(std::uint64_t generation,
                                     bool success,
                                     std::string error,
                                     std::chrono::steady_clock::time_point now) noexcept {
    if (!operation_.has_value() || operation_->generation != generation) return false;
    if (now >= operation_->deadline) {
        tick(now);
        return false;
    }
    const auto kind = operation_->intent.kind;
    operation_.reset();
    controls_armed_after_ = now + std::chrono::milliseconds{250};
    if (!success) {
        error_ = core::utf8_code_point_prefix(error, 160U);
        if (error_.empty()) error_ = "The lobby action failed safely.";
        phase_ = FriendsLobbyPhase::error;
        return true;
    }
    error_.clear();
    if (kind == FriendsLobbyActionKind::start_lobby) {
        phase_ = FriendsLobbyPhase::waiting_for_host;
    } else {
        update_phase_from_lobby();
    }
    return true;
}

FriendsLobbyMenuModel::Hit FriendsLobbyMenuModel::hit_test(ui::Point point) const noexcept {
    const auto tab_width = layout_.tabs.width / 3;
    for (std::size_t index{}; index < 3U; ++index) {
        auto tab = layout_.tabs;
        tab.x += static_cast<std::int32_t>(index) * tab_width;
        tab.width = tab_width;
        if (contains(tab, point)) {
            return {static_cast<HitKind>(static_cast<std::uint8_t>(HitKind::tab_friends) +
                                        static_cast<std::uint8_t>(index)),
                    index};
        }
    }
    if (contains(layout_.search_field, point)) return {HitKind::search_field, 0U};
    if (contains(layout_.search_button, point)) return {HitKind::search_button, 0U};
    if (contains(layout_.friends_list, point)) {
        const auto row_height = layout_.friends_list.height /
                                static_cast<std::int32_t>(visible_rows);
        const auto row = static_cast<std::size_t>(
            std::max(0, (point.y - layout_.friends_list.y) / row_height));
        return {HitKind::friend_row, row};
    }
    const auto invitation_list = rect(414, 149, 318, 266);
    if (contains(invitation_list, point)) {
        const auto row_height = 38 * scale;
        const auto row = static_cast<std::size_t>(
            std::max(0, (point.y - invitation_list.y) / row_height));
        return {HitKind::invitation_row, row};
    }
    if (contains(layout_.primary_button, point)) return {HitKind::primary, 0U};
    if (contains(layout_.secondary_button, point)) return {HitKind::secondary, 0U};
    if (contains(layout_.back_button, point)) return {HitKind::back, 0U};
    return {};
}

std::optional<FriendsLobbyIntent> FriendsLobbyMenuModel::intent_for(
    Hit hit,
    std::chrono::steady_clock::time_point now) const {
    if (now < controls_armed_after_) return std::nullopt;
    switch (hit.kind) {
    case HitKind::tab_friends:
    case HitKind::tab_requests:
    case HitKind::tab_invitations:
    case HitKind::search_field:
        return std::nullopt;
    case HitKind::search_button:
        if (!service_available_ || search_.empty() || busy()) return std::nullopt;
        return FriendsLobbyIntent{FriendsLobbyActionKind::search, {}, search_};
    case HitKind::friend_row:
        if (first_visible_friend_row_ + hit.row >= visible_friends_.size()) return std::nullopt;
        return FriendsLobbyIntent{FriendsLobbyActionKind::invite_friend,
                                  snapshot_.friends[visible_friends_[
                                      first_visible_friend_row_ + hit.row]].id,
                                  {}};
    case HitKind::invitation_row:
        if (first_visible_invitation_row_ + hit.row >= visible_invitations_.size()) {
            return std::nullopt;
        }
        return FriendsLobbyIntent{FriendsLobbyActionKind::accept_lobby_invite,
                                  snapshot_.invitations[visible_invitations_[
                                      first_visible_invitation_row_ + hit.row]].id,
                                  {}};
    case HitKind::primary:
        return primary_button().intent;
    case HitKind::secondary:
        return secondary_button().intent;
    case HitKind::back:
        return FriendsLobbyIntent{FriendsLobbyActionKind::back, {}, {}};
    case HitKind::none:
        break;
    }
    return std::nullopt;
}

void FriendsLobbyMenuModel::rebuild_visible_rows() {
    visible_friends_.clear();
    visible_invitations_.clear();
    const auto query = lower_ascii(search_);
    for (std::size_t index{}; index < snapshot_.friends.size(); ++index) {
        const auto& row = snapshot_.friends[index];
        const auto request = row.relationship == "pending";
        const auto returned_by_search = std::ranges::any_of(
            search_results_, [&](const FriendsLobbyFriend& result) {
                return result.id == row.id;
            });
        if ((tab_ == FriendsLobbyTab::friends && request && !returned_by_search) ||
            (tab_ == FriendsLobbyTab::requests && !request) ||
            tab_ == FriendsLobbyTab::invitations) {
            continue;
        }
        // AoSPlay searches username, nickname and stable IDs. Re-filtering its
        // result by display nickname used to hide valid username matches (and
        // reintroduced case/Unicode discrepancies on the client).
        if (!query.empty() && !returned_by_search &&
            lower_ascii(row.name).find(query) == std::string::npos) {
            continue;
        }
        visible_friends_.push_back(index);
    }
    for (std::size_t index{}; index < snapshot_.invitations.size(); ++index) {
        visible_invitations_.push_back(index);
    }
    const auto friend_maximum = visible_friends_.size() > visible_rows
                                    ? visible_friends_.size() - visible_rows
                                    : 0U;
    const auto invitation_maximum = visible_invitations_.size() > visible_invitation_rows
                                        ? visible_invitations_.size() - visible_invitation_rows
                                        : 0U;
    first_visible_friend_row_ = (std::min)(first_visible_friend_row_, friend_maximum);
    first_visible_invitation_row_ =
        (std::min)(first_visible_invitation_row_, invitation_maximum);
}

void FriendsLobbyMenuModel::merge_search_results() {
    snapshot_.friends = authoritative_friends_;
    for (const auto& result : search_results_) {
        if (snapshot_.friends.size() >= maximum_rows) break;
        if (result.id == local_account_id_) continue;
        const auto existing = std::ranges::find(snapshot_.friends, result.id,
                                                &FriendsLobbyFriend::id);
        if (existing == snapshot_.friends.end()) snapshot_.friends.push_back(result);
    }
}

void FriendsLobbyMenuModel::stabilize_selection() {
    const auto friend_present = std::ranges::any_of(visible_friends_, [&](std::size_t index) {
        return snapshot_.friends[index].id == selected_friend_id_;
    });
    if (!friend_present) selected_friend_id_.clear();
    const auto invitation_present =
        std::ranges::any_of(visible_invitations_, [&](std::size_t index) {
            return snapshot_.invitations[index].id == selected_invitation_id_;
        });
    if (!invitation_present) selected_invitation_id_.clear();
}

void FriendsLobbyMenuModel::update_phase_from_lobby() noexcept {
    if (operation_.has_value()) return;
    if (!service_available_ && entered_) {
        phase_ = FriendsLobbyPhase::reconnecting;
        return;
    }
    if (!snapshot_.lobby.has_value()) {
        phase_ = FriendsLobbyPhase::idle;
    } else if (!snapshot_.lobby->server_id.empty()) {
        phase_ = FriendsLobbyPhase::idle;
    } else if (snapshot_.lobby->state == "starting" ||
               snapshot_.lobby->state == "waiting") {
        phase_ = FriendsLobbyPhase::waiting_for_host;
    } else {
        phase_ = FriendsLobbyPhase::idle;
    }
}

std::chrono::milliseconds FriendsLobbyMenuModel::timeout_for(
    FriendsLobbyActionKind kind) noexcept {
    switch (kind) {
    case FriendsLobbyActionKind::start_lobby:
        return std::chrono::seconds{30};
    case FriendsLobbyActionKind::join_game:
    case FriendsLobbyActionKind::join_friend_lobby:
    case FriendsLobbyActionKind::accept_lobby_invite:
        return std::chrono::seconds{20};
    default:
        return std::chrono::seconds{12};
    }
}

} // namespace battlespades::frontend
