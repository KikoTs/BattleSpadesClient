#include "battlespades/world/weapon_catalog.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using battlespades::world::WeaponCategory;
using battlespades::world::WeaponSecondaryBehavior;
using battlespades::world::aims_down_sights;
using battlespades::world::find_retail_weapon_constant;
using battlespades::world::find_weapon_definition;
using battlespades::world::no_selectable_tool;
using battlespades::world::selectable_tool_count;
using battlespades::world::valid_selectable_tool;
using battlespades::world::weapon_catalog;
using battlespades::world::weapon_catalog_contract_sha256;
using battlespades::world::weapon_secondary_behavior;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, const char* message) {
    expect(std::abs(actual - expected) < 0.000001, message);
}

void expect_constant(std::uint8_t tool_id,
                     std::string_view name,
                     double expected,
                     const char* message) {
    const auto* weapon = find_weapon_definition(tool_id);
    expect(weapon != nullptr, "constant owner must be a real tool");
    const auto* value = find_retail_weapon_constant(*weapon, name);
    expect(value != nullptr && value->value_count == 1U, message);
    expect_near(value->values.front(), expected, message);
}

void catalog_is_a_dense_protocol_id_table() {
    const auto catalog = weapon_catalog();
    expect(catalog.size() == selectable_tool_count,
           "catalog must contain every real Protocol 168 tool");
    for (std::size_t index{}; index < catalog.size(); ++index) {
        expect(catalog[index].tool_id == index,
               "catalog index must equal its byte-level tool id");
        expect(valid_selectable_tool(static_cast<std::uint8_t>(index)),
               "every catalog row must be selectable");
    }
    expect(!valid_selectable_tool(no_selectable_tool) &&
               find_weapon_definition(no_selectable_tool) == nullptr,
           "tool 65 is the sentinel, not an equipable weapon");
    expect(weapon_catalog_contract_sha256().size() == 64U,
           "generated catalog must retain its server-contract digest");
}

void recovered_server_stats_cover_old_and_new_weapons() {
    const auto* shotgun = find_weapon_definition(9U);
    expect(shotgun != nullptr && shotgun->category == WeaponCategory::shotgun &&
               shotgun->pellet_count == 10U && shotgun->spread == 0.04 &&
               shotgun->clip_size == 5U,
           "engineer shotgun must use the authoritative multi-pellet profile");
    const auto* sniper = find_weapon_definition(18U);
    expect(sniper != nullptr && sniper->head_damage == 175.0 &&
               sniper->team_colored_first_person_image &&
               !sniper->first_person_blue_asset.empty() &&
               !sniper->first_person_green_asset.empty(),
           "sniper must retain damage and all team-colored retail images");
    const auto* auto_shotgun = find_weapon_definition(62U);
    expect(auto_shotgun != nullptr && auto_shotgun->pellet_count == 10U &&
               auto_shotgun->fire_interval == 0.35 &&
               auto_shotgun->toolbar_icon_asset ==
                   "png/ui/weapons/autoShotgun.png",
           "later weapons must fall back to their authored toolbar image");
    const auto* molotov = find_weapon_definition(33U);
    expect(molotov != nullptr && molotov->projectile && molotov->blast_radius == 4.0 &&
               molotov->selectable_when_empty,
           "oriented explosives must retain projectile and empty-selection rules");
}

