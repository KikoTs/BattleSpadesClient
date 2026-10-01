#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::platform {

/**
 * Steam Workshop map subscriptions, installed the way retail installed them.
 *
 * Retail's native shared.steam.pyd (2012) published a map as one legacy
 * ISteamRemoteStorage file, "<name>.aos", plus a separate "<name>.png"
 * preview. On download it split the .aos into two files under the subscribed
 * map directory, and wrote the preview beside them:
 *
 *   ugc/maps/Subscribed_<published file id>.vxl
 *   ugc/maps/Subscribed_<published file id>.ugc
 *   ugc/maps/Subscribed_<published file id>.png
 *
 * The id is the full 64-bit PublishedFileId_t ("%s/Subscribed_%llu.vxl").
 * The title shown in the map list comes from the .ugc sidecar's "title".
 *
 * This sync also writes Subscribed_<id>.txt, a byte copy of the .ugc: the
 * server reads a map's metadata from <map>.txt, and this client's Create
 * Match launcher requires .vxl, .txt and .ugc before it hosts a custom map.
 * Subscribed maps found in existing retail installs have the same four
 * files, with .txt identical to .ugc.
 *
 * Everything in this header is pure file logic so it can be tested without
 * Steam; the Steam side lives in steam_workshop.hpp.
 */

/** Steam Workshop application this client syncs; nothing else is ever asked for. */
inline constexpr std::uint32_t workshop_app_id{224540U};

/** "Subscribed_<id>", the stem every file of one subscribed map shares. */
[[nodiscard]] std::string workshop_subscribed_stem(std::uint64_t published_file_id);

/** Parses "Subscribed_<id>" (no extension); nullopt for anything else. */
[[nodiscard]] std::optional<std::uint64_t> parse_workshop_subscribed_stem(std::string_view stem);

/** The two payloads a retail .aos container carries. */
struct AosContainer final {
    std::vector<unsigned char> vxl;
    std::vector<unsigned char> ugc;
};

/**
 * Splits a retail .aos container.
 *
 * Retail (shared.steam.pyd sub_10003200 / sub_10001DF0) writes, then reads,
 * a sequence of chunks: a 4-byte NUL-terminated tag ("VXL\0" or "UGC\0"),
 * a 4-byte little-endian length, then that many bytes. Publishing writes VXL
 * first; the reader accepts either order and stops once it has both. An
 * unknown tag, a truncated chunk or a missing payload is an error here
 * (retail printed it and kept whatever it had already written).
 */
[[nodiscard]] std::optional<AosContainer> parse_aos_container(std::span<const unsigned char> bytes,
                                                              std::string& error);

/** Builds a container exactly as retail's publisher did (VXL, then UGC). */
[[nodiscard]] std::vector<unsigned char> build_aos_container(std::span<const unsigned char> vxl,
                                                             std::span<const unsigned char> ugc);

/**
 * True when `name` is a plain file name safe to join onto a directory.
 *
 * Steam reports item file names (a legacy item's m_pchFileName) that came from
 * whoever published it, so `..`, separators, drive letters, absolute paths,
 * control characters and reserved device names are refused.
 */
[[nodiscard]] bool workshop_safe_file_name(std::string_view name) noexcept;

/** What this sync wrote for one subscription. */
struct WorkshopIndexEntry final {
    /** SteamUGCDetails_t::m_rtimeUpdated of the version installed. */
    std::uint32_t time_updated{};
    /** File names (in the maps directory) this sync created for the item. */
    std::vector<std::string> files;
};

/**
 * The record of what the sync installed, so an item re-downloads only when
 * Steam reports a newer time_updated and only the sync's own files are ever
 * removed. Subscribed_* files that are not in the index (a retail download,
 * a hand-copied map) are never touched.
 */
struct WorkshopIndex final {
    std::map<std::uint64_t, WorkshopIndexEntry> items;
};

/** Index file kept in the maps directory; its leading dot keeps it out of stem lists. */
inline constexpr std::string_view workshop_index_file_name{".battlespades_workshop_index"};

