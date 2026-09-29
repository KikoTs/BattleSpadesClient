#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace battlespades::world {
namespace {

constexpr std::uint8_t pistol_tool_id{17U};
constexpr std::uint8_t spade_tool_id{2U};
constexpr std::uint8_t block_tool_id{5U};
constexpr std::uint8_t minigun_tool_id{8U};
constexpr std::uint8_t zombie_hand_tool_id{24U};
constexpr std::uint8_t zombie_prefab_tool_id{28U};
/** WeaponViewModelInput's default: no use of the tool is in flight. */
constexpr double idle_seconds_since_primary{1.0e8};

[[nodiscard]] constexpr ViewModelVector add(ViewModelVector left,
                                             ViewModelVector right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr ViewModelVector scale(ViewModelVector value,
                                               double factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] RetailModelPose add(const RetailModelPose& left,
                                  const RetailModelPose& right) noexcept {
    return {add(left.position, right.position),
            add(left.orientation_degrees, right.orientation_degrees)};
}

[[nodiscard]] RetailModelPose weapon_shoot_animation(double elapsed,
                                                      double length) noexcept {
    RetailModelPose result;
    // AnimWeaponShoot.start() explicitly starts at the identity pose. Its
    // first update uses the remaining timer, hence the strict elapsed > 0.
    if (elapsed <= 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    const double f = -timer / 32.0;
    result.position = {f * 10.0, f * 10.0, f * 10.0};
    result.orientation_degrees.x = f * 280.0;
    return result;
}

[[nodiscard]] RetailModelPose place_block_animation(double elapsed,
                                                     double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    // AnimPlaceBlock.start() = (-length,-length,0); update adds dt to x/y.
    result.position = {-timer, -timer, 0.0};
    return result;
}

[[nodiscard]] RetailModelPose use_spade_animation(double elapsed,
                                                  double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    double f{};
    double f2{};
    if (timer >= 0.6) {
        f2 = 0.0;
        f = 1.0 - timer;
    } else if (timer >= 0.3) {
        f2 = 0.6 - timer;
        f = 0.4;
    } else if (timer >= 0.1) {
        f2 = 0.3;
        f = 0.4;
    } else {
        f2 = timer * 3.0;
        f = timer * 4.0;
    }

    if (elapsed == 0.0) {
        // AnimUseSpade.start() uses the opposite signed pose; update() flips
        // it on the first simulation update. This discontinuity is retail.
        result.position = {-(f2 / 16.0 * 3.0), f / 1.3, -f2 / 0.6};
        result.orientation_degrees = {f / -0.32 * 60.0, 0.0, f * 30.0};
    } else {
        result.position = {f2 / 16.0 * 3.0, -(f / 1.3), f2 / 0.6};
        result.orientation_degrees = {-f / -0.32 * 60.0, 0.0, -f * 30.0};
    }
    return result;
}

/**
 * AnimUsePickAxe / AnimUseKnife / AnimUseCrowbar / AnimUseMachete /
 * AnimUseRiotStick (identical bodies) and AnimUseRiotShield (z only).
 *
 * Every one of their start() methods zeroes position and orientation, and
 * Character.update_weapon runs Tool.update before use_primary, so the start
 * frame draws the rest pose; the first update() then jumps to the 0.4/-0.5/
 * 120 degree wind-up and eases back to rest across shoot_interval.
 */
[[nodiscard]] RetailModelPose use_melee_animation(double elapsed,
                                                  double length,
                                                  bool shield) noexcept {
    RetailModelPose result;
    if (!(elapsed > 0.0) || elapsed >= length || length <= 0.0) {
        return result;
    }
    const double progress = std::clamp(elapsed / length, 0.0, 1.0);
    result.position.z = 0.4 - progress * 0.4;
    if (!shield) {
        result.position.y = -0.5 + progress * 0.5;
        result.orientation_degrees.x = 120.0 - progress * 120.0;
    }
    return result;
}

[[nodiscard]] RetailModelPose throw_animation(double elapsed,
                                              double length,
                                              bool retain_end) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || length <= 0.0 || (!retain_end && elapsed >= length)) {
        return result;
    }
    elapsed = std::min(elapsed, length);
    result.position.x = -elapsed * 1.5;
    result.position.y = elapsed * 0.5;
    result.orientation_degrees.x = elapsed * -40.0;
    return result;
}

[[nodiscard]] RetailModelPose zombie_hand_animation(double elapsed,
                                                    double length) noexcept {
    RetailModelPose result;
    if (elapsed < 0.0 || elapsed >= length) {
        return result;
    }
    const double timer = length - elapsed;
    const double factor = timer * 6.0;
    if (elapsed == 0.0) {
        result.position = {0.0, -factor / 3.0, -factor / 6.5};
        result.orientation_degrees.x = -(40.0 - length * 200.0);
    } else {
        result.position = {0.0, factor / 3.0, factor / 6.5};
        result.orientation_degrees.x = 40.0 - timer * 200.0;
    }
    return result;
}

/** Tool.shoot_interval (the Animation length), falling back to the rate. */
[[nodiscard]] double animation_length(const WeaponDefinition& weapon) noexcept {
    const double interval =
        weapon.retail.use.shoot_interval.value_or(weapon.fire_interval);
    return std::max(interval > 0.0 ? interval : weapon.fire_interval, 0.001);
}

void set_all_parts(RetailToolHold& hold, ViewModelVector position,
                   ViewModelVector orientation = {}) noexcept {
    for (auto& part : hold.parts) {
        part.position = position;
        part.orientation_degrees = orientation;
    }
}

} // namespace