void recovered_retail_classes_keep_exact_inherited_tuning() {
    const auto* sniper2 = find_weapon_definition(19U);
    expect(sniper2 != nullptr && sniper2->retail.damage.hit_regions.has_value(),
           "sniper2 must retain its recovered five-region damage tuple");
    expect(*sniper2->retail.damage.hit_regions ==
               std::array<double, 5U>{34.0, 85.0, 34.0, 34.0, 34.0},
           "sniper2 must resolve the A-alias values used by the retail class");
    expect(sniper2->retail.use.shoot_interval == 1.1 &&
               sniper2->retail.ammo.magazine_capacity == 5U &&
               sniper2->retail.ammo.initial_magazine == 5U &&
               sniper2->retail.ammo.reserve_capacity == 15U &&
               sniper2->retail.ammo.initial_reserve == 15U &&
               sniper2->retail.ammo.restock_amount == 15U &&
               sniper2->damage_type == 6,
           "sniper2 timing, ammo and damage type must match retail");

    const auto* knife = find_weapon_definition(1U);
    expect(knife != nullptr && knife->retail.damage.block == 1.0 &&
               knife->retail.damage.melee_player == 80.0,
           "knife must not inherit the later overwritten 20-damage alias");

    const auto* pistol = find_weapon_definition(17U);
    expect(pistol != nullptr && pistol->retail.damage.hit_regions.has_value() &&
               *pistol->retail.damage.hit_regions ==
                   std::array<double, 5U>{20.0, 50.0, 20.0, 20.0, 20.0} &&
               pistol->retail.ammo.magazine_capacity == 6U &&
               pistol->retail.ammo.initial_magazine == 6U &&
               pistol->retail.ammo.reserve_capacity == 30U &&
               pistol->retail.ammo.initial_reserve == 30U &&
               pistol->retail.ammo.restock_amount == 30U,
           "pistol must retain region damage and the full retail ammo tuple");
    expect_near(*pistol->retail.aim.accuracy, 0.015,
                "pistol accuracy must match the recovered class");
    expect_near(*pistol->retail.aim.recoil_up, -0.005,
                "pistol recoil must match the recovered class");
}

void explosives_and_special_tools_keep_behavior_constants() {
    const auto* grenade = find_weapon_definition(11U);
    expect(grenade != nullptr && grenade->retail.ammo.maximum_count == 4U &&
               grenade->retail.ammo.initial_count == 2U &&
               grenade->retail.ammo.count_restock_amount == 4U &&
               grenade->retail.use.fuse == 2.5 &&
               grenade->retail.use.maximum_fuse == 2.5 &&
               grenade->retail.use.stoppable && grenade->retail.use.delayed_use,
           "grenade cooking and count inventory must match retail");
    expect_constant(11U, "GRENADE_THROW_SPEED", 50.0,
                    "grenade throw speed must be attached to the grenade");

    expect_constant(12U, "ROCKET_SPEED", 75.0,
                    "RPG rocket speed must be attached to the RPG");
    expect_constant(12U, "ROCKET_GRAVITY_MULTIPLIER", 0.05,
                    "RPG gravity must be attached to the RPG");
    expect_constant(12U, "ROCKET_EXPLOSION_RADIUS", 4.0,
                    "RPG explosion radius must be attached to the RPG");
    expect_constant(12U, "ROCKET_EXPLOSION_DAMAGE", 140.0,
                    "RPG explosion damage must be attached to the RPG");
    expect_constant(12U, "ROCKET_EXPLOSION_BLOCK_DAMAGE", 5.0,
                    "RPG block damage must be attached to the RPG");

    expect_constant(14U, "DRILL_DIGGING_SPEED", 20.0,
                    "drill digging speed must be attached to the drill gun");
    expect_constant(14U, "DRILL_DESTROYED_EXPLOSION_DAMAGE", 95.0,
                    "drill destruction damage must be attached to the drill gun");
    expect_constant(14U, "DRILL_DRILLING_BLOCK_DAMAGE", 20.0,
                    "drill terrain damage must be attached to the drill gun");

    expect_constant(33U, "BLOCKFIRE_CHARACTER_DAMAGE", 2.5,
                    "molotov fire damage must be attached to the molotov");
    expect_constant(33U, "MOLOTOV_THROW_MIN_SPEED", 35.0,
                    "molotov minimum throw speed must be attached to the molotov");
    expect_constant(33U, "MOLOTOV_THROW_MAX_CHARGE", 3.0,
                    "molotov charge duration must be attached to the molotov");

    struct BlastExpectation final {
        std::uint8_t tool_id;
        double radius;
        double block_damage;
    };
    constexpr std::array expected_blasts{
        BlastExpectation{11U, 4.0, 4.0},
        BlastExpectation{12U, 4.0, 5.0},
        BlastExpectation{13U, 4.0, 2.0},
        BlastExpectation{14U, 3.0, 5.0},
        BlastExpectation{20U, 3.0, 15.0},
        BlastExpectation{21U, 5.0, 7.0},
        BlastExpectation{31U, 2.0, 15.0},
        BlastExpectation{32U, 2.0, 0.5},
        BlastExpectation{33U, 4.0, 3.0},
        BlastExpectation{54U, 3.0, 3.0},
        BlastExpectation{55U, 4.0, 6.0},
        BlastExpectation{57U, 5.0, 6.0},
        BlastExpectation{58U, 3.0, 15.0},
        BlastExpectation{59U, 8.0, 7.0},
    };
    for (const auto& expected : expected_blasts) {
        const auto* explosive = find_weapon_definition(expected.tool_id);
        expect(explosive != nullptr, "explosive definition must exist");
        expect_near(explosive->blast_radius, expected.radius,
                    "primary explosion radius must match its exact constant");
        expect_near(explosive->block_damage, expected.block_damage,
                    "primary explosion block damage must match its exact constant");
    }
}

