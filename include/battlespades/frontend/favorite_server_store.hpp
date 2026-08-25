#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/**
 * Durable replacement for Steam's retail favourite-server set.
 *
 * The store owns only canonical `aos://host:port` identifiers. Discovery and
 * A2S querying remain outside this class, so loading favourites never blocks a
 * network worker and malformed disk data cannot manufacture a server row.
 */
class FavoriteServerStore final {
public:
    explicit FavoriteServerStore(std::filesystem::path path);

    /** Load a fail-closed, deduplicated set. Missing files are an empty set. */
    [[nodiscard]] std::set<std::string, std::less<>> load() const;

    /**
     * Atomically replace the on-disk set where the platform permits it.
     * Returns false on any I/O error and leaves no exception crossing the UI
     * event boundary.
     */
    [[nodiscard]] bool save(const std::set<std::string, std::less<>>& identifiers) const noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    /** Syntactic guard shared by load(), save(), and direct-IP Add. */
    [[nodiscard]] static bool valid_identifier(std::string_view identifier) noexcept;

private:
    std::filesystem::path path_;
};

} // namespace battlespades::frontend