RetailToolAnimationFamily retail_tool_animation_family(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case 2U:  // SpadeTool
    case 3U:  // SuperSpadeTool
    case 4U:  // ClassicSpadeTool
    case 45U: // UGCSuperSpadeTool
        return RetailToolAnimationFamily::use_spade;
    case 0U:  // PickAxeTool      AnimUsePickAxe
    case 1U:  // KnifeTool        AnimUseKnife
    case 34U: // CrowbarTool      AnimUseCrowbar
    case 44U: // UGCPickAxeTool   (inherits AnimUsePickAxe)
    case 49U: // RiotStickTool    AnimUseRiotStick
    case 50U: // MacheteTool      AnimUseMachete
        return RetailToolAnimationFamily::use_melee;
    case 52U:
        return RetailToolAnimationFamily::use_riot_shield;
    case 24U:
        return RetailToolAnimationFamily::zombie_hand;
    case 5U:  // BlockTool (main only: started in on_stop_primary)
    case 22U: // FlareBlockTool (placement state exists only for main)
    case 23U: // PrefabTool
    case 27U: // SHRAPNEL_TOOL -> BlockTool
    case 28U: // ZombiePrefabTool (PrefabTool.use_primary)
    case 41U: // UGCTool (on_start_primary, main only)
        return RetailToolAnimationFamily::place_block;
    case 16U: // RocketTurretWeapon
    case 20U: // LandmineWeapon
    case 21U: // DynamiteWeapon
    case 51U: // MedPackWeapon
    case 56U: // RadarStationWeapon
    case 59U: // C4Weapon
        // Weapon.use_primary starts AnimWeaponShoot and its shoot() override
        // starts AnimPlaceBlock on the same frame; apply_animations sums both.
        return RetailToolAnimationFamily::weapon_shoot_and_place_block;
    case 11U: // GrenadeTool           AnimThrowGrenade(fuse)
    case 31U: // ClassicGrenadeTool
    case 32U: // AntipersonnelGrenadeTool
        return RetailToolAnimationFamily::throw_cooked;
    case 33U: // MolotovWeapon         AnimThrowGrenade(A1645, stop_on_end=False)
    case 54U: // ChemicalBombWeapon    AnimThrowGrenade(A1662, stop_on_end=False)
    case 57U: // StickyGrenadeWeapon   AnimThrowGrenade(A1681, stop_on_end=False)
        return RetailToolAnimationFamily::throw_charged;
    case 25U: // BombTool
    case 26U: // DiamondTool
    case 30U: // IntelTool
    case 39U: // NullTool
    case 40U: // FakePistolTool
    case 42U: // UGCPrefabTool never starts its place_block animation
    case 43U: // PaintbrushTool
    case 63U: // BlockSuckerWeapon.play_shoot_animation = False
    case 64U: // DisguiseTool
        return RetailToolAnimationFamily::none;
    default:
        // Every remaining id is a Weapon subclass that keeps
        // play_shoot_animation=True and Weapon.use_primary.
        return find_weapon_definition(tool_id) != nullptr
                   ? RetailToolAnimationFamily::weapon_shoot
                   : RetailToolAnimationFamily::none;
    }
}

