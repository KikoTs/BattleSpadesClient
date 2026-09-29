#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/local_entity.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::filesystem::path asset_root() {
    return std::filesystem::path{AOS_TEST_ASSET_ROOT};
}

/** Ids are dense 0..39 and the table is directly indexable by id. */
void the_catalog_covers_every_retail_id() {
    const auto catalog = entity_catalog();
    expect(catalog.size() == 40U,
           "the catalog must carry all 40 retail entity ids, got " +
               std::to_string(catalog.size()));
    for (std::size_t index = 0U; index < catalog.size(); ++index) {
        expect(catalog[index].type_id == index,
               "row " + std::to_string(index) + " must be id " + std::to_string(index));
        const auto* found = find_entity_definition(static_cast<std::uint8_t>(index));
        expect(found == &catalog[index],
               "find_entity_definition must return the row at its own id");
    }
    expect(find_entity_definition(40U) == nullptr,
           "an out-of-range id must return nullptr, not wrap");
}

/**
 * The five entities with no shipping art keep an EMPTY parts span.
 *
 * Pinned as a NEGATIVE result on purpose. Every one of these is a plausible
 * candidate for someone quietly substituting a lookalike model, and
 * MACHINE_GUN is the one that proves the point: retail itself ships no art for
 * it (mgWeapon.py declares `model = []`), so any mesh we drew there would be
 * invented, not recovered.
 */
void entities_without_art_stay_empty() {
    for (const std::uint8_t type_id : {std::uint8_t{0U},
                                       std::uint8_t{2U},
                                       std::uint8_t{7U},
                                       std::uint8_t{26U},
                                       std::uint8_t{31U}}) {
        const auto* definition = find_entity_definition(type_id);
        expect(definition != nullptr, "id must exist");
        expect(definition->parts.empty(),
               std::string{definition->symbolic_name} +
                   " ships no art; its parts span must stay empty");
        expect(definition->category == EntityCategory::unportable,
               std::string{definition->symbolic_name} + " must be unportable");
        expect(!definition->spawnable,
               std::string{definition->symbolic_name} + " must not appear in the spawn menu");
    }
}

/**
 * BASE(1) must never reach the wire.
 *
 * It is absent from retail's GameScene.ENTITIES and a clean client dies with
 * KeyError: 1 on receiving it. That was live-measured, so the flag is carried
 * from day one rather than added when the network stage lands.
 */
/**
 * Radar A1900 is the can_detect_player squared-distance limit (250) and A1901
 * the 45 s lifetime; dynamite binds A1632 = 8, not the stale named 5.
 */
void radar_and_dynamite_use_the_stock_aliases() {
    const auto* radar = find_entity_definition(36U);
    expect(radar != nullptr && radar->sense_radius == 250.0F && radar->lifetime == 45.0F,
           "radar station must sense 250 blocks and live 45 s");
    const auto* dynamite = find_entity_definition(10U);
    expect(dynamite != nullptr && dynamite->blast_radius == 8.0F &&
               dynamite->blast_damage == 300.0F,
           "dynamite must use the stock A1632 radius 8 and damage 300");
}

void base_is_never_wire_safe() {
    const auto* base = find_entity_definition(1U);
    expect(base != nullptr && !base->wire_safe, "BASE(1) must be flagged wire_safe=false");
    const auto* flag = find_entity_definition(0U);
    expect(flag != nullptr && !flag->wire_safe, "FLAG(0) is defensively flagged alongside BASE");
    // Everything else is expected to be safe; a new unsafe row should be a
    // deliberate edit here rather than a silent addition.
    std::size_t unsafe{};
    for (const auto& definition : entity_catalog()) {
        if (!definition.wire_safe) {
            ++unsafe;
        }
    }
    expect(unsafe == 2U, "exactly two rows may be wire-unsafe, found " + std::to_string(unsafe));
}

/**
 * Every referenced model exists on disk AND actually parses.
 *
 * Collects every failure and reports them together rather than dying on the
 * first, because a single missing file usually means a whole naming
 * convention is wrong and one-at-a-time is a slow way to learn that.
 */
