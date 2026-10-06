#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/**
 * One retail achievement, from Steam's schema for application 224540.
 *
 * Steam marks every one of them as settable by game servers only, and the
 * retail server evaluated them, so the BattleSpades server does the same: it
 * tracks the rules and announces an unlock with the retail ACHIEVEMENT_GAINED
 * message, which names the achievement by `display_name`.
 */
struct AchievementDefinition final {
    /** Steam API name, the stable identifier (for example `spade_kill`). */
    std::string_view api_name;
    /** Localization stem: `<token>_DESC` is the description string id. */
    std::string_view token;
    std::string_view display_name;
    /** English description, the fallback when the string table has none. */
    std::string_view description;
    /** Backing counter, empty for one-shot achievements. */
    std::string_view stat;
    /** Counter value that unlocks it; zero for one-shot achievements. */
    std::uint32_t threshold{};
};

/** All 77, ordered by API name. */
[[nodiscard]] std::span<const AchievementDefinition> retail_achievement_definitions() noexcept;

[[nodiscard]] const AchievementDefinition* find_achievement(std::string_view api_name) noexcept;
/** The achievement an ACHIEVEMENT_GAINED message names; case-insensitive. */
[[nodiscard]] const AchievementDefinition*
find_achievement_by_display_name(std::string_view display_name) noexcept;

/** One achievement this player has unlocked, as the ledger stores it. */
struct UnlockedAchievement final {
    std::string api_name;
    /** Seconds since the Unix epoch when this client first saw the unlock. */
    std::int64_t unlocked_at{};
};

/**
 * This player's unlocked achievements, kept beside the settings.
 *
 * The server owns the rules and the lasting record; this is the client's own
 * copy, so an unlock earned in an offline Create Match is still remembered and
 * can be shown without asking anyone. Unknown names in the file are dropped;
 * a missing or damaged file is an empty ledger, never an error.
 */
class AchievementLedger final {
public:
    AchievementLedger() = default;
    explicit AchievementLedger(std::filesystem::path file);

    /** Records an unlock; false when it was already there or is not a retail achievement. */
    bool unlock(std::string_view api_name, std::int64_t unlocked_at);
    [[nodiscard]] bool unlocked(std::string_view api_name) const noexcept;
    [[nodiscard]] const std::vector<UnlockedAchievement>& entries() const noexcept { return entries_; }

    /** Writes the file atomically; false leaves the previous file in place. */
    bool save() const;

private:
    std::filesystem::path file_;
    std::vector<UnlockedAchievement> entries_;
};

}  // namespace battlespades::frontend
