#include "battlespades/world/entity_attachment.hpp"
#include "battlespades/world/local_entity.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error{message};
}

[[nodiscard]] bool near(double left, double right, double tolerance = 1e-9) {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool near(Vec3 left, Vec3 right, double tolerance = 1e-9) {
    return near(left.x, right.x, tolerance) && near(left.y, right.y, tolerance) &&
           near(left.z, right.z, tolerance);
}

[[nodiscard]] std::shared_ptr<VxlMap> platform_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{}; column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic world must parse");
    for (std::uint32_t y{60U}; y <= 90U; ++y) {
        for (std::uint32_t x{110U}; x <= 150U; ++x) {
            expect(loaded.map->set_voxel(x, y, 233U, VxlColor{100U, 120U, 90U, 255U}),
                   "platform voxel must be accepted");
        }
    }
    return std::make_shared<VxlMap>(std::move(*loaded.map));
}

void yaw_follows_to_pitch_yaw() {
    expect(near(retail_yaw_degrees({0.0, 1.0, 0.0}), 0.0), "+y is yaw 0");
    expect(near(retail_yaw_degrees({1.0, 0.0, 0.0}), 90.0), "+x is yaw 90");
    expect(near(retail_yaw_degrees({0.0, -1.0, 0.0}), 180.0), "-y is yaw 180");
    expect(near(retail_yaw_degrees({-1.0, 0.0, 0.0}), -90.0), "-x is yaw -90");
    expect(near(retail_yaw_degrees({0.0, 0.0, 1.0}), 0.0), "a vertical vector has yaw 0");
}

void set_target_stores_the_offsets() {
    const Vec3 player{100.0, 50.0, 200.0};
    const Vec3 grenade{100.0, 51.0, 201.0}; // one block along +y, one block lower
    const auto attachment = attach_entity_to_player(7U, grenade, player, 0.0);
    expect(attachment.player_id == 7U, "the player id is kept");
    expect(near(attachment.horizontal_offset, 1.0), "horiz_offset is the flat distance");
    expect(near(attachment.vertical_offset, 1.5), "vert_offset is offset.z + 0.5");
    expect(near(attachment.yaw_offset_degrees, 0.0),
           "a grenade straight ahead of a yaw-0 player has no yaw offset");

    const auto side = attach_entity_to_player(7U, {101.0, 50.0, 200.0}, player, 0.0);
    expect(near(side.yaw_offset_degrees, 90.0) && near(side.vertical_offset, 0.5),
           "a grenade on the +x side sits 90 degrees round");

    const auto turned = attach_entity_to_player(7U, {101.0, 50.0, 200.0}, player, 90.0);
    expect(near(turned.yaw_offset_degrees, 0.0),
           "the offset is relative to the player's own yaw");

    const auto centre = attach_entity_to_player(7U, player, player, 45.0);
    expect(near(centre.horizontal_offset, 0.0) && near(centre.yaw_offset_degrees, -45.0),
           "a grenade at the player's centre has no flat offset");
    expect(near(attached_entity_position(centre, player, 123.0), {100.0, 50.0, 200.5}),
           "and stays at the centre whatever the yaw");
}

void update_carries_the_grenade_with_the_player() {
    const Vec3 player{100.0, 50.0, 200.0};
    const auto attachment = attach_entity_to_player(3U, {100.0, 51.0, 200.2}, player, 0.0);

    // Retail's own quirk: the first update already puts the grenade 0.5 lower
    // than where it stuck (vert_offset = offset.z + 0.5).
    expect(near(attached_entity_position(attachment, player, 0.0), {100.0, 51.0, 200.7}),
           "unmoved player: same flat spot, z + 0.5");

    expect(near(attached_entity_position(attachment, {110.0, 40.0, 190.0}, 0.0),
                {110.0, 41.0, 190.7}),
           "the grenade moves with the player");

    expect(near(attached_entity_position(attachment, player, 90.0), {101.0, 50.0, 200.7}),
           "and turns with the player: yaw 90 looks down +x");
    expect(near(attached_entity_position(attachment, player, 180.0), {100.0, 49.0, 200.7}),
           "yaw 180 looks down -y");

    EntityAttachment bare;
    bare.player_id = 9U;
    expect(near(attached_entity_position(bare, player, 77.0), {100.0, 50.0, 200.5}),
           "without a character at set_target the defaults are 0, 0.5, 0");
}