void every_referenced_model_loads() {
    std::vector<std::string> failures;
    for (const auto& definition : entity_catalog()) {
        for (const auto& piece : definition.parts) {
            const auto path = asset_root() / "kv6" / std::string{piece.kv6};
            if (!std::filesystem::is_regular_file(path)) {
                failures.push_back(std::string{definition.symbolic_name} + " -> " + path.string() +
                                   " does not exist");
                continue;
            }
            std::string error;
            const auto model = Kv6Model::load_file(path, &error);
            if (!model.has_value()) {
                failures.push_back(std::string{definition.symbolic_name} + " -> " + path.string() +
                                   " failed to parse: " + error);
            }
        }
    }
    if (!failures.empty()) {
        std::string report = std::to_string(failures.size()) + " entity model(s) unusable:";
        for (const auto& failure : failures) {
            report += "\n  " + failure;
        }
        throw std::runtime_error{report};
    }
}

/**
 * Every referenced sound stem ships.
 *
 * This is the check that catches the intel case: retail's source names a
 * pickup cue whose file does not exist in the shipped tree, and wiring it
 * would produce a silent entity that looks like a code bug.
 */
void every_referenced_sound_ships() {
    std::vector<std::string> failures;
    const auto sounds = asset_root() / "sounds";
    for (const auto& definition : entity_catalog()) {
        for (const auto stem :
             {definition.sound_place, definition.sound_trigger, definition.sound_trigger_water}) {
            if (stem.empty()) {
                continue;
            }
            const auto path = sounds / (std::string{stem} + ".ogg");
            if (!std::filesystem::is_regular_file(path)) {
                failures.push_back(std::string{definition.symbolic_name} + " -> " + path.string() +
                                   " does not ship");
            }
        }
    }
    if (!failures.empty()) {
        std::string report = std::to_string(failures.size()) + " entity sound(s) missing:";
        for (const auto& failure : failures) {
            report += "\n  " + failure;
        }
        throw std::runtime_error{report};
    }
}

/**
 * The renderer band is budgeted per PART, so the widest rig must fit.
 *
 * The turret is three meshes; a per-entity budget would let 256 turrets be
 * spawned against 256 slots and overrun by 512.
 */
void the_widest_rig_is_known() {
    expect(maximum_entity_parts() >= 3U,
           "the rocket turret's three-part rig must be reflected in the budget");
    const auto* turret = find_entity_definition(8U);
    expect(turret != nullptr && turret->parts.size() == 3U,
           "ROCKET_TURRET must carry exactly three parts");
    // Base static, ball yaw, gun yaw+pitch -- the thing that makes it read as
    // a machine rather than a weathervane.
    expect(turret->parts[0U].rotation_mode == 0U, "the turret base is bolted down");
    expect(turret->parts[1U].rotation_mode == 1U, "the turret ball takes yaw only");
    expect(turret->parts[2U].rotation_mode == 2U, "the turret gun adds pitch");
}

/**
 * Every entity renders at a physically sensible world size.
 *
 * This is the regression guard for shipping a 32-block-wide ammo crate. The
 * catalog once defaulted model_size to 1.0, which is not a scale at all but
 * "one world block per KV6 voxel" -- 16 to 32 times too big on every row that
 * had no retail MODEL_SIZE constant.
 *
 * The bound is calibrated, not guessed: the character body/head/leg stack is 56
 * voxels at scale 0.05 (class_models.cpp:13) = 2.80 blocks against a real
 * standing height of 2.7. Nothing in the entity set should be larger than a
 * couple of players, or smaller than a grenade.
 */
void every_entity_renders_at_a_sensible_size() {
    std::vector<std::string> failures;
    for (const auto& definition : entity_catalog()) {
        for (const auto& piece : definition.parts) {
            const auto path = asset_root() / "kv6" / std::string{piece.kv6};
            std::string error;
            const auto model = Kv6Model::load_file(path, &error);
            if (!model.has_value()) {
                continue; // every_referenced_model_loads owns that failure
            }
            const auto voxels = std::max({model->size_x(), model->size_y(), model->size_z()});
            const auto blocks = static_cast<float>(voxels) * definition.model_size * piece.scale;
            if (blocks < 0.1F || blocks > 4.0F) {
                failures.push_back(std::string{definition.symbolic_name} + " -> " +
                                   std::string{piece.kv6} + " is " + std::to_string(voxels) +
                                   " voxels at scale " +
                                   std::to_string(definition.model_size * piece.scale) + " = " +
                                   std::to_string(blocks) + " blocks");
            }
        }
    }
    if (!failures.empty()) {
        std::string report{"entities render at implausible world sizes:"};
        for (const auto& failure : failures) {
            report += "\n  " + failure;
        }
        throw std::runtime_error{report};
    }
}

