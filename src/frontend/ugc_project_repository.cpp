#include "battlespades/frontend/ugc_project_repository.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <ranges>
#include <set>
#include <string_view>

namespace battlespades::frontend {
namespace {

constexpr std::uintmax_t maximum_sidecar_bytes{1U << 20U};
constexpr std::uint64_t unpublished_handle{18'446'744'073'709'551'615ULL};

struct ModeDefinition final {
    std::string_view id;
    std::string_view label;
};

constexpr std::array modes{
    ModeDefinition{"zom", "ZOMBIE_MODE_TITLE"},
    ModeDefinition{"tdm", "TDM_TITLE"},
    ModeDefinition{"dia", "DIAMOND_MINE_TITLE"},
    ModeDefinition{"oc", "OCCUPATION_MODE_TITLE"},
    ModeDefinition{"dem", "DEMOLITION_TITLE"},
    ModeDefinition{"mh", "MULTIHILL_TITLE"},
    ModeDefinition{"vip", "VIP_MODE_TITLE"},
    ModeDefinition{"ctf", "CTF_TITLE"},
    ModeDefinition{"tc", "TC_TITLE"},
};

[[nodiscard]] std::string bounded_json_string(const nlohmann::json& document,
                                              std::string_view key,
                                              std::size_t maximum = 512U) {
    const auto found = document.find(key);
    if (found == document.end() || !found->is_string()) return {};
    auto value = found->get<std::string>();
    if (value.size() > maximum) value.resize(maximum);
    return value;
}

[[nodiscard]] std::set<std::string, std::less<>> project_tags(
    const nlohmann::json& document) {
    std::set<std::string, std::less<>> output;
    const auto found = document.find("tags");
    if (found == document.end() || !found->is_array()) return output;
    for (const auto& value : *found) {
        if (!value.is_string()) continue;
        auto tag = value.get<std::string>();
        if (tag.size() <= 64U) output.insert(std::move(tag));
    }
    return output;
}

[[nodiscard]] std::uint64_t project_handle(const nlohmann::json& document) noexcept {
    try {
        const auto found = document.find("aos_ugc_handle");
        return found != document.end() && found->is_number_unsigned()
                   ? found->get<std::uint64_t>()
                   : unpublished_handle;
    } catch (const nlohmann::json::exception&) {
        return unpublished_handle;
    }
}

[[nodiscard]] bool project_modified(const nlohmann::json& document) noexcept {
    try {
        const auto found = document.find("modified_since_publish");
        return found != document.end() && found->is_boolean() && found->get<bool>();
    } catch (const nlohmann::json::exception&) {
        return true;
    }
}

[[nodiscard]] bool safe_uid(std::string_view uid) noexcept {
    if (uid.empty() || uid.size() > 255U || uid == "." || uid == "..") return false;
    const std::filesystem::path path{uid};
    return !path.has_parent_path() && path.filename() == path &&
           path.extension() == ".ugc" &&
           uid.find_first_of("/\\\0") == std::string_view::npos;
}

} // namespace

namespace {
[[nodiscard]] std::optional<bool> revival_publication_current(const std::filesystem::path& sidecar) {
    auto path = sidecar;
    path += ".publication.json";
    std::error_code error;
    if (std::filesystem::is_symlink(path, error) || !std::filesystem::is_regular_file(path, error) ||
        std::filesystem::file_size(path, error) > 16384U || error) return std::nullopt;
    try {
        std::ifstream input{path, std::ios::binary};
        const auto receipt = nlohmann::json::parse(input);
        const auto& files = receipt.at("files");
        if (!files.is_array() || files.size() < 3U || files.size() > 4U) return std::nullopt;
        std::set<std::string> expected;
        std::set<std::string> allowed;
        for (const auto extension : {".ugc", ".vxl", ".txt", ".png"}) {
            auto sibling = sidecar;
            sibling.replace_extension(extension);
            allowed.insert(sibling.filename().string());
            if (std::string_view{extension} != ".png" || std::filesystem::exists(sibling))
                expected.insert(sibling.filename().string());
        }
        bool current = true;
        for (const auto& file : files) {
            const auto name = file.at("filename").get<std::string>();
            if (allowed.erase(name) != 1U) return std::nullopt;
            expected.erase(name);
            const auto sibling = sidecar.parent_path() / name;
            if (!std::filesystem::is_regular_file(sibling) || std::filesystem::is_symlink(sibling) ||
                std::filesystem::file_size(sibling) != file.at("size").get<std::uintmax_t>() ||
                std::to_string(std::filesystem::last_write_time(sibling).time_since_epoch().count()) !=
                    file.at("modified_ticks").get<std::string>()) current = false;
        }
        return expected.empty() && current;
    } catch (...) { return std::nullopt; }
}
} // namespace

UgcProjectScanResult scan_ugc_projects(const std::filesystem::path& maps_root) {
    UgcProjectScanResult result;
    std::error_code code;
    if (!std::filesystem::exists(maps_root, code)) {
        if (!code) std::filesystem::create_directories(maps_root, code);
        if (code) result.warnings.push_back("cannot create UGC map catalog: " + code.message());
        return result;
    }
    if (!std::filesystem::is_directory(maps_root, code) || code) {
        result.warnings.push_back("UGC map catalog is not a directory");
        return result;
    }

    constexpr std::size_t maximum_projects{4'096U};
    for (std::filesystem::directory_iterator iterator{maps_root, code}, end;
         !code && iterator != end && result.maps.size() < maximum_projects;
         iterator.increment(code)) {
        const auto& entry = *iterator;
        if (entry.is_symlink(code)) {
            result.warnings.push_back(entry.path().filename().string() + ": linked projects are not supported");
            continue;
        }
        if (!entry.is_regular_file(code) || code || entry.path().extension() != ".ugc") {
            code.clear();
            continue;
        }
        const auto size = entry.file_size(code);
        if (code || size == 0U || size > maximum_sidecar_bytes) {
            result.warnings.push_back(entry.path().filename().string() +
                                      ": sidecar is empty or exceeds 1 MiB");
            code.clear();
            continue;
        }
        try {
            std::ifstream stream{entry.path(), std::ios::binary};
            if (!stream) throw std::runtime_error{"cannot open sidecar"};
            const auto document = nlohmann::json::parse(stream);
            if (!document.is_object()) throw std::runtime_error{"root must be an object"};

            UgcLocalMapRecord map;
            map.uid = entry.path().filename().string();
            map.title = bounded_json_string(document, "title", 200U);
            if (map.title.empty()) map.title = entry.path().stem().string();
            auto vxl = entry.path();
            vxl.replace_extension(".vxl");
            auto preview = entry.path();
            preview.replace_extension(".png");
            const bool has_vxl = std::filesystem::is_regular_file(vxl, code) && !code;
            code.clear();
            if (std::filesystem::is_regular_file(preview, code) && !code) {
                map.preview_asset = preview.string();
            }
            code.clear();

            const auto tags = project_tags(document);
            for (const auto& mode : modes) {
                if (!tags.contains(mode.id)) continue;
                map.modes.push_back(UgcPublishModeStatus{
                    std::string{mode.id},
                    std::string{mode.label},
                    has_vxl,
                    has_vxl ? std::string{} : std::string{"UGC_MAP_DATA_REQUIRED"},
                });
            }
            if (map.modes.empty()) {
                map.modes.push_back(UgcPublishModeStatus{
                    {}, "MODE", false, "UGC_MAP_DATA_REQUIRED"});
            }
            const auto handle = project_handle(document);
            map.state = !has_vxl
                            ? UgcLocalMapState::data_required
                        : handle == unpublished_handle
                            ? UgcLocalMapState::unpublished
                        : project_modified(document)
                            ? UgcLocalMapState::changed_since_publish
                            : UgcLocalMapState::published;
            if (has_vxl) {
                if (const auto current = revival_publication_current(entry.path()))
                    map.state = *current ? UgcLocalMapState::published : UgcLocalMapState::changed_since_publish;
            }
            result.maps.push_back(std::move(map));
        } catch (const std::exception& exception) {
            result.warnings.push_back(entry.path().filename().string() + ": " + exception.what());
        }
    }
    if (code) result.warnings.push_back("UGC catalog scan stopped: " + code.message());
    std::ranges::sort(result.maps, [](const auto& left, const auto& right) {
        return left.title < right.title;
    });
    return result;
}

bool delete_ugc_project(const std::filesystem::path& maps_root,
                        std::string_view uid,
                        std::string& error) noexcept {
    error.clear();
    try {
        if (!safe_uid(uid)) {
            error = "invalid UGC project identifier";
            return false;
        }
        std::error_code code;
        const auto canonical_root = std::filesystem::weakly_canonical(maps_root, code);
        if (code || canonical_root.empty()) {
            error = "cannot resolve UGC catalog root";
            return false;
        }
        const auto sidecar = canonical_root / std::filesystem::path{uid};
        if (std::filesystem::is_symlink(sidecar, code) || code ||
            !std::filesystem::is_regular_file(sidecar, code) || code) {
            error = "UGC sidecar does not exist or is not a regular file";
            return false;
        }
        constexpr std::array extensions{
            std::string_view{".ugc"}, std::string_view{".vxl"},
            std::string_view{".txt"}, std::string_view{".png"},
            std::string_view{".ugc.publication.json"},
        };
        bool removed{};
        for (const auto extension : extensions) {
            auto path = sidecar;
            path.replace_extension(extension);
            if (std::filesystem::is_symlink(path, code)) {
                error = "UGC project contains a symlink";
                return false;
            }
            code.clear();
            removed = std::filesystem::remove(path, code) || removed;
            if (code) {
                error = "cannot delete UGC project: " + code.message();
                return false;
            }
        }
        if (!removed) error = "UGC project did not contain removable files";
        return removed;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace battlespades::frontend