/** Reads the index; a missing or unreadable index is empty (nothing will be removed). */
[[nodiscard]] WorkshopIndex load_workshop_index(const std::filesystem::path& file);
/** Writes the index atomically. */
[[nodiscard]] bool save_workshop_index(const std::filesystem::path& file, const WorkshopIndex& index,
                                       std::string& error);
/** Text form of the index, exposed for tests. */
[[nodiscard]] std::string serialize_workshop_index(const WorkshopIndex& index);
[[nodiscard]] WorkshopIndex parse_workshop_index(std::string_view text);

/** One subscription as Steam reports it, reduced to what planning needs. */
struct WorkshopSubscribedItem final {
    std::uint64_t published_file_id{};
    std::uint32_t time_updated{};
};

struct WorkshopSyncPlan final {
    /** Not installed by this sync yet. */
    std::vector<std::uint64_t> install;
    /** Installed, but Steam has a newer version or a file went missing. */
    std::vector<std::uint64_t> update;
    /** Installed and current. */
    std::vector<std::uint64_t> keep;
    /** In the index but no longer subscribed: remove its recorded files. */
    std::vector<std::uint64_t> remove;
};

/**
 * Decides what to do. `installed_files_present(id)` reports whether every
 * file the index recorded for `id` is still on disk; an item whose files went
 * missing is re-installed even when its time_updated is unchanged.
 */
template <typename FilesPresent>
[[nodiscard]] WorkshopSyncPlan plan_workshop_sync(const WorkshopIndex& index,
                                                  std::span<const WorkshopSubscribedItem> subscribed,
                                                  FilesPresent&& installed_files_present) {
    WorkshopSyncPlan plan;
    std::map<std::uint64_t, bool> seen;
    for (const auto& item : subscribed) {
        if (item.published_file_id == 0U || seen.contains(item.published_file_id)) continue;
        seen.emplace(item.published_file_id, true);
        const auto found = index.items.find(item.published_file_id);
        if (found == index.items.end()) {
            plan.install.push_back(item.published_file_id);
        } else if (item.time_updated > found->second.time_updated ||
                   !installed_files_present(item.published_file_id)) {
            plan.update.push_back(item.published_file_id);
        } else {
            plan.keep.push_back(item.published_file_id);
        }
    }
    for (const auto& [id, entry] : index.items) {
        if (!seen.contains(id)) plan.remove.push_back(id);
    }
    return plan;
}

/** True when every file `entry` lists exists as a regular file in `maps_directory`. */
[[nodiscard]] bool workshop_entry_files_present(const std::filesystem::path& maps_directory,
                                                const WorkshopIndexEntry& entry);

/** Writes `bytes` to `target` through a temporary sibling and a rename. */
[[nodiscard]] bool write_file_atomically(const std::filesystem::path& target,
                                         std::span<const unsigned char> bytes, std::string& error);

/**
 * Installs one subscription from its downloaded .aos bytes (and preview, when
 * Steam had one). Returns the files written, for the index; nothing is
 * written unless the container parses.
 */
[[nodiscard]] std::optional<WorkshopIndexEntry> install_workshop_map(
    const std::filesystem::path& maps_directory, std::uint64_t published_file_id,
    std::uint32_t time_updated, std::span<const unsigned char> aos_bytes,
    std::span<const unsigned char> preview_png, std::string& error);

/**
 * Removes what the index recorded for `published_file_id`, but only names of
 * the form Subscribed_<that id>.<ext> that are safe plain file names; a
 * tampered index cannot reach any other file. Returns how many were removed.
 */
std::size_t remove_workshop_map(const std::filesystem::path& maps_directory,
                                std::uint64_t published_file_id, const WorkshopIndexEntry& entry);

/** Reads a whole file, refusing anything over `maximum_bytes`. */
[[nodiscard]] std::optional<std::vector<unsigned char>> read_workshop_file(
    const std::filesystem::path& file, std::uintmax_t maximum_bytes, std::string& error);

/** Largest .aos the sync accepts (a 512x512x64 map plus sidecar is ~10 MiB). */
inline constexpr std::uintmax_t maximum_workshop_item_bytes{64U * 1024U * 1024U};

} // namespace battlespades::platform