/**
 * Part translations are world units; loader pivot offsets stay raw voxels.
 *
 * Retail mixes the two conventions and the generator normalises them. A raw
 * voxel offset that slipped through would be ~20x too large and fling the part
 * off into the map, so any offset bigger than a few blocks is a conversion bug
 * rather than an authored value.
 */
void part_offsets_are_world_units() {
    for (const auto& definition : entity_catalog()) {
        for (const auto& piece : definition.parts) {
            for (const auto axis : piece.offset) {
                expect(std::abs(axis) <= 4.0F,
                       std::string{definition.symbolic_name} + " offset " + std::to_string(axis) +
                           " looks like unconverted voxel units");
            }
        }
    }
    // models.py passes the grave's 11 to KV6.offset_pivots. Treating it as a
    // world translation seats the grave below the wire surface and moves the
    // death camera's visual anchor away from the model.
    const auto* grave = find_entity_definition(11U);
    expect(grave != nullptr && !grave->parts.empty(), "GRAVE must have art");
    expect(std::abs(grave->model_size - 0.10F) < 1e-4F,
           "the grave's recovered class scale is 0.10");
    expect(std::abs(grave->parts[0U].offset[2U]) < 1e-4F &&
               std::abs(grave->parts[0U].pivot_offset[2U] - 11.0F) < 1e-4F,
           "the grave's 11-voxel value must remain a loader pivot offset");
    // The turret's are ALREADY premultiplied in the source (A1612 = -3 * 0.06)
    // and must NOT be scaled a second time.
    const auto* turret = find_entity_definition(8U);
    expect(turret != nullptr && turret->parts.size() == 3U, "turret rig");
    expect(std::abs(turret->parts[0U].offset[2U] + 0.18F) < 1e-4F,
           "the turret base offset must stay at the source's -0.18");

    const auto* medpack = find_entity_definition(30U);
    expect(medpack != nullptr && medpack->parts.size() == 1U, "MEDPACK must have its retail model");
    expect(std::abs(medpack->parts[0U].pivot_offset[0U] - 8.0F) < 1e-4F &&
               std::abs(medpack->parts[0U].pivot_offset[1U] + 21.0F) < 1e-4F &&
               std::abs(medpack->parts[0U].pivot_offset[2U]) < 1e-4F,
           "MedPack must retain models.py's raw (8,-21,0) KV6 pivot");
}

/** Grounded server entities touch terrain despite their unrelated KV6 pivots. */
void grounded_entity_rigs_seat_their_real_mesh_bottom_on_support() {
    const auto adjustment_for = [](std::uint8_t type) {
        const auto* definition = find_entity_definition(type);
        expect(definition != nullptr && !definition->parts.empty(),
               "ground-contact fixture must have a model");
        const auto& contact_part = definition->parts[0U];
        std::string error;
        auto model = Kv6Model::load_file(
            asset_root() / "kv6" / std::string{contact_part.kv6}, &error);
        expect(model.has_value(), "ground-contact model failed to load: " + error);
        model->offset_pivots(contact_part.pivot_offset);
        const auto mesh = model->mesh();

        LocalEntity entity;
        entity.type = type;
        entity.position = {50.25, 60.25, 100.0};
        entity.face = 4U;
        entity.grounded = true;
        const auto adjustment = entity_vertical_contact_adjustment(
            entity, *definition, contact_part, mesh.minimum[1U]);
        const auto origin = entity_presentation_position(entity, contact_part);
        const auto visual_bottom =
            origin.z + adjustment - static_cast<double>(mesh.minimum[1U]) *
                                          static_cast<double>(definition->model_size) *
                                          static_cast<double>(contact_part.scale);
        expect(std::abs(visual_bottom - entity.position.z) < 0.0001,
               std::string{definition->symbolic_name} +
                   " must put its lowest visible voxel on the support plane");
        entity.grounded = false;
        const auto airborne_adjustment = entity_vertical_contact_adjustment(
            entity, *definition, contact_part, mesh.minimum[1U]);
        expect(std::abs(airborne_adjustment - adjustment) < 0.0001,
               std::string{definition->symbolic_name} +
                   " must retain the identical model/contact offset while bouncing");
        return adjustment;
    };

    // These values exercise the three mistakes that a generic half-block
    // nudge cannot solve: a shallow model, a multi-part rig, and a loader pivot.
    const auto mine = adjustment_for(9U);
    const auto turret = adjustment_for(8U);
    const auto grave = adjustment_for(11U);
    expect(mine > 0.37 && mine < 0.38,
           "landmine must descend by its measured 0.375-block air gap");
    expect(turret > 0.52 && turret < 0.54,
           "turret base must define one stable contact correction for the full rig");
    expect(grave > 0.54 && grave < 0.56,
           "grave's recovered 11-voxel pivot requires the missing native contact offset, got " +
               std::to_string(grave));

    // The catalog contains several other physical model families. Prove the
    // same invariant for all of them rather than fixing only the screenshots.
    for (const auto& definition : entity_catalog()) {
        if (uses_entity_terrain_gravity(definition) && !definition.parts.empty()) {
            static_cast<void>(adjustment_for(definition.type_id));
        }
    }

    const auto* mine_definition = find_entity_definition(9U);
    expect(mine_definition != nullptr, "landmine row");
    LocalEntity airborne;
    airborne.type = 9U;
    airborne.position = {1.0, 2.0, 3.0};
    const auto airborne_adjustment = entity_vertical_contact_adjustment(
        airborne, *mine_definition, mine_definition->parts[0U], -2.5F);
    airborne.grounded = true;
    const auto grounded_adjustment = entity_vertical_contact_adjustment(
        airborne, *mine_definition, mine_definition->parts[0U], -2.5F);
    expect(airborne_adjustment == grounded_adjustment && airborne_adjustment != 0.0,
           "the model contact offset must not switch at the final bounce");
    airborne.face = 0U;
    expect(entity_vertical_contact_adjustment(
               airborne, *mine_definition, mine_definition->parts[0U], -2.5F) == 0.0,
           "wall attachments must never be pulled onto the floor");
}