bool retail_tool_is_digging_tool(std::uint8_t tool_id) noexcept {
    switch (tool_id) {
    case 0U:
    case 1U:
    case 2U:
    case 3U:
    case 4U:
    case 24U:
    case 34U:
    case 44U:
    case 45U:
    case 49U:
    case 50U:
    case 52U:
        return true;
    default:
        return false;
    }
}

RetailToolHold retail_tool_hold(std::uint8_t tool_id, bool main_character) noexcept {
    RetailToolHold hold;
    // Values are the Tool subclasses' __init__ writes. Nearly all of them sit
    // behind `if character.main:`, so remote characters keep the zero
    // initial_position; arms_position_offset is assigned unconditionally but
    // only draw_fps (main character) ever reads it.
    switch (tool_id) {
    case 1U: // KnifeTool
        if (main_character) {
            set_all_parts(hold, {-0.05, 0.03, -0.05});
        }
        hold.arms_position_offset = {0.05, -0.03, 0.05};
        break;
    case 5U:  // BlockTool
    case 22U: // FlareBlockTool
    case 23U: // PrefabTool
    case 27U: // SHRAPNEL_TOOL -> BlockTool
        if (main_character) {
            set_all_parts(hold, {-0.04, 0.0, 0.3}, {0.0, 45.0, 0.0});
        }
        hold.arms_position_offset = {0.04, 0.0, -0.3};
        break;
    case 8U: // MinigunWeapon: initial_position[1] for every character.
        hold.parts[1U].position = {0.0, -0.30000000000000004, 1.1};
        break;
    case 10U: // Shotgun2Weapon
    case 12U: // RPGWeapon
    case 14U: // DrillgunWeapon
    case 18U: // SniperWeapon
    case 29U: // SnowBlowerWeapon
    case 47U: // UGCDrillgunWeapon (inherits DrillgunWeapon.__init__)
    case 48U: // UGCSnowBlowerWeapon (inherits SnowBlowerWeapon.__init__)
    case 57U: // StickyGrenadeWeapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.1, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.1, 0.0};
        break;
    case 13U: // RPG2Weapon: the one class with a remote initial_position.
    case 46U: // UGCRPG2Weapon
        set_all_parts(hold, {0.0, main_character ? 0.1 : 0.3, 0.0});
        // arms_position_offset = initial_position[0] * -1
        hold.arms_position_offset = {0.0, main_character ? -0.1 : -0.3, 0.0};
        break;
    case 16U: { // RocketTurretWeapon: per-part, both characters, *0.05.
        hold.arms_position_offset = {0.0, -0.18, 0.0};
        const auto part = [&](double y) {
            return scale(add({-15.0, y, 10.0}, scale(hold.arms_position_offset, -1.0)), 0.05);
        };
        hold.parts[0U].position = part(0.0);
        hold.parts[1U].position = part(main_character ? 14.0 : 0.0);
        hold.parts[2U].position = part(main_character ? 8.0 : 0.0);
        break;
    }
    case 19U: // Sniper2Weapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.2, 0.1});
        }
        hold.arms_position_offset = {0.0, -0.2, -0.1};
        break;
    case 20U: // LandmineWeapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.18, 0.0}, {0.0, 70.0, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.18, 0.0};
        break;
    case 21U: // DynamiteWeapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.18, 0.0}, {-20.0, 200.0, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.18, 0.0};
        break;
    case 15U: // MGWeapon (view_model is empty, so this never reaches a pose)
    case 25U: // BombTool
    case 26U: // DiamondTool
    case 30U: // IntelTool
        if (main_character) {
            set_all_parts(hold, {0.0, 0.18, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.18, 0.0};
        break;
    case 24U: // ZombieHandTool
        if (main_character) {
            set_all_parts(hold, {0.0, 0.0, -0.25});
        }
        hold.arms_position_offset = {0.0, 0.0, 0.25};
        break;
    case 28U: // ZombiePrefabTool: PrefabTool.__init__, then per-part overrides.
        if (main_character) {
            hold.parts[0U].orientation_degrees = {0.0, 0.0, 180.0};
            hold.parts[1U].position = {0.0, 0.2, 0.8};
            hold.parts[1U].orientation_degrees = {0.0, 45.0, 30.0};
        }
        hold.arms_position_offset = {0.04, 0.0, -0.3};
        break;
    case 32U: // AntipersonnelGrenadeTool
        if (main_character) {
            set_all_parts(hold, {0.0, 0.2, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.2, 0.0};
        break;
    case 35U: // TommyGunWeapon
    case 36U: // SnubPistolWeapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.12, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.12, 0.0};
        break;
    case 42U: // UGCPrefabTool: initial = -arms offset, orientation reset to 0.
        if (main_character) {
            set_all_parts(hold, {-0.04, 0.0, 0.3});
        }
        hold.arms_position_offset = {0.04, 0.0, -0.3};
        break;
    case 49U: // RiotStickTool: arms offset only, the stick stays at origin.
    case 50U: // MacheteTool
        hold.arms_position_offset = {0.0, 0.03, -0.05};
        break;
    case 51U: // MedPackWeapon
        if (main_character) {
            set_all_parts(hold, {-0.1, -0.18, 0.45});
        }
        hold.arms_position_offset = {0.0, 0.1, -0.2};
        break;
    case 52U: // RiotShieldTool
        if (main_character) {
            set_all_parts(hold, {0.45, -0.6, -0.2});
        }
        hold.arms_position_offset = {0.05, -0.03, 0.05};
        break;
    case 53U: // AutoPistolWeapon
        if (main_character) {
            set_all_parts(hold, {-0.15, -0.05, 0.2});
        }
        hold.arms_position_offset = {0.0, 0.05, -0.05};
        break;
    case 54U: // ChemicalBombWeapon
        if (main_character) {
            set_all_parts(hold, {-0.02, 0.2, 0.0});
        }
        hold.arms_position_offset = {0.02, -0.2, 0.0};
        break;
    case 55U: // GrenadeLauncherWeapon
        if (main_character) {
            set_all_parts(hold, {-0.15, 0.12, 0.1});
        }
        hold.arms_position_offset = {0.1, -0.18, 0.0};
        break;
    case 56U: // RadarStationWeapon
        if (main_character) {
            hold.parts[0U].position = {-0.1, 0.6, 0.2};
        }
        hold.arms_position_offset = {0.0, -0.66, -0.1};
        break;
    case 58U: // MineLauncherWeapon
        if (main_character) {
            set_all_parts(hold, {-0.15, 0.15, 0.0});
        }
        hold.arms_position_offset = {0.0, -0.1, 0.0};
        break;
    case 59U: // C4Weapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.18, 0.0});
        }
        hold.arms_position_offset = {-0.1, -0.1, 0.2};
        break;
    case 60U: // AssaultRifleWeapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.2, 0.0});
        }
        hold.arms_position_offset = {-0.1, -0.08, -0.1};
        break;
    case 61U: // LightMachineGunWeapon: no arms_position_offset assignment.
        if (main_character) {
            set_all_parts(hold, {0.0, 0.0, 0.25});
        }
        break;
    case 62U: // AutoShotgunWeapon
        if (main_character) {
            set_all_parts(hold, {0.0, 0.125, 0.13});
        }
        hold.arms_position_offset = {0.0, -0.1, -0.2};
        break;
    case 63U: // BlockSuckerWeapon (initial_position explicitly zero)
        hold.arms_position_offset = {-0.3, -0.1, 0.4};
        break;
    case 64U: // DisguiseTool
        if (main_character) {
            set_all_parts(hold, {-0.1, 0.0, 0.05});
        }
        hold.arms_position_offset = {0.0, 0.01, 0.0};
        break;
    default:
        // Pickaxe, spades, crowbar, UGC pickaxe/superspade, rifle, SMG,
        // shotgun, classic shotgun/SMG, pistol, fake pistol, grenade, classic
        // grenade, molotov, paintbrush (offset 0), UGC tool (model_offset and
        // arms offset cancel; default item offset is zero) and NullTool keep
        // the Tool defaults.
        break;
    }
    return hold;
}

RetailModelPose evaluate_retail_tool_animation(std::uint8_t tool_id,
                                               double seconds_since_primary,
                                               bool main_character) noexcept {
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr) {
        return {};
    }
    const double length = animation_length(*weapon);
    switch (retail_tool_animation_family(tool_id)) {
    case RetailToolAnimationFamily::weapon_shoot:
        return weapon_shoot_animation(seconds_since_primary, length);
    case RetailToolAnimationFamily::weapon_shoot_and_place_block: {
        auto result = weapon_shoot_animation(seconds_since_primary, length);
        // The shoot() overrides return early for remote characters.
        if (main_character) {
            result = add(result, place_block_animation(seconds_since_primary, length));
        }
        return result;
    }
    case RetailToolAnimationFamily::place_block:
        // BlockTool/FlareBlockTool/UGCTool only start the animation on the
        // local placing path; PrefabTool.use_primary starts it for everyone.
        if (!main_character && tool_id != 23U && tool_id != zombie_prefab_tool_id) {
            return {};
        }
        return place_block_animation(seconds_since_primary, length);
    case RetailToolAnimationFamily::use_spade:
        return use_spade_animation(seconds_since_primary, length);
    case RetailToolAnimationFamily::use_melee:
        return use_melee_animation(seconds_since_primary, length, false);
    case RetailToolAnimationFamily::use_riot_shield:
        return use_melee_animation(seconds_since_primary, length, true);
    case RetailToolAnimationFamily::zombie_hand:
        return zombie_hand_animation(seconds_since_primary, length);
    case RetailToolAnimationFamily::throw_cooked:
        // Started on press, stopped on release: an observer only learns of
        // the throw after on_stop_primary has already stopped it.
        if (!main_character) {
            return {};
        }
        return throw_animation(seconds_since_primary,
                               weapon->retail.use.fuse.value_or(length), false);
    case RetailToolAnimationFamily::throw_charged: {
        if (!main_character) {
            return {};
        }
        // stop_on_end=False keeps the drawn-back pose while the button is
        // held; on_stop_primary stops it. Callers pass the 1e9 "idle"
        // sentinel when no throw is being held.
        if (seconds_since_primary >= idle_seconds_since_primary) {
            return {};
        }
        // MolotovWeapon uses A1645 = 3 s; chemical and sticky bombs use 1 s.
        const double charge = tool_id == 33U ? 3.0 : 1.0;
        return throw_animation(seconds_since_primary, charge, true);
    }
    case RetailToolAnimationFamily::none:
        break;
    }
    return {};
}

