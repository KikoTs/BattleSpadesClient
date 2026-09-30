#pragma once

#include "battlespades/world/class_selection.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/**
 * The player's last confirmed loadout of every class, kept across sessions.
 *
 * Retail keeps these in its config file as `loadout<N>` (tool ids) and
 * `prefabs<N>` (construct names) for each class id N (aoslib/config.py
 * loadout_name / prefab_name), written by SelectClass.create_loadout_list.
 * The file uses the same key names in one JSON object.
 *
 * What a saved entry may select is decided when it is read back, against the
 * rules of the server the player is joining (world::saved_row_indices,
 * world::default_class_constructs); the store only keeps well-formed data.
 */
class ClassLoadoutStore final {
public:
    using Loadouts = std::map<std::uint8_t, world::SavedClassLoadout>;

    explicit ClassLoadoutStore(std::filesystem::path path);

    /** Fail-closed: a missing or damaged file is an empty set. */
    [[nodiscard]] Loadouts load() const;

    /** Atomic replace where the platform permits it; never throws. */
    [[nodiscard]] bool save(const Loadouts& loadouts) const noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /** Parse the file's text. Malformed entries are dropped, not repaired. */
    [[nodiscard]] static Loadouts parse(std::string_view json);
    [[nodiscard]] static std::string serialize(const Loadouts& loadouts);

    /**
     * Merge one SelectClass visit into the saved set, as retail's
     * create_loadout_list does: the tool list always replaces the class's
     * entry, the construct list only when it is not empty. Returns whether
     * anything changed.
     */
    [[nodiscard]] static bool merge(Loadouts& saved, const Loadouts& visit);

private:
    std::filesystem::path path_;
};

} // namespace battlespades::frontend
