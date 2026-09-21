#include "battlespades/world/weapon_presentation.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

namespace battlespades::world {
namespace {
bool safe_id(std::string_view value) {
    return !value.empty() && value.size() <= 100U &&
        std::ranges::all_of(value, [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
}
}
std::vector<WeaponPresentation> load_weapon_presentations(const std::filesystem::path& root) {
    std::vector<WeaponPresentation> result;
    const auto path = root.parent_path() / "client/cosmetics/weapon-presentation.json";
    std::error_code error;
    if (std::filesystem::file_size(path, error) > 128U * 1024U || error) return result;
    try {
        std::ifstream input{path};
        const auto data = nlohmann::json::parse(input);
        if (data.at("schema_version") != 1 || !data.at("weapons").is_array() ||
            data.at("weapons").size() > 100U) return result;
        std::set<std::string> ids;
        for (const auto& row : data.at("weapons")) {
            try {
                WeaponPresentation item;
                item.id = row.at("id");
                item.model_sha256 = row.at("model_sha256");
                if (!safe_id(item.id) || item.model_sha256.size() != 64U ||
                    !std::ranges::all_of(item.model_sha256, [](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                    }) || ids.contains(item.id)) continue;
                if (row.contains("sight") && !row.at("sight").is_null()) {
                    const auto& value = row.at("sight");
                    if (!value.at("rear").is_array() || value.at("rear").size() != 3U ||
                        !value.at("front").is_array() || value.at("front").size() != 3U) continue;
                    WeaponSightTags tags{value.at("rear").get<std::array<float,3U>>(),
                                         value.at("front").get<std::array<float,3U>>(),
                                         value.at("eye_distance").get<float>()};
                    const auto finite = [](const auto& point) {
                        return std::ranges::all_of(point, [](float n) { return std::isfinite(n) && std::abs(n) <= 512.0F; });
                    };
                    if (!finite(tags.rear) || !finite(tags.front) ||
                        !std::isfinite(tags.eye_distance) || tags.eye_distance < 0.2F || tags.eye_distance > 4.0F ||
                        tags.front[1] - tags.rear[1] < 1.0F) continue;
                    item.sight = tags;
                }
                item.fire_gain = row.value("fire_gain", 1.0F);
                if (!std::isfinite(item.fire_gain) || item.fire_gain < 0.1F || item.fire_gain > 1.5F) continue;
                item.fire_samples = row.value("fire_samples", std::vector<std::string>{});
                if (item.fire_samples.size() > 8U || !std::ranges::all_of(item.fire_samples, safe_id)) continue;
                if(row.contains("cues")){
                    if(!row["cues"].is_object()||row["cues"].size()>12U)continue;
                    bool valid=true;
                    for(const auto& [cue,values]:row["cues"].items()){
                        if(!safe_id(cue)||!values.is_array()){valid=false;break;}
                        auto samples=values.get<std::vector<std::string>>();
                        if(samples.size()>8U||!std::ranges::all_of(samples,safe_id)){valid=false;break;}
                        item.cues.emplace(cue,std::move(samples));
                    }if(!valid)continue;
                }
                ids.insert(item.id);
                result.push_back(std::move(item));
            } catch (const nlohmann::json::exception&) { /* Keep the other skins usable. */ }
        }
    } catch (const nlohmann::json::exception&) { }
    return result;
}
std::optional<WeaponPresentation> weapon_presentation(const std::filesystem::path& root, std::string_view id) {
    const auto items = load_weapon_presentations(root);
    const auto found = std::ranges::find(items, id, &WeaponPresentation::id);
    return found == items.end() ? std::nullopt : std::optional{*found};
}
std::array<float,3U> sight_tag_position(const WeaponSightTags& tags, std::array<float,3U> point, float scale) noexcept {
    // Build an orthonormal frame around the authored aiming line. This also
    // corrects pitch/yaw; moving a pivot alone cannot align two distinct points.
    std::array<float,3U> forward{tags.front[0]-tags.rear[0], tags.rear[2]-tags.front[2], tags.front[1]-tags.rear[1]};
    const float length = std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
    if (!std::isfinite(length) || length < 0.001F) return {};
    for (auto& value : forward) value /= length;
    const float horizontal = std::hypot(forward[0], forward[2]);
    if (horizontal < 0.001F) return {};
    const std::array<float,3U> right{forward[2]/horizontal,0.0F,-forward[0]/horizontal};
    const std::array<float,3U> up{forward[1]*right[2],forward[2]*right[0]-forward[0]*right[2],-forward[1]*right[0]};
    const std::array<float,3U> delta{point[0]-tags.rear[0],tags.rear[2]-point[2],point[1]-tags.rear[1]};
    const auto dot = [&](const auto& axis) { return (delta[0]*axis[0]+delta[1]*axis[1]+delta[2]*axis[2])*scale; };
    return {dot(right),dot(up),tags.eye_distance+dot(forward)};
}
} // namespace battlespades::world
