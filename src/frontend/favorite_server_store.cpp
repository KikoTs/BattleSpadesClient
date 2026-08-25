#include "battlespades/frontend/favorite_server_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <system_error>
#include <utility>

namespace battlespades::frontend {
namespace {

[[nodiscard]] std::string trim_ascii(std::string value) {
    constexpr std::string_view whitespace{" \t\r\n"};
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1U);
}

} // namespace

FavoriteServerStore::FavoriteServerStore(std::filesystem::path path) : path_(std::move(path)) {}

std::set<std::string, std::less<>> FavoriteServerStore::load() const {
    std::set<std::string, std::less<>> result;
    std::ifstream input{path_};
    if (!input)
        return result;

    std::string line;
    while (std::getline(input, line)) {
        line = trim_ascii(std::move(line));
        if (line.empty() || line.front() == '#')
            continue;
        if (valid_identifier(line))
            result.insert(std::move(line));
    }
    return result;
}

bool FavoriteServerStore::save(
    const std::set<std::string, std::less<>>& identifiers) const noexcept {
    try {
        std::error_code error;
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path(), error);
            if (error)
                return false;
        }

        auto temporary = path_;
        temporary += ".tmp";
        {
            std::ofstream output{temporary, std::ios::trunc};
            if (!output)
                return false;
            output << "# BattleSpades favourite servers v1\n";
            for (const auto& identifier : identifiers) {
                if (valid_identifier(identifier))
                    output << identifier << '\n';
            }
            output.flush();
            if (!output)
                return false;
        }

        // Windows rename cannot replace an existing file. Remove only the
        // exact target after the complete temporary file has been flushed.
        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::rename(temporary, path_, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

const std::filesystem::path& FavoriteServerStore::path() const noexcept {
    return path_;
}

bool FavoriteServerStore::valid_identifier(std::string_view identifier) noexcept {
    constexpr std::string_view prefix{"aos://"};
    if (!identifier.starts_with(prefix) || identifier.size() > 320U)
        return false;
    const auto endpoint = identifier.substr(prefix.size());
    const auto separator = endpoint.rfind(':');
    if (separator == std::string_view::npos || separator == 0U ||
        separator + 1U >= endpoint.size()) {
        return false;
    }
    if (!std::all_of(endpoint.begin(),
                     endpoint.begin() + static_cast<std::ptrdiff_t>(separator),
                     [](char value) {
                         const auto byte = static_cast<unsigned char>(value);
                         return std::isalnum(byte) != 0 || value == '.' || value == '-';
                     })) {
        return false;
    }
    unsigned port{};
    for (const char value : endpoint.substr(separator + 1U)) {
        if (value < '0' || value > '9')
            return false;
        port = port * 10U + static_cast<unsigned>(value - '0');
        if (port > 65'535U)
            return false;
    }
    return port != 0U;
}

} // namespace battlespades::frontend
