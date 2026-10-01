#include "battlespades/world/cosmetic_files.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace battlespades::world {
namespace {

using Substitutes = std::map<std::string, std::filesystem::path, std::less<>>;

bool safe_relative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return false;
    for (const auto& part : path)
        if (part == "..")
            return false;
    return true;
}

Substitutes load_substitutes(const std::filesystem::path& assets_root) {
    Substitutes result;
    std::ifstream input{assets_root / "client" / "cosmetics" / "retail-substitutes.json",
                        std::ios::binary};
    if (!input)
        return result;
    const auto document = nlohmann::json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("files") || !document["files"].is_object())
        return result;
    for (const auto& [key, value] : document["files"].items()) {
        if (!value.is_string())
            continue;
        const std::filesystem::path retail{value.get<std::string>()};
        if (key.starts_with("client/cosmetics/") && safe_relative(retail))
            result.emplace(key, retail);
    }
    return result;
}

const Substitutes& substitutes_for(const std::filesystem::path& assets_root) {
    static std::mutex mutex;
    static std::map<std::filesystem::path, Substitutes> cache;
    std::lock_guard lock{mutex};
    auto found = cache.find(assets_root);
    if (found == cache.end())
        found = cache.emplace(assets_root, load_substitutes(assets_root)).first;
    return found->second;
}

} // namespace

std::filesystem::path resolve_cosmetic_file(const std::filesystem::path& file) {
    std::error_code error;
    if (std::filesystem::exists(file, error) || error)
        return file;
    const auto absolute = std::filesystem::absolute(file, error).lexically_normal();
    if (error)
        return file;
    const std::vector<std::filesystem::path> parts(absolute.begin(), absolute.end());
    for (std::size_t i = 0; i + 1U < parts.size(); ++i) {
        if (parts[i] != "client" || parts[i + 1U] != "cosmetics")
            continue;
        std::filesystem::path assets_root, key;
        for (std::size_t j = 0; j < i; ++j)
            assets_root /= parts[j];
        for (std::size_t j = i; j < parts.size(); ++j)
            key /= parts[j];
        const auto& table = substitutes_for(assets_root);
        const auto substitute = table.find(key.generic_string());
        if (substitute == table.end())
            return file;
        auto retail = assets_root / "original" / substitute->second;
        return std::filesystem::exists(retail, error) && !error ? retail : file;
    }
    return file;
}

} // namespace battlespades::world
