#include "battlespades/frontend/runtime_vfx_adapter.hpp"

#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::frontend {
namespace {

[[nodiscard]] std::uint32_t impact_coordinate(float value,
                                               std::uint32_t maximum) noexcept {
    return static_cast<std::uint32_t>(
        std::clamp<long>(std::lround(value), 0L, static_cast<long>(maximum)));
}

} // namespace

std::optional<world::TerrainImpactEvent> make_corpse_explosion_impact(
    const network::ExplodeCorpsePacket& packet,
    std::array<float, 3U> retained_position,
    world::VxlColor team_color) {
    if (!packet.show_explosion_effect) {
        return std::nullopt;
    }

    world::TerrainImpactEvent impact;
    impact.kind = world::TerrainImpactKind::corpse_explosion;
    impact.cell = {impact_coordinate(retained_position[0U], world::VxlMap::width - 1U),
                   impact_coordinate(retained_position[1U], world::VxlMap::depth - 1U),
                   impact_coordinate(retained_position[2U], world::VxlMap::height - 1U)};
    impact.color = team_color;
    impact.destroyed = true;
    impact.radius = 3.0F;
    impact.position = retained_position;
    return impact;
}

world::TerrainImpactEvent make_jetpack_death_airburst(
    std::array<float, 3U> retained_position) noexcept {
    world::TerrainImpactEvent impact;
    impact.kind = world::TerrainImpactKind::explosion;
    impact.cell = {impact_coordinate(retained_position[0U], world::VxlMap::width - 1U),
                   impact_coordinate(retained_position[1U], world::VxlMap::depth - 1U),
                   impact_coordinate(retained_position[2U], world::VxlMap::height - 1U)};
    impact.color = world::VxlColor{255U, 166U, 58U, 255U};
    impact.destroyed = false;
    impact.radius = 5.5F;
    impact.position = retained_position;
    return impact;
}

} // namespace battlespades::frontend