void the_session_handles_the_stuck_grenade() {
    TutorialSessionConfig live;
    live.network_authoritative = true;
    live.initial_position = {130.5, 70.5, 230.75};
    live.initial_orientation = {-1.0, 0.0, 0.0};
    live.initial_loadout = {17U, 2U, 5U};
    live.initial_tool = static_cast<std::uint8_t>(17U);
    TutorialWorldSession session{platform_world(), live};

    LocalEntity flying;
    flying.id = 500U;
    flying.type = 34U;
    flying.position = {128.0, 70.0, 228.0};
    flying.velocity = {-10.0, 0.0, 0.0};
    flying.owner = 4U;
    flying.fuse = 5.0;
    expect(session.apply_server_entity(flying), "the flying sticky is accepted");
    static_cast<void>(session.take_terrain_impacts());

    const auto taken = session.take_server_entity(500U);
    expect(taken.has_value() && taken->type == 34U && taken->owner == 4U,
           "the destroyed flying sticky is handed back");
    expect(session.entities().empty(), "and is gone from the world");
    expect(session.take_terrain_impacts().empty(),
           "StickyGrenadeEntity has no on_delete: no blast when it sticks");
    expect(!session.take_server_entity(500U).has_value(), "it can be taken only once");

    LocalEntity stuck;
    stuck.id = 501U;
    stuck.type = 35U;
    stuck.position = {126.0, 70.0, 232.5};
    stuck.owner = 4U;
    stuck.fuse = 5.0;
    expect(session.apply_server_entity(stuck), "the stuck sticky is accepted");
    for (int step{}; step < 60; ++step) session.tick();
    const auto found = std::ranges::find(session.entities(), std::uint64_t{501U},
                                         &LocalEntity::id);
    expect(found != session.entities().end(), "the stuck sticky lives through its fuse");
    expect(near(found->position, stuck.position),
           "a grenade stuck to terrain does not fall or drift");
    expect(found->fuse > 3.9 && found->fuse < 4.1,
           "the packet fuse counts down locally (set_packet, update)");
    expect(!found->fuse_label, "set_packet leaves draw_fuse off");
    expect(!entity_world_label(*found, found->position, std::nullopt, std::nullopt).visible,
           "no countdown digits without SET_FUSE");

    ServerEntityMutation fuse;
    fuse.entity_id = 501U;
    fuse.property = ServerEntityProperty::fuse;
    fuse.scalar = 3.2;
    expect(session.apply_server_entity_update(fuse), "SET_FUSE is accepted");
    const auto relabelled = std::ranges::find(session.entities(), std::uint64_t{501U},
                                              &LocalEntity::id);
    const auto label = entity_world_label(*relabelled, relabelled->position, std::nullopt,
                                          std::nullopt);
    expect(relabelled->fuse_label && label.visible && label.value == 4U &&
               near(label.position.z, relabelled->position.z - 1.0),
           "set_fuse shows ceil(fuse) one block above the grenade");

    expect(session.carry_server_entity(501U, {140.0, 80.0, 229.0}),
           "an attached grenade can be carried");
    expect(near(std::ranges::find(session.entities(), std::uint64_t{501U}, &LocalEntity::id)
                    ->position,
                {140.0, 80.0, 229.0}),
           "to exactly where its player is");
    expect(!session.carry_server_entity(999U, {1.0, 1.0, 1.0}) &&
               !session.carry_server_entity(501U, {std::nan(""), 1.0, 1.0}),
           "unknown ids and non-finite positions are refused");

    static_cast<void>(session.take_terrain_impacts());
    expect(session.destroy_server_entity(501U), "DestroyEntity removes the stuck grenade");
    expect(!session.take_terrain_impacts().empty(),
           "AttachedStickyGrenadeEntity.on_delete is the explosion");
    expect(session.entities().empty(), "and the grenade is gone");

    session.present_server_entity_blast(*taken);
    expect(!session.take_terrain_impacts().empty(),
           "a server that never creates entity 35 still gets its blast");
}

void the_retail_cues_ship() {
    const std::filesystem::path sounds{std::filesystem::path{AOS_TEST_ASSET_ROOT} / "sounds"};
    for (const auto* stem :
         {"AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_attach_001",
          "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_attach_005",
          "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_attach_water_003",
          "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_countdown_001",
          "AoS_soundfx_PLAYER_medic_wpn_SHIELD_bullet_imp_007",
          "AoS_soundfx_PLAYER_medic_wpn_SHIELD_melee_imp_005"}) {
        expect(std::filesystem::exists(sounds / (std::string{stem} + ".ogg")),
               "every sticky and riot-shield cue must exist in the retail sound set");
    }
    expect(riot_shield_melee_hit_type == 2U, "MELEE_KILL is 2");
}

} // namespace

int main() {
    try {
        yaw_follows_to_pitch_yaw();
        set_target_stores_the_offsets();
        update_carries_the_grenade_with_the_player();
        the_session_handles_the_stuck_grenade();
        the_retail_cues_ship();
    } catch (const std::exception& error) {
        std::cerr << "entity attachment test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