WeaponViewModelInput compose_weapon_view_model_input(
    std::uint8_t tool_id, double seconds_since_primary,
    std::uint64_t action_serial, ViewModelVector base_sway,
    double pullout_remaining, double mechanism_phase) noexcept {
    const double pullout = std::max(0.0, pullout_remaining);
    WeaponViewModelInput result;
    result.tool_id = tool_id;
    result.seconds_since_primary = seconds_since_primary;
    result.action_serial = action_serial;
    result.sway_x = base_sway.x - pullout * 5.0;
    result.sway_y = base_sway.y - pullout * 5.0;
    result.sway_z = base_sway.z;
    result.mechanism_phase = mechanism_phase;
    return result;
}

RetailViewModelPose
evaluate_retail_view_model(const RetailViewModelInput& input) noexcept {
    std::uint8_t tool_id{pistol_tool_id};
    switch (input.tool) {
    case TutorialTool::pistol:
        tool_id = pistol_tool_id;
        break;
    case TutorialTool::spade:
        tool_id = spade_tool_id;
        break;
    case TutorialTool::block:
        tool_id = block_tool_id;
        break;
    }
    return evaluate_weapon_view_model(
        {tool_id, input.seconds_since_primary, 0U, input.sway_x, input.sway_y,
         input.sway_z});
}