/** Grave uses the packet team colour despite its non-magenta authored panel. */
void the_grave_resolves_its_exact_black_team_panel() {
    const auto path = asset_root() / "kv6" / "grave.kv6";
    std::string error;
    auto grave = Kv6Model::load_file(path, &error);
    expect(grave.has_value(), "grave.kv6 failed to load: " + error);
    const auto had_black = std::ranges::any_of(grave->voxels(), [](const Kv6Model::Voxel& voxel) {
        return voxel.color == VxlColor{0U, 0U, 0U, 255U};
    });
    expect(had_black, "grave fixture lost its exact-black team panel");
    constexpr VxlColor team{17U, 91U, 204U, 255U};
    apply_entity_team_material(*grave, 11U, team);
    const auto has_team = std::ranges::any_of(
        grave->voxels(), [team](const Kv6Model::Voxel& voxel) { return voxel.color == team; });
    expect(has_team, "grave panel did not resolve to the CreateEntity RGB");
}

/** The menu offers only rows we can actually place. */
void the_spawn_menu_is_placeable() {
    const auto menu = spawnable_entities();
    expect(!menu.empty(), "the spawn menu must not be empty");
    std::set<std::uint8_t> seen;
    for (const auto* definition : menu) {
        expect(definition->spawnable, "menu rows must be spawnable");
        expect(definition->category != EntityCategory::unportable,
               "no unportable row may reach the menu");
        expect(seen.insert(definition->type_id).second, "the menu must not list an id twice");
    }
}

/**
 * Dynamite's blast radius is 8, not the 5 the named constant block claims.
 *
 * Retail ships two disagreeing constant blocks and the weapon modules bind the
 * A-aliased one. Pinned because reading the human-friendly name is the natural
 * mistake and it is silently wrong by a factor that matters.
 */
void the_alias_block_wins_where_retail_disagrees() {
    const auto* dynamite = find_entity_definition(10U);
    expect(dynamite != nullptr, "DYNAMITE must exist");
    expect(dynamite->blast_radius > 7.5F && dynamite->blast_radius < 8.5F,
           "dynamite blast radius must come from the alias block (8), got " +
               std::to_string(dynamite->blast_radius));
    expect(dynamite->fuse > 6.5F && dynamite->fuse < 7.5F, "dynamite fuse must be 7 s");
}

/** Rows whose numbers we invented must say so, not pose as recovered parity. */
void invented_rows_are_labelled() {
    const auto* c4 = find_entity_definition(38U);
    expect(c4 != nullptr, "C4 must exist");
    expect(c4->weakest_provenance == EntityProvenance::battlespades,
           "every C4 number comes from our own server, so the row is an invention");
    const auto* landmine = find_entity_definition(9U);
    expect(landmine != nullptr && landmine->weakest_provenance == EntityProvenance::retail_alias,
           "the landmine is fully recovered from the retail alias block");
}

