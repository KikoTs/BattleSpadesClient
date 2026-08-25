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

} // namespace

FriendsLobbyMenuModel::FriendsLobbyMenuModel()
    : layout_{rect(38, 30, 724, 540),
              rect(250, 42, 300, 52),
              rect(56, 96, 340, 34),
              rect(56, 138, 286, 34),
              rect(346, 138, 50, 34),
              rect(56, 182, 340, 270),
              rect(404, 96, 338, 356),
              rect(404, 464, 162, 48),
              rect(580, 464, 162, 48),
              rect(40, 530, 120, 30)} {}

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

void FriendsLobbyMenuModel::set_identity(std::string account_id) {
    if (account_id == local_account_id_) return;
    local_account_id_ = core::utf8_code_point_prefix(account_id, 64U);
    snapshot_ = {};
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
    search_focused_ = false;
}

void FriendsLobbyMenuModel::set_tab(FriendsLobbyTab tab) noexcept {
    tab_ = tab;
    selected_friend_id_.clear();
    selected_invitation_id_.clear();
    first_visible_friend_row_ = 0U;
    first_visible_invitation_row_ = 0U;
    rebuild_visible_rows();
}

void FriendsLobbyMenuModel::set_service_status(bool available, std::string status) {
    service_available_ = available;
    service_status_ = core::utf8_code_point_prefix(status, 128U);
    if (!available && entered_ && !snapshot_.lobby.has_value()) {
        phase_ = FriendsLobbyPhase::reconnecting;
    } else if (available && phase_ == FriendsLobbyPhase::reconnecting) {
        update_phase_from_lobby();
    }
}

void FriendsLobbyMenuModel::apply_snapshot(FriendsLobbySnapshot snapshot,
                                           std::chrono::steady_clock::time_point now) {
    const auto retain_action_error = phase_ == FriendsLobbyPhase::error && !error_.empty();
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
    snapshot_ = std::move(snapshot);
    // A host may observe the published server in a poll before the Start POST
    // callback returns. That authoritative state completes Start, but must not
    // cancel an unrelated friend search/invite that happened concurrently.
    if (operation_.has_value() &&
        operation_->intent.kind == FriendsLobbyActionKind::start_lobby &&
        snapshot_.lobby.has_value() && !snapshot_.lobby->server_id.empty()) {
        operation_.reset();
    }
    if (!retain_action_error) error_.clear();
    rebuild_visible_rows();
    stabilize_selection();
    if (!retain_action_error) update_phase_from_lobby();
    if (!operation_.has_value()) controls_armed_after_ = now;
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
    error_.clear();
    rebuild_visible_rows();
    stabilize_selection();
    return true;
}

bool FriendsLobbyMenuModel::erase_search_code_point() noexcept {
    if (!search_focused_ || busy() || search_.empty()) return false;
    search_.resize(previous_utf8_boundary(search_));
    rebuild_visible_rows();
    stabilize_selection();
    return true;
}

void FriendsLobbyMenuModel::clear_search() noexcept {
    search_.clear();
    first_visible_friend_row_ = 0U;
    rebuild_visible_rows();
    stabilize_selection();
}

bool FriendsLobbyMenuModel::scroll_rows(std::int32_t rows) noexcept {
    auto& first = tab_ == FriendsLobbyTab::invitations
                      ? first_visible_invitation_row_
                      : first_visible_friend_row_;
    const auto count = tab_ == FriendsLobbyTab::invitations
                           ? visible_invitations_.size()
                           : visible_friends_.size();
    const auto maximum = count > visible_rows ? count - visible_rows : 0U;
    const auto previous = first;
    if (rows < 0) {
        const auto distance = static_cast<std::size_t>(-static_cast<std::int64_t>(rows));
        first = distance > first ? 0U : first - distance;
    } else {
        first = (std::min)(maximum, first + static_cast<std::size_t>(rows));
    }
    return previous != first;
}

void FriendsLobbyMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = point.has_value() ? std::optional{hit_test(*point)} : std::nullopt;
}

void FriendsLobbyMenuModel::pointer_press(std::optional<ui::Point> point,
                                          std::chrono::steady_clock::time_point now) noexcept {
    pointer_move(point);
    pressed_ = hovered_;
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
            if (row < visible_friends_.size()) {
                selected_friend_id_ = snapshot_.friends[visible_friends_[row]].id;
            }
        } else if (hit.kind == HitKind::invitation_row &&
                   first_visible_invitation_row_ + hit.row < visible_invitations_.size()) {
            selected_invitation_id_ = snapshot_.invitations[
                visible_invitations_[first_visible_invitation_row_ + hit.row]].id;
        } else {
            result = intent_for(hit, now);
        }
    }
    pressed_.reset();
    return result;
}