void every_tool_has_a_recovered_runtime_identity() {
    for (const auto& tool : weapon_catalog()) {
        expect(!tool.retail.module.empty() && !tool.retail.class_name.empty(),
               "every protocol tool must map to a recovered retail class");
        expect(tool.damage_type >= -1 && tool.kill_type >= -1,
               "damage and kill ids must use -1 as their only sentinel");
    }
}

void right_mouse_behavior_requires_a_real_retail_capability() {
    const auto behavior = [](std::uint8_t id) {
        const auto* weapon = find_weapon_definition(id);
        expect(weapon != nullptr, "secondary behavior owner must exist");
        return weapon_secondary_behavior(*weapon);
    };
    expect(behavior(18U) == WeaponSecondaryBehavior::magnified_scope &&
               behavior(19U) == WeaponSecondaryBehavior::magnified_scope,
           "only the recovered sniper sights must be magnified scopes");
    // Retail's gate is (not has_secondary) and can_zoom and sight != None. It
    // has no weapon-category term and no magnification threshold, and those
    // two invented terms are what left every iron-sight weapon in the game --
    // the rifle, the SMGs, the shotguns, the pistols, the bazookas -- with no
    // right click at all.
    expect(behavior(6U) == WeaponSecondaryBehavior::iron_sights &&
               behavior(7U) == WeaponSecondaryBehavior::iron_sights &&
               behavior(12U) == WeaponSecondaryBehavior::iron_sights &&
               behavior(62U) == WeaponSecondaryBehavior::iron_sights,
           "ordinary guns and launchers must aim through their own iron sights");
    int aiming{};
    for (const auto& tool : weapon_catalog()) {
        aiming += aims_down_sights(weapon_secondary_behavior(tool)) ? 1 : 0;
    }
    expect(aiming == 22, "exactly twenty-two recovered tools aim down sights");
    expect(behavior(8U) == WeaponSecondaryBehavior::spin_up,
           "minigun RMB must own the recovered barrel spin-up action");
    expect(behavior(11U) == WeaponSecondaryBehavior::none &&
               behavior(51U) == WeaponSecondaryBehavior::none &&
               behavior(64U) == WeaponSecondaryBehavior::none,
           "sightless grenade, medpack and disguise must ignore RMB");
    expect(behavior(4U) == WeaponSecondaryBehavior::tool_action &&
               behavior(5U) == WeaponSecondaryBehavior::tool_action &&
               behavior(23U) == WeaponSecondaryBehavior::tool_action &&
               behavior(43U) == WeaponSecondaryBehavior::tool_action &&
               behavior(59U) == WeaponSecondaryBehavior::tool_action,
           "spade, block, prefab, paint and C4 must route their real RMB action");
    expect(behavior(15U) == WeaponSecondaryBehavior::deploy_machine_gun,
           "MG right mouse must deploy rather than enter generic aim");
}