RetailViewModelPose
evaluate_weapon_view_model(const WeaponViewModelInput& input) noexcept {
    RetailViewModelPose result;
    // Recovered from character.pyd Character.draw_fps, lines 2167-2177.
    result.arm_parts = {{
        {{0.29, -0.059, 0.10}, -25.0},
        {{-0.12, -0.061, 0.60}, -50.0},
        {{-0.48, -0.06, 0.48}, 0.0},
    }};

    const auto* weapon = find_weapon_definition(input.tool_id);
    if (weapon == nullptr) {
        return result;
    }
    const ViewModelVector sway{input.sway_x, input.sway_y, input.sway_z};
    result.model_scale = weapon->retail.use.view_model_size.value_or(0.05);
    result.tool_sway = sway;
    result.draws_player_arms =
        input.tool_id != zombie_hand_tool_id && input.tool_id != zombie_prefab_tool_id;
    // DiggingTool.rotate_arm_ratio; every other Tool keeps 1.0.
    result.arm_rotation_ratio = retail_tool_is_digging_tool(input.tool_id) ? 0.25 : 1.0;
    // ZombieHandTool.model_scale=0.5 is applied by Character.draw to the
    // third-person weapon hierarchy. The live local character keeps
    // view_weapon.size at the inherited view_model_size (0.05), so applying
    // the tool scale here incorrectly halves and recentres both FPS hands.
    result.tool_part_count =
        std::min<std::size_t>(weapon->first_person_models.size(),
                              result.tool_parts.size());

    // Tool.__init__ initial_position/initial_orientation (main character)
    // plus every playing Animation, summed per part by Tool.apply_animations.
    // They are not KV6 pivots: the same translation also anchors the hands.
    const auto hold = retail_tool_hold(input.tool_id, true);
    const auto animation =
        evaluate_retail_tool_animation(input.tool_id, input.seconds_since_primary, true);
    for (std::size_t index{}; index < result.tool_parts.size(); ++index) {
        result.tool_parts[index] = add(hold.parts[index], animation);
    }
    result.tool = result.tool_parts[0U];

    if (input.tool_id == minigun_tool_id && result.tool_part_count >= 2U) {
        // MinigunWeapon.apply_animations adds AnimRoll only to the barrel.
        // RMB can spool without firing; primary continues the same phase.
        result.tool_parts[1U].orientation_degrees.z +=
            std::clamp(input.mechanism_phase, 0.0, 1.0) * 360.0;
    }
    if (input.tool_id == zombie_hand_tool_id) {
        // ZombieHandTool.apply_transform applies AnimZombieHand only to
        // last_used_hand; the inactive hand keeps its (0,0,-0.25) pose.
        for (std::size_t index{}; index < result.tool_parts.size(); ++index) {
            result.tool_parts[index] = hold.parts[index];
        }
        if (input.action_serial > 0U && result.tool_part_count >= 2U) {
            const auto active_hand =
                static_cast<std::size_t>((input.action_serial - 1U) % 2U);
            result.tool_parts[active_hand].position =
                add(result.tool_parts[active_hand].position, animation.position);
            result.tool_parts[active_hand].orientation_degrees =
                animation.orientation_degrees;
        }
        result.tool = result.tool_parts[0U];
    }

    // Tool.get_arms_position/get_arms_orientation read model 0 and return
    // zero when the tool has no models (MGWeapon, NullTool). Character.
    // draw_fps then adds the same sway/pullout translation it gave the tool.
    // Keeping ZombiePrefabTool's inherited PrefabTool offset keeps the
    // semantic pose exact even though that tool hides the class arms.
    if (result.tool_part_count > 0U) {
        result.arms_position =
            add(add(result.tool_parts[0U].position, hold.arms_position_offset), sway);
        result.arms_orientation_degrees = result.tool_parts[0U].orientation_degrees;
    } else {
        result.arms_position = sway;
        result.arms_orientation_degrees = {};
    }
    return result;
}