std::optional<FriendsLobbyOperation> FriendsLobbyMenuModel::begin(
    FriendsLobbyIntent intent,
    std::chrono::steady_clock::time_point now) {
    if (operation_.has_value() || intent.kind == FriendsLobbyActionKind::back ||
        !service_available_) {
        return std::nullopt;
    }
    FriendsLobbyOperation operation;
    operation.generation = next_generation_++;
    operation.intent = std::move(intent);
    operation.deadline = now + timeout_for(operation.intent.kind);
    operation_ = operation;
    error_.clear();
    switch (operation.intent.kind) {
    case FriendsLobbyActionKind::start_lobby:
        phase_ = FriendsLobbyPhase::starting;
        break;
    case FriendsLobbyActionKind::join_game:
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
    const auto kind = operation_->intent.kind;
    operation_.reset();
    controls_armed_after_ = now + std::chrono::milliseconds{750};
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
    if (contains(layout_.lobby_panel, point)) {
        const auto row_height = 38 * scale;
        const auto row = static_cast<std::size_t>(
            std::max(0, (point.y - layout_.lobby_panel.y - 48 * scale) / row_height));
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
        if (search_.empty() || busy()) return std::nullopt;
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
        if (busy()) return std::nullopt;
        if (!selected_friend_id_.empty()) {
            const auto found = std::ranges::find(snapshot_.friends, selected_friend_id_,
                                                  &FriendsLobbyFriend::id);
            if (found != snapshot_.friends.end() && found->relationship == "pending" &&
                found->direction == "incoming") {
                return FriendsLobbyIntent{FriendsLobbyActionKind::accept_friend_request,
                                          found->id, {}};
            }
            if (found != snapshot_.friends.end() && snapshot_.lobby.has_value() &&
                found->presence != "offline") {
                return FriendsLobbyIntent{FriendsLobbyActionKind::invite_friend, found->id, {}};
            }
        }
        if (!snapshot_.lobby.has_value()) {
            if (!selected_invitation_id_.empty()) {
                return FriendsLobbyIntent{FriendsLobbyActionKind::accept_lobby_invite,
                                          selected_invitation_id_, {}};
            }
            return FriendsLobbyIntent{FriendsLobbyActionKind::create_lobby, {}, {}};
        }
        // The retail Match Lobby owns roster, Start and Leave. This screen is
        // deliberately friends-only even while an AoSPlay lobby is active.
        return std::nullopt;
    case HitKind::secondary:
        if (busy()) return std::nullopt;
        if (!selected_friend_id_.empty()) {
            const auto found = std::ranges::find(snapshot_.friends, selected_friend_id_,
                                                  &FriendsLobbyFriend::id);
            if (found != snapshot_.friends.end() && found->relationship == "pending") {
                return FriendsLobbyIntent{FriendsLobbyActionKind::decline_friend_request,
                                          found->id, {}};
            }
            if (found != snapshot_.friends.end()) {
                return FriendsLobbyIntent{FriendsLobbyActionKind::remove_friend,
                                          found->id, {}};
            }
        }
        if (!selected_invitation_id_.empty()) {
            return FriendsLobbyIntent{FriendsLobbyActionKind::decline_lobby_invite,
                                      selected_invitation_id_, {}};
        }
        return std::nullopt;
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
        if ((tab_ == FriendsLobbyTab::friends && request) ||
            (tab_ == FriendsLobbyTab::requests && !request) ||
            tab_ == FriendsLobbyTab::invitations) {
            continue;
        }
        if (!query.empty() && lower_ascii(row.name).find(query) == std::string::npos) continue;
        visible_friends_.push_back(index);
    }
    for (std::size_t index{}; index < snapshot_.invitations.size(); ++index) {
        visible_invitations_.push_back(index);
    }
    const auto friend_maximum = visible_friends_.size() > visible_rows
                                    ? visible_friends_.size() - visible_rows
                                    : 0U;
    const auto invitation_maximum = visible_invitations_.size() > visible_rows
                                        ? visible_invitations_.size() - visible_rows
                                        : 0U;
    first_visible_friend_row_ = (std::min)(first_visible_friend_row_, friend_maximum);
    first_visible_invitation_row_ =
        (std::min)(first_visible_invitation_row_, invitation_maximum);
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
    case FriendsLobbyActionKind::accept_lobby_invite:
        return std::chrono::seconds{20};
    default:
        return std::chrono::seconds{12};
    }
}

} // namespace battlespades::frontend
