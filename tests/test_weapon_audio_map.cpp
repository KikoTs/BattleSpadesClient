#include "battlespades/audio/explosion_sound.hpp"
#include "battlespades/audio/entity_sound.hpp"
#include "battlespades/audio/sound_groups.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::audio::sound_group_stems;
using battlespades::audio::retail_sound_pitch_ratio;
using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::filesystem::path sound_root() {
    return std::filesystem::path{AOS_TEST_ASSET_ROOT} / "sounds";
}

/**
 * The one verified retail hole.
 *
 * snowBlowerWeapon.py:19 and ugcSnowBlowerWeapon.py:20 both declare
 * `reload_sound = 'snowcan_reload'`, but no such OGG ships. It is unreachable
 * in practice because SnowBlowerWeapon.is_reloadable() returns False
 * (snowBlowerWeapon.py:93-94), so retail never requests it. Pinning it here
 * keeps the exemption honest: if someone later "fixes" it by substituting a
 * different sample, this test is where that decision has to be argued.
 */
constexpr std::string_view known_missing_group{"snowcan_reload"};

struct Cue final {
    std::uint8_t tool_id;
    std::string_view weapon;
    std::string_view role;
    std::string_view group;
};

[[nodiscard]] std::vector<Cue> every_cue() {
    std::vector<Cue> cues;
    for (const auto& weapon : weapon_catalog()) {
        const auto add = [&](std::string_view role, std::string_view group) {
            if (!group.empty()) {
                cues.push_back({weapon.tool_id, weapon.symbolic_name, role, group});
            }
        };
        add("shoot_sound", weapon.shoot_sound);
        add("reload_sound", weapon.reload_sound);
        add("reload_done_sound", weapon.reload_done_sound);
        add("fire_loop", weapon.sounds.fire_loop);
        add("fire_tail", weapon.sounds.fire_tail);
        add("spin_loop", weapon.sounds.spin_loop);
        add("melee_miss", weapon.sounds.melee_miss);
        add("melee_hit_block", weapon.sounds.melee_hit_block);
        add("melee_hit_player", weapon.sounds.melee_hit_player);
        add("empty_fire", weapon.sounds.empty_fire);
        add("pin", weapon.sounds.pin);
        add("throw_release", weapon.sounds.throw_release);
        add("tool_loop_start", weapon.sounds.tool_loop_start);
        add("tool_loop", weapon.sounds.tool_loop);
        add("tool_loop_stop", weapon.sounds.tool_loop_stop);
        add("tool_extra", weapon.sounds.tool_extra);
    }
    return cues;
}

void group_expansion_follows_the_retail_convention() {
    expect(sound_group_stems("").empty(), "an empty group must name no file");
    expect(sound_group_stems("smgreload") == std::vector<std::string>{"smgreload"},
           "an unnumbered group must name exactly its own file");
    const auto four = sound_group_stems("des_imp_small_001-004");
    expect(four.size() == 4U && four.front() == "des_imp_small_001" &&
               four.back() == "des_imp_small_004",
           "a numbered range must expand to every variant inclusive");
    // A trailing dash that is not a numbered range must stay literal.
    expect(sound_group_stems("foo-bar") == std::vector<std::string>{"foo-bar"},
           "a non-numeric suffix must not be treated as a range");
}

void authored_pitch_metadata_and_quantization_match_retail() {
    const auto* pistol = find_weapon_definition(17U);
    expect(pistol != nullptr && pistol->shoot_sound_pitch[0U] == -0.8F &&
               pistol->shoot_sound_pitch[1U] == 0.8F,
           "the pistol must preserve its authored +/-0.8 semitone shot pitch");
    const auto* assault = find_weapon_definition(60U);
    expect(assault != nullptr && assault->shoot_sound_pitch[0U] == 0.0F &&
               assault->shoot_sound_pitch[1U] == 0.0F,
           "the new assault rifle cue must remain unpitched");

    expect(std::abs(retail_sound_pitch_ratio(0.0F, 0.0F, 123U) - 1.0F) < 0.000001F,
           "an unpitched cue must retain unit frequency");
    // Python 2 int() truncates +/-66.666 toward zero. randint is inclusive,
    // so this range resolves to -0.792, 0, +0.792 semitone at its endpoints.
    const auto low = std::pow(2.0F, -0.792F / 12.0F);
    const auto high = std::pow(2.0F, 0.792F / 12.0F);
    expect(std::abs(retail_sound_pitch_ratio(-0.8F, 0.8F, 0U) - low) < 0.000001F,
           "the low pitch endpoint must use retail truncation");
    expect(std::abs(retail_sound_pitch_ratio(-0.8F, 0.8F, 66U) - 1.0F) < 0.000001F,
           "the inclusive random pitch range must contain exact unity");
    expect(std::abs(retail_sound_pitch_ratio(-0.8F, 0.8F, 132U) - high) < 0.000001F,
           "the high pitch endpoint must be inclusive");
}

