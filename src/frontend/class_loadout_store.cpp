#include "battlespades/frontend/class_loadout_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace battlespades::frontend {
namespace {

// CLASS_NOOF is 19; the bound only keeps a damaged file from growing the map.
constexpr unsigned maximum_class_id{63U};
constexpr std::size_t maximum_tools{32U};
// Three constructs can be chosen; a little slack keeps hand-edited files usable.
constexpr std::size_t maximum_constructs{8U};
constexpr std::size_t maximum_file_bytes{64U * 1024U};
constexpr std::string_view loadout_prefix{"loadout"};
constexpr std::string_view prefabs_prefix{"prefabs"};

[[nodiscard]] bool parse_class_id(std::string_view text, std::uint8_t& class_id) noexcept {
    unsigned value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value > maximum_class_id) {
        return false;
    }
    class_id = static_cast<std::uint8_t>(value);
    return true;
}

[[nodiscard]] bool valid_construct(std::string_view name) noexcept {
    return name == world::flare_block_construct || world::valid_map_prefab_name(name);
}

} // namespace

ClassLoadoutStore::ClassLoadoutStore(std::filesystem::path path) : path_(std::move(path)) {}

ClassLoadoutStore::Loadouts ClassLoadoutStore::parse(std::string_view json) {
    Loadouts result;
    const auto document = nlohmann::json::parse(json, nullptr, false);
    if (!document.is_object()) return result;
    for (const auto& [key, value] : document.items()) {
        if (!value.is_array()) continue;
        std::uint8_t class_id{};
        if (key.starts_with(loadout_prefix) &&
            parse_class_id(std::string_view{key}.substr(loadout_prefix.size()), class_id)) {
            std::vector<std::uint8_t> tools;
            bool valid{true};
            for (const auto& item : value) {
                if (!item.is_number_unsigned() || item.get<std::uint64_t>() > 255U ||
                    tools.size() == maximum_tools) {
                    valid = false;
                    break;
                }
                const auto tool = static_cast<std::uint8_t>(item.get<std::uint64_t>());
                if (std::ranges::find(tools, tool) == tools.end()) tools.push_back(tool);
            }
            if (valid) result[class_id].loadout = std::move(tools);
        } else if (key.starts_with(prefabs_prefix) &&
                   parse_class_id(std::string_view{key}.substr(prefabs_prefix.size()),
                                  class_id)) {
            std::vector<std::string> constructs;
            bool valid{true};
            for (const auto& item : value) {
                if (!item.is_string() || constructs.size() == maximum_constructs) {
                    valid = false;
                    break;
                }
                auto name = item.get<std::string>();
                if (!valid_construct(name)) {
                    valid = false;
                    break;
                }
                if (std::ranges::find(constructs, name) == constructs.end()) {
                    constructs.push_back(std::move(name));
                }
            }
            if (valid) result[class_id].prefabs = std::move(constructs);
        }
    }
    return result;
}

std::string ClassLoadoutStore::serialize(const Loadouts& loadouts) {
    auto document = nlohmann::json::object();
    for (const auto& [class_id, saved] : loadouts) {
        if (class_id > maximum_class_id) continue;
        const auto suffix = std::to_string(static_cast<unsigned>(class_id));
        auto tools = nlohmann::json::array();
        for (const auto tool : saved.loadout) {
            if (tools.size() == maximum_tools) break;
            tools.push_back(static_cast<unsigned>(tool));
        }
        document[std::string{loadout_prefix} + suffix] = std::move(tools);
        auto constructs = nlohmann::json::array();
        for (const auto& name : saved.prefabs) {
            if (constructs.size() == maximum_constructs) break;
            if (valid_construct(name)) constructs.push_back(name);
        }
        document[std::string{prefabs_prefix} + suffix] = std::move(constructs);
    }
    return document.dump(2) + '\n';
}

bool ClassLoadoutStore::merge(Loadouts& saved, const Loadouts& visit) {
    bool changed{};
    for (const auto& [class_id, chosen] : visit) {
        if (class_id > maximum_class_id) continue;
        auto& entry = saved[class_id];
        if (entry.loadout != chosen.loadout) {
            entry.loadout = chosen.loadout;
            changed = true;
        }
        // selectClass.py: `if len(prefabs) > 0: setattr(config, prefab_name...)`.
        if (!chosen.prefabs.empty() && entry.prefabs != chosen.prefabs) {
            entry.prefabs = chosen.prefabs;
            changed = true;
        }
    }
    return changed;
}

ClassLoadoutStore::Loadouts ClassLoadoutStore::load() const {
    try {
        std::ifstream input{path_, std::ios::binary};
        if (!input) return {};
        std::string text;
        text.reserve(4096U);
        std::array<char, 4096U> buffer{};
        while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) ||
               input.gcount() > 0) {
            text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
            if (text.size() > maximum_file_bytes) return {};
        }
        return parse(text);
    } catch (...) {
        return {};
    }
}

bool ClassLoadoutStore::save(const Loadouts& loadouts) const noexcept {
    try {
        std::error_code error;
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path(), error);
            if (error) return false;
        }
        auto temporary = path_;
        temporary += ".tmp";
        {
            std::ofstream output{temporary, std::ios::trunc | std::ios::binary};
            if (!output) return false;
            output << serialize(loadouts);
            output.flush();
            if (!output) return false;
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

const std::filesystem::path& ClassLoadoutStore::path() const noexcept {
    return path_;
}

} // namespace battlespades::frontend