void every_catalogued_asset_exists() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    for (const auto& tool : weapon_catalog()) {
        for (const auto asset : {tool.toolbar_icon_asset,
                                 tool.first_person_image_asset,
                                 tool.first_person_blue_asset,
                                 tool.first_person_green_asset,
                                 tool.first_person_neutral_asset,
                                 tool.voxel_model_asset}) {
            if (!asset.empty()) {
                expect(std::filesystem::is_regular_file(root / asset),
                       "generated weapon asset path must exist");
            }
        }
        for (const auto& model : tool.third_person_models) {
            expect(std::filesystem::is_regular_file(root / model.asset),
                   "every recovered third-person KV6 part must exist");
        }
        for (const auto& model : tool.first_person_models) {
            expect(std::filesystem::is_regular_file(root / model.asset),
                   "every recovered first-person KV6 part must exist");
        }
        for (const auto asset : {tool.sight_model_asset,
                                 tool.pin_model.asset,
                                 tool.casing_model_asset,
                                 tool.tracer_model_asset}) {
            if (!asset.empty()) {
                expect(std::filesystem::is_regular_file(root / asset),
                       "every recovered weapon effect KV6 must exist");
            }
        }
        if (!tool.voxel_model_asset.empty()) {
            expect(!tool.third_person_models.empty() &&
                       !tool.first_person_models.empty(),
                   "load_weapon models must populate both retail model views");
        }
    }
}

void multipart_and_special_view_models_match_retail() {
    const auto* minigun = find_weapon_definition(8U);
    expect(minigun != nullptr && minigun->first_person_models.size() == 2U &&
               minigun->first_person_models[0].asset == "kv6/Minigun_Body.kv6" &&
               minigun->first_person_models[1].asset == "kv6/Minigun_Barrel.kv6",
           "minigun must retain separate animated body and barrel models");
    const auto* turret = find_weapon_definition(16U);
    expect(turret != nullptr && turret->first_person_models.size() == 3U,
           "rocket turret must retain base, ball and gun model parts");
    const auto* c4 = find_weapon_definition(59U);
    expect(c4 != nullptr && c4->first_person_models.size() == 1U &&
               c4->first_person_models.front().asset == "kv6/c4_detonator.kv6",
           "C4 first person must show the detonator, not the placed charge");
    const auto* pistol = find_weapon_definition(17U);
    expect(pistol != nullptr && pistol->shoot_sound == "pistolshoot" &&
               pistol->reload_sound == "pistolreload" &&
               pistol->sight_model_asset == "kv6/pistol_sight.kv6" &&
               pistol->casing_model_asset == "kv6/pistolcasing.kv6" &&
               pistol->tracer_model_asset == "kv6/pistoltracer.kv6",
           "pistol audiovisual identities must match recovered retail media");
    expect(pistol->third_person_models.size() == 1U &&
               pistol->third_person_models.front().authored_offset ==
                   std::array<float, 3U>{6.0F, -18.0F, 0.0F},
           "pistol must retain its models.py third-person hand offset");
    const auto* machete = find_weapon_definition(50U);
    expect(machete != nullptr && machete->third_person_models.size() == 1U &&
               machete->third_person_models.front().authored_offset ==
                   std::array<float, 3U>{13.0F, -25.0F, 15.0F},
           "machete must retain its unusual authored hand offset");
    const auto* medpack = find_weapon_definition(51U);
    expect(medpack != nullptr && medpack->first_person_models.size() == 1U &&
               medpack->first_person_models.front().authored_offset ==
                   std::array<float, 3U>{0.0F, 0.0F, -3.0F},
           "medpack must retain its explicit first-person vertical offset");
    const auto* assault = find_weapon_definition(60U);
    expect(assault != nullptr &&
               assault->shoot_sound ==
                   "AoS_soundfx_PLAYER_commando_wpn_ASSAULT_RIFLE_burst_fire_001-005" &&
               assault->reload_sound ==
                   "AoS_soundfx_PLAYER_commando_wpn_ASSAULT_RIFLE_reload_001",
           "new-class weapon sound groups must survive constants_audio recovery");
}

} // namespace

int main() {
    try {
        catalog_is_a_dense_protocol_id_table();
        recovered_server_stats_cover_old_and_new_weapons();
        recovered_retail_classes_keep_exact_inherited_tuning();
        explosives_and_special_tools_keep_behavior_constants();
        every_tool_has_a_recovered_runtime_identity();
        right_mouse_behavior_requires_a_real_retail_capability();
        multipart_and_special_view_models_match_retail();
        every_catalogued_asset_exists();
        std::cout << "Weapon catalog parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