void every_explosion_bank_and_water_variant_resolves() {
    const auto root = sound_root();
    constexpr std::array<std::uint8_t, 13U> tools{
        11U, 12U, 13U, 14U, 20U, 21U, 31U, 33U, 54U, 55U, 57U, 58U, 59U};
    for (const auto tool : tools) {
        for (const bool submerged : {false, true}) {
            const auto authored = battlespades::audio::retail_explosion_sound(tool, submerged);
            const auto stems = sound_group_stems(authored.group);
            expect(!stems.empty(), "an explosion bank must not resolve empty");
            for (const auto& stem : stems) {
                expect(std::filesystem::is_regular_file(root / (stem + ".ogg")),
                       "missing explosion sample " + stem + ".ogg");
            }
        }
    }
    expect(battlespades::audio::retail_explosion_sound(54U, false).group ==
               "AoS_soundfx_PLAYER_specialist_wpn_chem_bomb_explode_001-005",
           "chemical bombs must use their five-sample Specialist bank");
    expect(battlespades::audio::retail_explosion_sound(58U, true).group ==
               "AoS_soundfx_PLAYER_engineer__wpn_mine_launcher_mine_explode_water_001-004",
           "mine-launcher impacts must use their authored water bank");
}

void deployable_place_banks_resolve_with_authored_variation() {
    const auto root = sound_root();
    constexpr std::array<std::uint8_t, 7U> entities{8U, 9U, 10U, 30U, 36U, 37U, 38U};
    for (const auto entity : entities) {
        for (const bool submerged : {false, true}) {
            const auto authored =
                battlespades::audio::retail_entity_place_sound(entity, submerged);
            const auto stems = sound_group_stems(authored.group);
            expect(!stems.empty(), "a mapped entity placement bank must not be empty");
            for (const auto& stem : stems) {
                expect(std::filesystem::is_regular_file(root / (stem + ".ogg")),
                       "missing entity placement sample " + stem + ".ogg");
            }
        }
    }
    expect(sound_group_stems(
               battlespades::audio::retail_entity_place_sound(30U, false).group).size() == 3U,
           "Med Pack placement must retain all three retail variants");
    expect(sound_group_stems(
               battlespades::audio::retail_entity_place_sound(38U, false).group).size() == 3U,
           "C4 placement must retain all three retail variants");
    expect(battlespades::audio::retail_entity_place_sound(37U, false).pitch_semitones[0U] ==
               -0.2F,
           "a dry Mine Launcher attach must retain its authored pitch range");
}

void every_mapped_sound_resolves_on_disk() {
    const auto root = sound_root();
    expect(std::filesystem::is_directory(root),
           "the original sound tree must be present at " + root.string());

    std::vector<std::string> missing;
    for (const auto& cue : every_cue()) {
        if (cue.group == known_missing_group) {
            continue;
        }
        const auto stems = sound_group_stems(cue.group);
        expect(!stems.empty(), "a non-empty cue must expand to at least one stem");
        for (const auto& stem : stems) {
            if (!std::filesystem::is_regular_file(root / (stem + ".ogg"))) {
                missing.emplace_back(std::string{cue.weapon} + "." +
                                     std::string{cue.role} + " -> " + stem + ".ogg");
            }
        }
    }
    if (!missing.empty()) {
        std::string report = "catalog cues with no OGG on disk:";
        for (const auto& entry : missing) {
            report += "\n  " + entry;
        }
        throw std::runtime_error{report};
    }
}

void every_structure_collapse_bank_resolves_on_disk() {
    const auto root = sound_root();
    constexpr std::array<std::size_t, 3U> representative_block_counts{{1U, 15U, 80U}};

    std::vector<std::string> missing;
    const auto verify_group = [&](TerrainSoundKind kind,
                                  std::size_t block_count,
                                  bool submerged) {
        const auto group = falling_sound_group(kind, block_count, submerged);
        const auto stems = sound_group_stems(group);
        expect(!stems.empty(), "a collapse sound bank must expand to at least one stem");
        for (const auto& stem : stems) {
            if (!std::filesystem::is_regular_file(root / (stem + ".ogg"))) {
                missing.emplace_back(std::string{group} + " -> " + stem + ".ogg");
            }
        }
    };

    for (const auto block_count : representative_block_counts) {
        verify_group(TerrainSoundKind::structure_split, block_count, false);
        verify_group(TerrainSoundKind::structure_break, block_count, false);
        verify_group(TerrainSoundKind::structure_break, block_count, true);
    }

    if (!missing.empty()) {
        std::string report = "collapse sound banks with no OGG on disk:";
        for (const auto& entry : missing) {
            report += "\n  " + entry;
        }
        throw std::runtime_error{report};
    }
}