/** Entity 29 is a retail item-id-dispatched rig, not one fixed marker mesh. */
void ugc_entity_variants_preserve_retail_models_scales_and_groups() {
    const auto health = ugc_entity_model_parts(0U);
    const auto ammo = ugc_entity_model_parts(1U);
    const auto spawn_large = ugc_entity_model_parts(6U);
    const auto base_large = ugc_entity_model_parts(18U);
    expect(health.size() == 2U && health[1U].kv6 == "healthcrate.kv6",
           "UGC health drop must use the health crate rig");
    expect(ammo.size() == 2U && ammo[1U].kv6 == "ammocrate.kv6",
           "UGC ammo drop must use the ammo crate rig");
    expect(spawn_large.size() == 2U && spawn_large[1U].kv6 == "ugc_spawn_zone.kv6" &&
               std::abs(spawn_large[1U].scale - 1.5F) < 1.0e-5F,
           "large spawn zone must keep retail's 1.5 scale");
    expect(base_large.size() == 2U && base_large[1U].kv6 == "ugc_base_zone.kv6" &&
               std::abs(base_large[1U].scale - 1.75F) < 1.0e-5F,
           "large base zone must keep retail's 1.75 scale");
    expect(ugc_entity_model_parts(19U).empty(), "UGC item ids end at 18");
    expect(ugc_entity_team(6U) == 3U && ugc_entity_team(9U) == 2U &&
               ugc_entity_team(18U) == 0U,
           "green, blue and neutral UGC zones must retain team material roles");
    expect(next_ugc_item_variant(0U) == 1U && next_ugc_item_variant(2U) == 0U &&
               next_ugc_item_variant(16U) == 17U && next_ugc_item_variant(18U) == 16U &&
               next_ugc_item_variant(3U) == 3U,
           "RMB groups must cycle without folding the OCC point into a group");
}

void ugc_toolbar_icons_are_variant_specific() {
    expect(ugc_tool_icon_asset(0U) == "png/ui/ugc_tools/ugc_health_drop.png",
           "UGC health marker must use the retail Game Data icon");
    expect(ugc_tool_icon_asset(5U) == "png/ui/ugc_tools/ugc_spawngreen_med.png",
           "UGC green medium spawn must use the retail Game Data icon");
    expect(ugc_tool_icon_asset(15U) == "png/ui/ugc_tools/ugc_baseblue_large.png",
           "UGC blue large base must use the retail Game Data icon");
    expect(ugc_tool_icon_asset(18U) == "png/ui/ugc_tools/ugc_base_large.png",
           "UGC neutral large base must use the retail Game Data icon");
    expect(ugc_tool_icon_asset(19U).empty(), "invalid UGC items must not alias an icon");
}

struct TestCase final {
    const char* name;
    void (*run)();
};

} // namespace

int main() {
    const TestCase cases[]{
        {"the_catalog_covers_every_retail_id", the_catalog_covers_every_retail_id},
        {"entities_without_art_stay_empty", entities_without_art_stay_empty},
        {"base_is_never_wire_safe", base_is_never_wire_safe},
        {"radar_and_dynamite_use_the_stock_aliases", radar_and_dynamite_use_the_stock_aliases},
        {"every_referenced_model_loads", every_referenced_model_loads},
        {"every_referenced_sound_ships", every_referenced_sound_ships},
        {"the_widest_rig_is_known", the_widest_rig_is_known},
        {"every_entity_renders_at_a_sensible_size", every_entity_renders_at_a_sensible_size},
        {"part_offsets_are_world_units", part_offsets_are_world_units},
        {"grounded_entity_rigs_seat_their_real_mesh_bottom_on_support",
         grounded_entity_rigs_seat_their_real_mesh_bottom_on_support},
        {"the_grave_resolves_its_exact_black_team_panel",
         the_grave_resolves_its_exact_black_team_panel},
        {"the_spawn_menu_is_placeable", the_spawn_menu_is_placeable},
        {"the_alias_block_wins_where_retail_disagrees",
         the_alias_block_wins_where_retail_disagrees},
        {"invented_rows_are_labelled", invented_rows_are_labelled},
        {"ugc_entity_variants_preserve_retail_models_scales_and_groups",
         ugc_entity_variants_preserve_retail_models_scales_and_groups},
        {"ugc_toolbar_icons_are_variant_specific", ugc_toolbar_icons_are_variant_specific},
    };
    for (const auto& test : cases) {
        try {
            test.run();
        } catch (const std::exception& error) {
            std::cerr << "FAILED " << test.name << ": " << error.what() << '\n';
            return 1;
        }
        std::cout << "ok " << test.name << '\n';
    }
    return 0;
}