RetailSightPose evaluate_weapon_sight(std::uint8_t tool_id) noexcept {
    RetailSightPose result;
    const auto* weapon = find_weapon_definition(tool_id);
    if (weapon == nullptr) {
        return result;
    }
    const auto& sight = weapon->retail.use.sight_position;
    // character.pyx:2074-2082 resets the matrix, rotates 180 degrees around
    // Y, then draw_scaled(sight, .05, sx+.025, sy-.35, sz+1.85).
    result.position = {sight[0U] + 0.025, sight[1U] - 0.35,
                       sight[2U] + 1.85};
    // character.pyx:2083-2089 draws the pin from the ALREADY-OFFSET X/Y/Z,
    // not from sight_pos: a near rear notch and a far front post 2.1 units
    // apart. Only classicRifleWeapon.py:33 assigns one.
    result.has_pin = !weapon->pin_model.asset.empty();
    result.pin_scale = weapon->retail.use.pin_scale;
    result.pin_position = {result.position.x - 0.015, result.position.y + 0.3,
                           result.position.z + 2.1};
    return result;
}

std::optional<RetailViewMuzzleFlash> retail_view_muzzle_flash(std::uint8_t tool_id) noexcept {
    // Class attributes of the stock aoslib/weapons subclasses; the Weapon
    // defaults are scale 0.5, duration 0.05 and a (0,0,0) zoomed offset.
    constexpr ViewModelVector common_zoom{0.0, -0.1, 3.0};
    switch (tool_id) {
    case 7U: // SMGWeapon (duration 0.05 explicitly)
    case 53U: // AutoPistolWeapon
    case 60U: // AssaultRifleWeapon
    case 61U: // LightMachineGunWeapon
        return RetailViewMuzzleFlash{{0.0, 0.12, 0.5}, common_zoom, 0.5, 0.05};
    case 38U: // ClassicSmgWeapon
        return RetailViewMuzzleFlash{{0.0, 0.12, 0.5}, common_zoom, 0.5, 0.01};
    case 8U: // MinigunWeapon: 0.12 - 0.3, no zoomed override (can_zoom = False)
        return RetailViewMuzzleFlash{{0.0, 0.12 - 0.30000000000000004, 2.0}, {}, 0.75, 0.05};
    case 9U:  // ShotgunWeapon
    case 10U: // Shotgun2Weapon
    case 37U: // ClassicShotgunWeapon
        return RetailViewMuzzleFlash{{-0.05, 0.12, 0.8}, common_zoom, 1.0, 0.05};
    case 62U: // AutoShotgunWeapon
        return RetailViewMuzzleFlash{{-0.05, 0.12, 0.8}, common_zoom, 0.6, 0.05};
    case 15U: // MGWeapon (undeployed draw_fps; the deployed flash is draw_manned)
        return RetailViewMuzzleFlash{{0.0, 0.18, 2.0}, common_zoom, 0.75, 0.01};
    case 17U: // PistolWeapon
        return RetailViewMuzzleFlash{{-0.05, 0.12, 0.35}, {0.0, -0.1, 1.5}, 0.5, 0.05};
    case 18U: // SniperWeapon: no zoomed override, so the aimed flash sits at the eye
    case 19U: // Sniper2Weapon
        return RetailViewMuzzleFlash{{0.0, 0.12, 0.9}, {}, 0.5, 0.05};
    case 35U: // TommyGunWeapon
        return RetailViewMuzzleFlash{{0.0, 0.35, 1.2}, common_zoom, 0.5, 0.01};
    case 36U: // SnubPistolWeapon
        return RetailViewMuzzleFlash{{0.0, 0.34, 0.5}, {0.0, -0.2, 2.5}, 0.5, 0.05};
    default:
        // ClassicRifleWeapon (6) only sets the third-person muzzle_flash_display;
        // launchers, throwables, melee and deployables have no flash at all.
        return std::nullopt;
    }
}