void the_known_retail_hole_is_still_a_hole() {
    // If this ever starts failing the asset appeared, and the exemption above
    // should be deleted rather than widened.
    const auto path = sound_root() / "snowcan_reload.ogg";
    expect(!std::filesystem::is_regular_file(path),
           "snowcan_reload.ogg now exists; remove the documented exemption");
    const auto uses_it = std::ranges::any_of(weapon_catalog(), [](const auto& weapon) {
        return weapon.reload_sound == known_missing_group;
    });
    expect(uses_it, "the snow blower must still declare its unshipped reload cue");
}

void loop_fire_weapons_all_have_a_sustained_loop() {
    // Retail sets shoot_sound = BLANK_SOUND on these precisely because the
    // sustained loop lives in update(). An automatic weapon with neither a
    // shoot sound nor a fire loop would be silent when fired, which is the
    // exact defect this whole port exists to remove.
    for (const auto& weapon : weapon_catalog()) {
        const bool loop_fire = weapon.mechanism == WeaponMechanism::firearm_automatic ||
                               weapon.mechanism == WeaponMechanism::firearm_spinup ||
                               weapon.mechanism == WeaponMechanism::deployed_machine_gun;
        if (!loop_fire) {
            continue;
        }
        expect(!weapon.shoot_sound.empty() || !weapon.sounds.fire_loop.empty(),
               std::string{weapon.symbolic_name} +
                   " fires automatically but has neither a shoot sound nor a fire loop");
    }
}

void a_fire_loop_always_has_a_tail() {
    for (const auto& weapon : weapon_catalog()) {
        if (weapon.sounds.fire_loop.empty()) {
            continue;
        }
        expect(!weapon.sounds.fire_tail.empty(),
               std::string{weapon.symbolic_name} +
                   " has a fire loop with no tail; the loop would cut off abruptly");
    }
}

void every_melee_tool_has_its_swing_and_impact_cues() {
    for (const auto& weapon : weapon_catalog()) {
        if (!weapon.melee) {
            continue;
        }
        expect(!weapon.sounds.melee_miss.empty(),
               std::string{weapon.symbolic_name} + " is melee but has no swing cue");
        expect(!weapon.sounds.melee_hit_block.empty(),
               std::string{weapon.symbolic_name} + " is melee but has no block-hit cue");
        expect(!weapon.sounds.melee_hit_player.empty(),
               std::string{weapon.symbolic_name} + " is melee but has no player-hit cue");
    }
}

void the_recovered_loop_weapons_are_all_present() {
    // The seven loop-fire tools recovered from retail. Pinned by id so a
    // regenerate that silently drops one fails here rather than in the ear.
    struct Expected final {
        std::uint8_t tool_id;
        std::string_view fire_loop;
    };
    constexpr std::array<Expected, 5U> expected{{
        {7U, "smg_fire_loop"},
        {8U, "minigun_fire_loop"},
        {35U, "tommygun_fire_loop"},
        {38U, "classic_smg_fire_loop"},
        {61U, "AoS_soundfx_PLAYER_medic_wpn_LMG_loop_fire_001-002"},
    }};
    for (const auto& entry : expected) {
        const auto* weapon = find_weapon_definition(entry.tool_id);
        expect(weapon != nullptr, "recovered loop tool must exist in the catalog");
        expect(weapon->sounds.fire_loop == entry.fire_loop,
               std::string{"tool "} + std::to_string(entry.tool_id) +
                   " lost its recovered fire loop");
    }
    const auto* minigun = find_weapon_definition(8U);
    expect(minigun != nullptr && minigun->sounds.spin_loop == "minigun_loop",
           "the minigun must keep its separate barrel spin loop");
}

} // namespace

int main() {
    try {
        group_expansion_follows_the_retail_convention();
        authored_pitch_metadata_and_quantization_match_retail();
        every_explosion_bank_and_water_variant_resolves();
        deployable_place_banks_resolve_with_authored_variation();
        every_mapped_sound_resolves_on_disk();
        every_structure_collapse_bank_resolves_on_disk();
        the_known_retail_hole_is_still_a_hole();
        loop_fire_weapons_all_have_a_sustained_loop();
        a_fire_loop_always_has_a_tail();
        every_melee_tool_has_its_swing_and_impact_cues();
        the_recovered_loop_weapons_are_all_present();
        std::cout << "weapon audio map tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
