#include "battlespades/render/texture_quality.hpp"

#include <array>
#include <vector>

namespace battlespades::render {
namespace {

[[nodiscard]] bool quality_component(const std::filesystem::path& value) {
    return value == "low" || value == "med" || value == "high";
}

} // namespace

std::filesystem::path texture_quality_asset(std::filesystem::path requested,
                                            TextureQualityTier quality) {
    if (requested.empty() || requested.is_absolute()) {
        return requested;
    }

    std::vector<std::filesystem::path> components;
    for (const auto& component : requested) {
        if (component == "..") {
            return requested;
        }
        if (component != "." && !component.empty()) {
            components.push_back(component);
        }
    }
    if (components.size() < 3U || components[0U] != "png" ||
        !quality_component(components[1U])) {
        return requested;
    }

    std::filesystem::path result{"png"};
    result /= texture_quality_directory(quality);
    for (std::size_t index{2U}; index < components.size(); ++index) {
        result /= components[index];
    }
    return result;
}

} // namespace battlespades::render