std::optional<RetailViewMuzzleFlashPose>
evaluate_retail_view_muzzle_flash(std::uint8_t tool_id, bool zoomed, ViewModelVector sway,
                                  double roll_degrees) noexcept {
    const auto flash = retail_view_muzzle_flash(tool_id);
    const auto* weapon = find_weapon_definition(tool_id);
    if (!flash.has_value() || weapon == nullptr) {
        return std::nullopt;
    }
    RetailViewMuzzleFlashPose result;
    // Character.draw_fps sets view_weapon.size = tool.view_model_size every
    // hip frame; draw_sight reuses that (unchanged) size.
    result.model_scale = weapon->retail.use.view_model_size.value_or(0.05) * flash->scale;
    result.roll_degrees = roll_degrees;
    if (zoomed) {
        // character.pyx:2072-2074,2092: identity, R_y(180), then draw_sight.
        result.position = flash->zoomed_view_offset;
    } else {
        // character.pyx: glTranslatef(sway.x-0.4, sway.y-0.55, sway.z+0.9) is
        // still on the stack when weapon_object.draw_fps(view_weapon) runs.
        const RetailViewModelPose root;
        result.position = add(add(root.character_offset, sway), flash->view_offset);
    }
    return result;
}

std::optional<std::array<float, 3U>>
view_model_muzzle_tip(const ChunkMesh& mesh) noexcept {
    if (mesh.vertices.empty()) {
        return std::nullopt;
    }
    float front = mesh.vertices.front().z;
    for (const auto& vertex : mesh.vertices) {
        front = std::max(front, vertex.z);
    }
    double sum_x{};
    double sum_y{};
    std::size_t count{};
    for (const auto& vertex : mesh.vertices) {
        if (vertex.z >= front - 1.01F) {
            sum_x += vertex.x;
            sum_y += vertex.y;
            ++count;
        }
    }
    if (count == 0U) {
        return std::nullopt;
    }
    return std::array<float, 3U>{static_cast<float>(sum_x / static_cast<double>(count)),
                                 static_cast<float>(sum_y / static_cast<double>(count)),
                                 front};
}

std::array<float, 3U> anchored_view_muzzle_flash_centre(std::array<float, 3U> tip,
                                                        double flash_scale) noexcept {
    const double scale = std::isfinite(flash_scale) && flash_scale > 0.0 ? flash_scale : 0.0;
    return {tip[0U], tip[1U],
            tip[2U] + static_cast<float>(view_muzzle_flash_lead_voxels * scale)};
}

} // namespace battlespades::world
