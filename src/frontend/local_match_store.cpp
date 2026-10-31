#include "battlespades/frontend/local_match_store.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace battlespades::frontend {
namespace {
constexpr std::uintmax_t maximum_bytes{64U * 1024U};

bool safe_map_name(std::string_view name) {
    return !name.empty() && name.size() <= 128U && name != "." && name != ".." &&
           name.find_first_of("/\\:") == std::string_view::npos &&
           std::ranges::none_of(name, [](unsigned char c) { return c < 0x20U; });
}
} // namespace

std::optional<CreateMatchConfiguration> load_local_match_configuration(
    const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::error_code code;
    if (!std::filesystem::exists(path, code) && !code) return std::nullopt;
    const auto size = std::filesystem::file_size(path, code);
    if (code || size > maximum_bytes) {
        error = "Cannot read saved local match settings (missing, inaccessible or too large).";
        return std::nullopt;
    }
    try {
        std::ifstream input{path, std::ios::binary};
        const auto document = nlohmann::json::parse(input);
        if (!document.is_object() || document.at("version") != 1)
            throw std::runtime_error("unsupported format");
        CreateMatchConfiguration candidate;
        const auto number = [&](const char* key) {
            const auto& value = document.at(key);
            if (!value.is_number_integer() || value.get<std::int64_t>() < 0 ||
                value.get<std::uint64_t>() > 65535U)
                throw std::runtime_error("invalid number");
            return value.get<std::uint16_t>();
        };
        const auto privacy = number("privacy");
        if (privacy > static_cast<std::uint16_t>(CreateMatchPrivacy::open))
            throw std::runtime_error("invalid privacy");
        candidate.privacy = static_cast<CreateMatchPrivacy>(privacy);
        candidate.retail_playlist_id = number("playlist");
        candidate.max_players = number("max_players");
        candidate.match_minutes = number("match_minutes");
        candidate.bot_count = number("bot_count");
        candidate.server_port = number("server_port");
        candidate.bot_difficulty = document.at("bot_difficulty").get<std::string>();
        candidate.map_name = document.at("map_name").get<std::string>();
        candidate.custom_map = document.value("custom_map", false);
        candidate.subscribed_map = document.value("subscribed_map", false);
        candidate.map_title = document.value("map_title", std::string{});
        candidate.map_author = document.value("map_author", std::string{});
        candidate.rule_overrides = document.at("rules").get<decltype(candidate.rule_overrides)>();
        if (!safe_map_name(candidate.map_name) || candidate.map_title.size() > 512U ||
            candidate.map_author.size() > 512U || candidate.rule_overrides.size() > 256U)
            throw std::runtime_error("invalid map or rules");
        // Reuse the lobby's mode, count, difficulty and rule validation. A
        // custom map is admitted later against the user's actual map catalog.
        auto normalized = CreateMatchMenuModel{candidate}.configuration();
        if (candidate.custom_map) {
            normalized.map_name = candidate.map_name;
            normalized.custom_map = true;
            normalized.subscribed_map = candidate.subscribed_map;
            normalized.map_title = candidate.map_title;
            normalized.map_author = candidate.map_author;
        }
        return normalized;
    } catch (const std::exception& failure) {
        error = "Cannot load saved local match settings: " + std::string{failure.what()};
        return std::nullopt;
    }
}

bool save_local_match_configuration(const std::filesystem::path& path,
                                    const CreateMatchConfiguration& value, std::string& error) {
    error.clear();
    static std::atomic<std::uint64_t> sequence{};
    auto temporary = path;
    const auto process_id =
#if defined(_WIN32)
        GetCurrentProcessId();
#else
        getpid();
#endif
    temporary += ".tmp." + std::to_string(process_id) + '.' + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + '.' +
        std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));
    try {
        const nlohmann::json document{
            {"version", 1}, {"privacy", static_cast<unsigned>(value.privacy)},
            {"playlist", value.retail_playlist_id}, {"max_players", value.max_players},
            {"match_minutes", value.match_minutes}, {"bot_count", value.bot_count},
            {"bot_difficulty", value.bot_difficulty}, {"server_port", value.server_port},
            {"map_name", value.map_name}, {"custom_map", value.custom_map},
            {"subscribed_map", value.subscribed_map}, {"map_title", value.map_title},
            {"map_author", value.map_author}, {"rules", value.rule_overrides}};
        const auto text = document.dump(2) + "\n";
        if (text.size() > maximum_bytes || !safe_map_name(value.map_name))
            throw std::runtime_error("invalid or oversized settings");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            if (!output) throw std::runtime_error("cannot write temporary settings");
            output.close();
            if (!output) throw std::runtime_error("cannot close temporary settings");
        }
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot replace saved settings");
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (const std::exception& failure) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = "Cannot save local match settings: " + std::string{failure.what()};
        return false;
    }
}
} // namespace battlespades::frontend
