#include "battlespades/world/weapon_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace battlespades::world {
namespace {

[[nodiscard]] bool repeats_while_held(const WeaponDefinition& weapon) noexcept {
    return weapon.mechanism == WeaponMechanism::melee ||
           weapon.mechanism == WeaponMechanism::firearm_automatic ||
           weapon.mechanism == WeaponMechanism::shotgun_automatic ||
           weapon.mechanism == WeaponMechanism::deployed_machine_gun ||
           weapon.mechanism == WeaponMechanism::paintbrush ||
           // SnowBlowerWeapon inherits the launcher's wire shape but not its
           // one-shot trigger policy. Retail calls use_primary continuously,
           // bounded by SNOWBLOWER_SHOOT_INTERVAL, while LMB remains held.
           ((weapon.tool_id == 29U || weapon.tool_id == 48U) &&
            weapon.mechanism == WeaponMechanism::oriented_launcher);
}

[[nodiscard]] bool automatically_reloads(WeaponMechanism mechanism) noexcept {
    switch (mechanism) {
    case WeaponMechanism::firearm_semi:
    case WeaponMechanism::firearm_automatic:
    case WeaponMechanism::firearm_burst:
    case WeaponMechanism::firearm_spinup:
    case WeaponMechanism::shotgun_semi:
    case WeaponMechanism::shotgun_automatic:
    case WeaponMechanism::deployed_machine_gun:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool consumes_weapon_ammo(WeaponMechanism mechanism) noexcept {
    switch (mechanism) {
    case WeaponMechanism::firearm_semi:
    case WeaponMechanism::firearm_automatic:
    case WeaponMechanism::firearm_burst:
    case WeaponMechanism::firearm_spinup:
    case WeaponMechanism::shotgun_semi:
    case WeaponMechanism::shotgun_automatic:
    case WeaponMechanism::deployed_machine_gun:
    case WeaponMechanism::cooked_throwable:
    case WeaponMechanism::charged_throwable:
    case WeaponMechanism::oriented_launcher:
    case WeaponMechanism::deployable:
    case WeaponMechanism::c4:
    case WeaponMechanism::disguise:
        return true;
    default:
        return false;
    }
}

} // namespace

WeaponRuntime::WeaponRuntime(std::uint32_t random_seed) noexcept
    : random_state_(random_seed == 0U ? 0xA05B168U : random_seed) {}

void WeaponRuntime::replace_loadout(std::span<const std::uint8_t> tool_ids,
                                    std::optional<std::uint8_t> selected) {
    replication_.replace_loadout(tool_ids, selected);
    reset_selected_runtime();
}

WeaponStateResult WeaponRuntime::select(std::uint8_t tool_id) noexcept {
    const auto result = replication_.select(tool_id);
    if (result == WeaponStateResult::accepted) {
        reset_selected_runtime();
    }
    return result;
}

void WeaponRuntime::set_primary(bool held) noexcept {
    if (held == primary_held_) {
        return;
    }
    primary_held_ = held;
    primary_pressed_ = held;
    primary_released_ = !held;
    if (!held) {
        // Re-arm the empty cue: the next press is a new trigger pull.
        dry_fire_latched_ = false;
    }
}

void WeaponRuntime::set_secondary(bool held) noexcept {
    if (held == secondary_held_) {
        return;
    }
    secondary_held_ = held;
    secondary_pressed_ = held;
    secondary_released_ = !held;
    if (!held) {
        secondary_dry_fire_latched_ = false;
    }
}

void WeaponRuntime::set_custom(bool held) noexcept {
    if (held == custom_held_) {
        return;
    }
    custom_held_ = held;
    custom_pressed_ = held;
    custom_released_ = !held;
}

void WeaponRuntime::set_context(WeaponRuntimeContext context) noexcept {
    context_ = context;
}

bool WeaponRuntime::can_use_while_sprinting(
    const WeaponDefinition& weapon, bool secondary) noexcept {
    // Character.can_shoot_primary/secondary (character.pyx:466/469) use the
    // same ALL_MELEE_WEAPONS membership escape before the per-tool flag. This
    // is why Zombie hands can claw while sprinting even though their Python
    // ZombieHandTool inherits Tool's default-false sprint attributes.
    if (weapon.mechanism == WeaponMechanism::melee) {
        return true;
    }
    return secondary
               ? weapon.retail.use.can_shoot_secondary_while_sprinting
               : weapon.retail.use.can_shoot_primary_while_sprinting;
}

WeaponStateResult WeaponRuntime::request_reload() noexcept {
    const auto selected = replication_.selected_tool();
    if (!selected.has_value()) {
        return WeaponStateResult::invalid_tool;
    }
    const auto& weapon = weapon_catalog()[*selected];
    if (reload_remaining_ > 0.0) {
        return WeaponStateResult::already_reloading;
    }
    const auto* ammo = replication_.ammo(*selected);
    const auto capacity = weapon.retail.ammo.magazine_capacity.value_or(
        weapon.clip_size);
    if (ammo == nullptr || capacity == 0U || ammo->magazine >= capacity ||
        ammo->reserve == 0U) {
        return WeaponStateResult::no_ammunition;
    }
    const auto result = replication_.begin_reload(*selected);
    if (result == WeaponStateResult::accepted) {
        reload_remaining_ = weapon.retail.use.reload_time.value_or(
            weapon.reload_time);
        emit(WeaponActionKind::reload_started, weapon);
    }
    return result;
}

void WeaponRuntime::restock_ammunition() noexcept {
    replication_.restock_ammunition();
    // A resupplied weapon is no longer empty, so a trigger still held from
    // before the restock should be able to click again if it runs dry a second
    // time without ever being released.
    dry_fire_latched_ = false;
    secondary_dry_fire_latched_ = false;
}

bool WeaponRuntime::restock_from_ammo_crate() noexcept {
    const bool changed = replication_.restock_from_ammo_crate();
    if (changed) {
        dry_fire_latched_ = false;
        secondary_dry_fire_latched_ = false;
    }
    // Weapon.restock(AMMO_CRATE): when the held weapon is reloadable and its
    // magazine is empty, Character.reload_next_update starts the reload on the
    // next update instead of leaving the gun dry until R is pressed.
    if (const auto selected = replication_.selected_tool(); selected.has_value()) {
        const auto& weapon = weapon_catalog()[*selected];
        const auto* ammo = replication_.ammo(*selected);
        const auto capacity = weapon.retail.ammo.magazine_capacity.value_or(
            weapon.clip_size);
        if (automatically_reloads(weapon.mechanism) && ammo != nullptr &&
            capacity != 0U && ammo->magazine == 0U && ammo->reserve > 0U) {
            reload_next_update_ = true;
        }
    }
    return changed;
}

void WeaponRuntime::on_unset() noexcept {
    // MinigunWeapon.on_unset explicitly restores shoot_interval_initial and
    // spin_speed=0 before chaining through Weapon/Tool. The shared runtime
    // reset also covers every other recovered on_unset invariant: an old
    // reload, burst, charge, or queued edge cannot cross into a new life/tool.
    reset_selected_runtime();
}

void WeaponRuntime::cancel_interaction() noexcept {
    if (block_sucker_state_ != 0U) {
        if (const auto selected = replication_.selected_tool(); selected.has_value()) {
            emit(WeaponActionKind::block_sucker_state,
                 weapon_catalog()[*selected], false, 0.0);
        }
        begin_block_sucker_settle();
    }
    primary_held_ = false;
    secondary_held_ = false;
    custom_held_ = false;
    primary_pressed_ = false;
    primary_released_ = false;
    secondary_pressed_ = false;
    secondary_released_ = false;
    custom_pressed_ = false;
    custom_released_ = false;
    interaction_active_ = false;
    interaction_elapsed_ = 0.0;
    interaction_value_ = 0.0;
    burst_remaining_ = 0U;
    block_sucker_state_ = 0U;
    // Releasing an interaction ends the trigger pull that latched the cue, so
    // the next press must click again. Hard life/tool boundaries additionally
    // call on_unset(), which clears timers and mechanism state below this layer.
    dry_fire_latched_ = false;
    secondary_dry_fire_latched_ = false;
}

void WeaponRuntime::tick(double dt) noexcept {
    if (!std::isfinite(dt) || dt <= 0.0) {
        return;
    }
    dt = std::min(dt, 0.25);
    cooldown_ = std::max(0.0, cooldown_ - dt);
    secondary_cooldown_ = std::max(0.0, secondary_cooldown_ - dt);
    block_sucker_reactivate_ = std::max(0.0, block_sucker_reactivate_ - dt);
    // Releasing RMB runs on_stop_secondary, clearing active_secondary. The
    // release is honoured here, after this frame's tool-switch input, so a
    // switch key handled while RMB was still down is refused first.
    swap_lock_remaining_ =
        secondary_held_ ? std::max(0.0, swap_lock_remaining_ - dt) : 0.0;

    const auto selected = replication_.selected_tool();
    if (!selected.has_value()) {
        cancel_interaction();
        return;
    }
    const auto& weapon = weapon_catalog()[*selected];

    if (reload_next_update_) {
        reload_next_update_ = false;
        static_cast<void>(request_reload());
    }

    if (reload_remaining_ > 0.0) {
        // Weapon.use_primary still refuses while reloading, but a press during
        // a clip_reload cycle makes Character.end_reload stop the chain after
        // the shell in progress (BS/server/player.py _reload_blocks_shot). The
        // press is latched here and replayed once that shell is loaded.
        if (primary_pressed_ && weapon.retail.ammo.clip_reload) {
            reload_interrupt_latched_ = true;
        }
        reload_remaining_ = std::max(0.0, reload_remaining_ - dt);
        if (reload_remaining_ == 0.0) {
            const bool interrupted = std::exchange(reload_interrupt_latched_, false);
            if (replication_.finish_reload(*selected) == WeaponStateResult::accepted) {
                // Rounds are back, so the weapon can click again the next time
                // it runs dry -- even if the trigger was never released.
                dry_fire_latched_ = false;
                secondary_dry_fire_latched_ = false;
                const auto* ammo = replication_.ammo(*selected);
                const auto capacity = weapon.retail.ammo.magazine_capacity.value_or(
                    weapon.clip_size);
                // Character.end_reload (character.pyd 0x1001FC00) transfers
                // exactly one round for clip_reload weapons, sends the finished
                // edge, and invokes Character.reload again while another shell
                // will fit -- unless `reloaded_cancel or shoot_primary_held or
                // shoot_primary`: a new press, the empty-while-held latch, or
                // a trigger that is simply HELD at the shell boundary all stop
                // the chain after the shell in progress.
                const bool fire_resume = std::exchange(shoot_primary_held_, false);
                const bool stop_chain = interrupted || fire_resume || primary_held_;
                const bool continue_shell_reload =
                    !stop_chain && weapon.retail.ammo.clip_reload && ammo != nullptr &&
                    ammo->magazine < capacity && ammo->reserve > 0U;
                // The wire completion edge occurs after every shell. Value is
                // local presentation metadata: 1 only on the final cycle, so
                // the retail reload_done_sound is not played per shell.
                emit(WeaponActionKind::reload_completed, weapon, false,
                     continue_shell_reload ? 0.0 : 1.0);
                if (continue_shell_reload &&
                    replication_.begin_reload(*selected) ==
                        WeaponStateResult::accepted) {
                    reload_remaining_ = weapon.retail.use.reload_time.value_or(
                        weapon.reload_time);
                    emit(WeaponActionKind::reload_started, weapon);
                } else if (interrupted && ammo != nullptr && ammo->magazine > 0U) {
                    // Let the latched trigger pull through with the shells
                    // loaded so far, on this same update.
                    primary_pressed_ = true;
                } else if (fire_resume && ammo != nullptr && ammo->magazine > 0U) {
                    // end_reload's set_primary_shoot(True), for magazine
                    // weapons as well: shoot_primary stays set, so the gun
                    // fires as soon as its shoot interval allows.
                    resume_fire_pending_ = true;
                }
            }
        }
    }
    if (resume_fire_pending_ && reload_remaining_ == 0.0 && cooldown_ == 0.0) {
        resume_fire_pending_ = false;
        primary_pressed_ = true;
    }

    const auto& aim = weapon.retail.aim;
    if (aim.variable_accuracy) {
        const double minimum = aim.spread_min.value_or(1.0);
        const double recovery = aim.spread_reduction_per_second.value_or(0.0);
        spread_ = std::max(minimum, spread_ - recovery * dt);
    }

    // MinigunWeapon.update keeps running during Character.reload. Reloading
    // forces the inactive interval alteration even when LMB/RMB remains held,
    // so the authored barrel and loop pitch wind down rather than freezing.
    const bool weapon_actions_enabled = reload_remaining_ == 0.0;
    if (weapon.mechanism == WeaponMechanism::firearm_spinup) {
        update_minigun_motor(weapon, dt, weapon_actions_enabled);
    }

    // Weapon.use_primary refuses every action while Character.reloading.
    // Keep input edges consumed so pressing during a magazine reload cannot
    // fire unexpectedly on the first post-reload frame.
    if (weapon_actions_enabled) {
        process_edges(weapon);
        process_held(weapon, dt);
    }
    update_block_sucker_animation(dt, weapon);
    primary_pressed_ = false;
    primary_released_ = false;
    secondary_pressed_ = false;
    secondary_released_ = false;
    custom_pressed_ = false;
    custom_released_ = false;
}

void WeaponRuntime::process_edges(const WeaponDefinition& weapon) noexcept {
    if (primary_pressed_ &&
        (!context_.sprinting || can_use_while_sprinting(weapon, false))) {
        switch (weapon.mechanism) {
        case WeaponMechanism::cooked_throwable:
            if (consume(weapon)) {
                // Consumption is deferred until the projectile is released.
                interaction_active_ = true;
                interaction_elapsed_ = 0.0;
                interaction_value_ = weapon.retail.use.fuse.value_or(weapon.fuse_time);
                // GrenadeTool plays GRENADE_PULL_PIN_SOUND here. Molotov and
                // the Specialist charged throws deliberately do not.
                emit(WeaponActionKind::throwable_primed, weapon);
            } else {
                // GrenadeTool.on_start_primary with no count left.
                auto_switch_requested_ = true;
            }
            break;
        case WeaponMechanism::charged_throwable:
            if (consume(weapon)) {
                interaction_active_ = true;
                interaction_elapsed_ = 0.0;
                interaction_value_ = 0.0;
            } else {
                // MolotovWeapon / StickyGrenadeWeapon on_start_primary.
                auto_switch_requested_ = true;
            }
            break;
        case WeaponMechanism::block_builder:
            interaction_active_ = true;
            emit(WeaponActionKind::block_line_begin, weapon);
            break;
        case WeaponMechanism::block_sucker:
            if (block_sucker_reactivate_ == 0.0) {
                interaction_active_ = true;
                interaction_elapsed_ = 0.0;
                block_sucker_state_ = 1U;
                block_sucker_settle_remaining_ = 0.0;
                block_sucker_horizontal_phase_ = 0.0;
                block_sucker_vertical_phase_ = 0.0;
                emit(WeaponActionKind::block_sucker_state, weapon, false, 1.0);
            }
            break;
        case WeaponMechanism::firearm_spinup:
            // Holding the trigger spins the barrels; process_held emits only
            // after the recovered minimum spin-speed threshold is crossed.
            break;
        default:
            activate(weapon, false);
            break;
        }
    }

    if (primary_released_) {
        switch (weapon.mechanism) {
        case WeaponMechanism::cooked_throwable:
            if (interaction_active_) {
                emit(WeaponActionKind::oriented_item, weapon, false,
                     interaction_value_);
                static_cast<void>(replication_.observe_shot(weapon.tool_id));
                cooldown_ = weapon.fire_interval;
            }
            interaction_active_ = false;
            break;
        case WeaponMechanism::charged_throwable:
            if (interaction_active_ && !context_.sprinting) {
                emit(WeaponActionKind::oriented_item, weapon, false,
                     interaction_value_);
                static_cast<void>(replication_.observe_shot(weapon.tool_id));
                cooldown_ = weapon.fire_interval;
            }
            interaction_active_ = false;
            break;
        case WeaponMechanism::block_builder:
            if (interaction_active_) {
                emit(WeaponActionKind::block_line_commit, weapon);
            }
            interaction_active_ = false;
            break;
        case WeaponMechanism::block_sucker:
            if (interaction_active_ || block_sucker_state_ != 0U) {
                emit(WeaponActionKind::block_sucker_state, weapon, false, 0.0);
                begin_block_sucker_settle();
                block_sucker_state_ = 0U;
                block_sucker_reactivate_ = named_value(
                    weapon, "BLOCK_SUCKER_REACTIVATE_DELAY", 0.5);
            }
            interaction_active_ = false;
            break;
        default:
            break;
        }
    }

    if (secondary_released_ && weapon.tool_id == 4U && interaction_active_) {
        // ClassicSpade delay_secondary cancels when RMB is released before
        // the recovered 0.8 second wind-up completes.
        interaction_active_ = false;
        interaction_elapsed_ = 0.0;
    }

    if (secondary_pressed_ &&
        (!context_.sprinting || can_use_while_sprinting(weapon, true))) {
        if (weapon.tool_id == 4U) {
            interaction_active_ = true;
            interaction_elapsed_ = 0.0;
        } else if (weapon.mechanism == WeaponMechanism::c4) {
            emit(WeaponActionKind::c4_detonate, weapon, true);
            secondary_cooldown_ = weapon.retail.use.secondary_shoot_interval
                                      .value_or(weapon.fire_interval);
        } else if (weapon.mechanism == WeaponMechanism::deployed_machine_gun) {
            // Retail secondary toggles MG deployment while primary remains a
            // normal hitscan in either the carried or deployed profile.
            if (!context_.deployed && !context_.deployable_target_valid) {
                return;
            }
            emit(context_.deployed ? WeaponActionKind::objective_use
                                   : WeaponActionKind::deployable_place,
                 weapon, true);
        } else if (weapon.mechanism == WeaponMechanism::paintbrush) {
            activate(weapon, true);
        } else if (weapon.mechanism == WeaponMechanism::prefab_builder ||
                   weapon.mechanism == WeaponMechanism::ugc_prefab_editor) {
            emit(WeaponActionKind::prefab_rotate, weapon, true);
        } else if (weapon.mechanism == WeaponMechanism::block_builder) {
            // Retail BlockTool RMB is a local cancel edge. It must not emit
            // SetColor: the eyedropper is the distinct weapon-custom binding.
            interaction_active_ = false;
            primary_held_ = false;
            primary_pressed_ = false;
            primary_released_ = false;
            emit(WeaponActionKind::block_line_cancel, weapon, true);
        } else if (weapon.tool_id == 45U ||
                   weapon.mechanism == WeaponMechanism::ugc_entity) {
            // The only remaining real use_secondary overrides: the UGC super
            // spade's immediate 3x3x3 alternate dig and the UGC tool's
            // item-variant cycle (docs/WEAPON_SECONDARY_RECOVERY.md).
            activate(weapon, true);
        } else if (weapon.tool_id == 2U) {
            // SpadeTool RMB is an INERT lock: delay_secondary=True, and
            // on_start_secondary only sets active_secondary, so Tool.can_swap
            // is false until the 1.0 s charge ends or RMB is released. It
            // sends nothing and digs nothing.
            swap_lock_remaining_ =
                weapon.retail.use.secondary_shoot_interval.value_or(1.0);
        }
        // Every other has_secondary tool inherits Tool.use_secondary, which
        // returns None: retail never sends a secondary ShootPacket for them.
    }

    if (custom_pressed_ &&
        (weapon.mechanism == WeaponMechanism::block_builder ||
         weapon.mechanism == WeaponMechanism::prefab_builder ||
         weapon.mechanism == WeaponMechanism::ugc_prefab_editor ||
         weapon.mechanism == WeaponMechanism::flare_builder ||
         weapon.mechanism == WeaponMechanism::paintbrush ||
         weapon.mechanism == WeaponMechanism::disguise ||
         // Retail SnowBlowerWeapon is the Block Cannon. Both ordinary and
         // UGC variants pick their projectile colour through weapon-custom.
         weapon.tool_id == 29U || weapon.tool_id == 48U)) {
        emit(WeaponActionKind::color_pick, weapon);
    }
}

void WeaponRuntime::process_held(const WeaponDefinition& weapon,
                                 double dt) noexcept {
    if (weapon.tool_id == 4U && interaction_active_) {
        interaction_elapsed_ += dt;
        const double delay = weapon.retail.use.secondary_shoot_interval
                                 .value_or(0.8);
        if (secondary_held_ && interaction_elapsed_ >= delay) {
            interaction_active_ = false;
            activate(weapon, true);
        }
        return;
    }
    if (weapon.mechanism == WeaponMechanism::cooked_throwable &&
        interaction_active_) {
        interaction_elapsed_ += dt;
        interaction_value_ = std::max(0.0, interaction_value_ - dt);
        if (interaction_value_ == 0.0) {
            emit(WeaponActionKind::oriented_item, weapon, false, 0.0);
            static_cast<void>(replication_.observe_shot(weapon.tool_id));
            cooldown_ = weapon.fire_interval;
            interaction_active_ = false;
            primary_held_ = false;
        }
        return;
    }
    if (weapon.mechanism == WeaponMechanism::charged_throwable &&
        interaction_active_) {
        if (!context_.sprinting) {
            interaction_elapsed_ += dt;
        }
        interaction_value_ = std::clamp(
            interaction_elapsed_ / charged_duration(weapon), 0.0, 1.0);
        return;
    }
    if (weapon.mechanism == WeaponMechanism::block_sucker &&
        interaction_active_) {
        interaction_elapsed_ += dt;
        const double warmup = named_value(
            weapon, "BLOCK_SUCKER_WARM_UP_DELAY", 1.0);
        if (block_sucker_state_ == 1U && interaction_elapsed_ >= warmup) {
            block_sucker_state_ = 2U;
            emit(WeaponActionKind::block_sucker_state, weapon, false, 2.0);
        }
        if (block_sucker_state_ == 2U && cooldown_ == 0.0) {
            emit(WeaponActionKind::block_suck, weapon);
            cooldown_ = weapon.fire_interval;
        }
        return;
    }
    if (weapon.mechanism == WeaponMechanism::firearm_burst &&
        burst_remaining_ > 0U) {
        burst_cooldown_ -= dt;
        if (burst_cooldown_ <= 0.0) {
            // Internal burst rounds use A1935 (0.1 s), not the trigger-level
            // 0.5 s cadence written by the preceding round.
            cooldown_ = 0.0;
            activate(weapon, false);
            if (burst_remaining_ > 0U) {
                --burst_remaining_;
            }
            burst_cooldown_ = named_value(weapon, "A1935", 0.1);
        }
        return;
    }
    if (primary_held_ && !primary_pressed_ && cooldown_ == 0.0 &&
        repeats_while_held(weapon)) {
        activate(weapon, false);
    }
    if (secondary_held_ && !secondary_pressed_ && secondary_cooldown_ == 0.0 &&
        weapon.mechanism == WeaponMechanism::paintbrush) {
        activate(weapon, true);
    }
}

void WeaponRuntime::update_minigun_motor(const WeaponDefinition& weapon,
                                         double dt,
                                         bool input_enabled) noexcept {
    const double initial = named_value(
        weapon, "MINIGUN_SHOOT_INTERVAL", weapon.fire_interval);
    const double range = named_value(
        weapon, "MINIGUN_SHOOT_INTERVAL_RANGE", -0.2);
    const double target = initial + range;
    // MinigunWeapon.update drives the motor while either shoot input is held.
    // LMB spins then fires; RMB only prepares the barrels. Character.reloading
    // makes both inputs inactive for this motor even when their physical
    // buttons remain down.
    const bool spinning =
        input_enabled &&
        ((primary_held_ &&
          (!context_.sprinting || can_use_while_sprinting(weapon, false))) ||
         (secondary_held_ &&
          (!context_.sprinting || can_use_while_sprinting(weapon, true))));
    const double rate =
        spinning
            ? named_value(
                  weapon,
                  "MINIGUN_SHOOT_INTERVAL_ACTIVE_ALTERATION_PER_SECOND", -0.15)
            : named_value(
                  weapon,
                  "MINIGUN_SHOOT_INTERVAL_INACTIVE_ALTERATION_PER_SECOND", 0.075);
    minigun_interval_ = std::clamp(minigun_interval_ + rate * dt,
                                   std::min(initial, target),
                                   std::max(initial, target));
    const double ratio = std::abs((minigun_interval_ - initial) / range);
    const double spin = ratio * named_value(
        weapon, "MINIGUN_BARREL_SPIN_SPEED_MAX", 5.0);
    // AnimRoll advances orientation by dt * spin_speed * 360 degrees.
    minigun_rotation_ = std::fmod(minigun_rotation_ + spin * dt, 1.0);
    if (minigun_rotation_ < 0.0) {
        minigun_rotation_ += 1.0;
    }
    const double threshold = named_value(
        weapon, "MINIGUN_BARREL_SPIN_SPEED_MIN_TO_ALLOW_SHOOTING", 0.5);
    if (input_enabled && primary_held_ && spin > threshold && cooldown_ == 0.0) {
        activate(weapon, false);
        cooldown_ = minigun_interval_;
    }
}

void WeaponRuntime::activate(const WeaponDefinition& weapon,
                             bool secondary) noexcept {
    if (reload_remaining_ > 0.0 || (secondary ? secondary_cooldown_ : cooldown_) > 0.0) {
        return;
    }
    if (context_.sprinting && !can_use_while_sprinting(weapon, secondary)) {
        return;
    }
    if ((weapon.mechanism == WeaponMechanism::deployable ||
         weapon.mechanism == WeaponMechanism::c4) &&
        !context_.deployable_target_valid) {
        // Retail Weapon.shoot returns False when its ghost target is absent;
        // base weapon logic therefore leaves both stock and cooldown intact.
        // c4Weapon.py / dynamiteWeapon.py / landmineWeapon.py /
        // medPackWeapon.py play BUILD_ERROR_SOUND on that refusal. Only a
        // trigger press reports it, and it draws no seed.
        if (secondary ? secondary_pressed_ : primary_pressed_) {
            report_placement_rejected(weapon, secondary);
        }
        return;
    }
    const bool consumes_this_action = consumes_weapon_ammo(weapon.mechanism) &&
                                      !(weapon.mechanism == WeaponMechanism::c4 &&
                                        secondary);
    if (consumes_this_action && !consume(weapon)) {
        // Character.shoot immediately enters reload when a firearm has a
        // reserve but no round left in the magazine. This edge must precede
        // the dry-fire latch: otherwise holding an automatic after its last
        // round leaves it permanently empty until the player presses R.
        if (automatically_reloads(weapon.mechanism) &&
            request_reload() == WeaponStateResult::accepted) {
            burst_remaining_ = 0U;
            return;
        }
        // Retail plays the empty cue once per trigger PRESS and is silent while
        // the trigger stays held. Held on an empty magazine, this path used to
        // re-enter every tick and emit sixty cues a second on an automatic --
        // measured, not estimated. Latch the cue and leave every gameplay field
        // on this path exactly as it was: consume() has already returned false,
        // so no ammunition moved, and no timing field is read or written here.
        bool& latch = secondary ? secondary_dry_fire_latched_ : dry_fire_latched_;
        if (!latch) {
            latch = true;
            emit(WeaponActionKind::dry_fire, weapon, secondary);
            // Weapon.use_primary calls Character.auto_switch_tool on the dry
            // pull. Nothing is left to reload here, so move off the tool.
            if (!secondary) {
                auto_switch_requested_ = true;
            }
        }
        return;
    }

    WeaponActionKind kind{WeaponActionKind::dry_fire};
    switch (weapon.mechanism) {
    case WeaponMechanism::melee:
        kind = WeaponActionKind::melee;
        break;
    case WeaponMechanism::firearm_semi:
    case WeaponMechanism::firearm_automatic:
    case WeaponMechanism::firearm_burst:
    case WeaponMechanism::firearm_spinup:
    case WeaponMechanism::shotgun_semi:
    case WeaponMechanism::shotgun_automatic:
    case WeaponMechanism::deployed_machine_gun:
        kind = WeaponActionKind::hitscan;
        break;
    case WeaponMechanism::flare_builder:
        kind = WeaponActionKind::flare_place;
        break;
    case WeaponMechanism::prefab_builder:
    case WeaponMechanism::ugc_prefab_editor:
        kind = WeaponActionKind::prefab_place;
        break;
    case WeaponMechanism::oriented_launcher:
        kind = WeaponActionKind::oriented_item;
        break;
    case WeaponMechanism::deployable:
    case WeaponMechanism::c4:
        kind = WeaponActionKind::deployable_place;
        break;
    case WeaponMechanism::objective:
        kind = WeaponActionKind::objective_use;
        break;
    case WeaponMechanism::ugc_entity:
        kind = WeaponActionKind::ugc_entity_use;
        break;
    case WeaponMechanism::paintbrush:
        kind = secondary ? WeaponActionKind::paint_area
                         : WeaponActionKind::paint_single;
        break;
    case WeaponMechanism::disguise:
        if (context_.disguise_active) {
            return;
        }
        kind = WeaponActionKind::disguise_activate;
        break;
    case WeaponMechanism::inert:
    case WeaponMechanism::block_builder:
    case WeaponMechanism::cooked_throwable:
    case WeaponMechanism::charged_throwable:
    case WeaponMechanism::block_sucker:
        return;
    }
    const double value = kind == WeaponActionKind::oriented_item
                             ? launcher_speed(weapon)
                             : 0.0;
    emit(kind, weapon, secondary, value);
    bool emptied_magazine{};
    if (consumes_this_action) {
        static_cast<void>(replication_.observe_shot(weapon.tool_id));
        const auto* ammo = replication_.ammo(weapon.tool_id);
        emptied_magazine = ammo != nullptr && ammo->magazine == 0U &&
                           ammo->reserve > 0U;
        // Weapon.use_primary: the shot that empties the magazine while the
        // trigger is down sets shoot_primary_held.
        if (emptied_magazine && !secondary && primary_held_) shoot_primary_held_ = true;
    }
    if (weapon.retail.aim.variable_accuracy) {
        spread_ = std::min(
            weapon.retail.aim.spread_max.value_or(spread_),
            spread_ + weapon.retail.aim.spread_increase_per_shot.value_or(0.0));
    }
    if (secondary) {
        secondary_cooldown_ = weapon.retail.use.secondary_shoot_interval
                                  .value_or(weapon.fire_interval);
    } else {
        cooldown_ = weapon.mechanism == WeaponMechanism::deployed_machine_gun &&
                            context_.deployed
                        ? named_value(weapon, "MG_DEPLOYED_SHOOT_INTERVAL", 0.1)
                        : weapon.fire_interval;
    }
    if (weapon.mechanism == WeaponMechanism::firearm_burst &&
        burst_remaining_ == 0U) {
        const auto burst_size = static_cast<std::uint8_t>(std::clamp(
            named_value(weapon, "A1934", 3.0), 1.0, 255.0));
        burst_remaining_ = static_cast<std::uint8_t>(burst_size - 1U);
        burst_cooldown_ = named_value(weapon, "A1935", 0.1);
    }
    if (emptied_magazine && automatically_reloads(weapon.mechanism) &&
        request_reload() == WeaponStateResult::accepted) {
        // An empty magazine terminates an incomplete burst. The new rounds
        // belong to the next trigger pull, never to a stale scheduled round.
        burst_remaining_ = 0U;
    }
}

void WeaponRuntime::report_placement_rejected(const WeaponDefinition& weapon,
                                              bool secondary) noexcept {
    // Presentation-only: no seed is drawn, so the shot RNG stream the server
    // replays stays identical to a client that never pressed.
    actions_.push_back(WeaponAction{WeaponActionKind::placement_rejected, weapon.tool_id, 0U,
                                    1U, secondary, 0.0, false, 0.0});
}

void WeaponRuntime::emit(WeaponActionKind kind,
                         const WeaponDefinition& weapon,
                         bool secondary, double value) noexcept {
    // burst_remaining_ is armed when the OPENING round is emitted and decremented
    // after each follow-up, so a non-zero value here identifies rounds 2..N
    // without any new state. Reads only; the seed stream is unchanged.
    const bool burst_follow_up =
        weapon.mechanism == WeaponMechanism::firearm_burst && burst_remaining_ > 0U;
    // Weapon.prep_shoot: the shot uses the bloom reached BEFORE this round's
    // shot_weapon() increase (activate() grows spread_ after emitting).
    double accuracy{};
    if (kind == WeaponActionKind::hitscan) {
        accuracy = weapon.retail.aim.variable_accuracy
                       ? current_accuracy()
                       : weapon.retail.aim.accuracy.value_or(weapon.spread);
    }
    actions_.push_back(WeaponAction{kind, weapon.tool_id, next_seed(),
                                    weapon.pellet_count, secondary, value,
                                    burst_follow_up, accuracy});
}

bool WeaponRuntime::consume(const WeaponDefinition& weapon) noexcept {
    const auto* ammo = replication_.ammo(weapon.tool_id);
    if (ammo == nullptr) {
        return false;
    }
    const bool finite = weapon.clip_size != 0U ||
                        weapon.retail.ammo.magazine_capacity.has_value() ||
                        weapon.retail.ammo.maximum_count.value_or(0U) != 0U;
    return !finite || ammo->magazine > 0U;
}

std::uint8_t WeaponRuntime::next_seed() noexcept {
    random_state_ ^= random_state_ << 13U;
    random_state_ ^= random_state_ >> 17U;
    random_state_ ^= random_state_ << 5U;
    return static_cast<std::uint8_t>(1U + random_state_ % 255U);
}

double WeaponRuntime::named_value(const WeaponDefinition& weapon,
                                  std::string_view name,
                                  double fallback) const noexcept {
    const auto* constant = find_retail_weapon_constant(weapon, name);
    return constant != nullptr && constant->value_count > 0U
               ? constant->values.front()
               : fallback;
}

double WeaponRuntime::charged_duration(const WeaponDefinition& weapon) const noexcept {
    if (weapon.tool_id == 33U) {
        return named_value(weapon, "MOLOTOV_THROW_MAX_CHARGE", 3.0);
    }
    if (weapon.tool_id == 54U) {
        return named_value(weapon, "A1662", 1.0);
    }
    if (weapon.tool_id == 57U) {
        return named_value(weapon, "A1681", 1.0);
    }
    for (const auto& constant : weapon.constants) {
        if (constant.name.ends_with("THROW_MAX_CHARGE") &&
            constant.value_count > 0U) {
            return std::max(0.001, constant.values.front());
        }
    }
    return 3.0;
}

double WeaponRuntime::launcher_speed(const WeaponDefinition& weapon) const noexcept {
    struct SpeedName final {
        std::uint8_t tool_id;
        std::string_view name;
    };
    static constexpr SpeedName names[]{{12U, "ROCKET_SPEED"},
                                       {13U, "ROCKET2_SPEED"},
                                       {14U, "DRILL_FLYING_SPEED"},
                                       {29U, "SNOWBALL_SPEED"},
                                       {46U, "UGC_ROCKET2_SPEED"},
                                       {47U, "UGC_DRILL_FLYING_SPEED"},
                                       {48U, "UGC_SNOWBALL_SPEED"},
                                       {55U, "GRENADE_LAUNCHER_PROJECTILE_SPEED"},
                                       {58U, "MINE_LAUNCHER_PROJECTILE_SPEED"}};
    for (const auto& entry : names) {
        if (entry.tool_id == weapon.tool_id) {
            return named_value(weapon, entry.name, 0.0);
        }
    }
    for (const auto& constant : weapon.constants) {
        if ((constant.name.ends_with("_SPEED") ||
             constant.name.ends_with("FLYING_SPEED")) &&
            constant.name.find("KNOCKBACK") == std::string_view::npos &&
            constant.value_count > 0U) {
            return constant.values.front();
        }
    }
    return 0.0;
}

void WeaponRuntime::reset_selected_runtime() noexcept {
    cancel_interaction();
    // Character owns one global reload flag. Leaving a tool or life cancels
    // it; otherwise returning to a weapon switched away from mid-reload would
    // make begin_reload report already_reloading forever.
    replication_.cancel_reload();
    // An edge queued for the outgoing tool must never act through the newly
    // selected model on the following simulation tick.
    actions_.clear();
    cooldown_ = 0.0;
    secondary_cooldown_ = 0.0;
    reload_remaining_ = 0.0;
    burst_cooldown_ = 0.0;
    block_sucker_reactivate_ = 0.0;
    reload_interrupt_latched_ = false;
    shoot_primary_held_ = false;
    resume_fire_pending_ = false;
    reload_next_update_ = false;
    auto_switch_requested_ = false;
    swap_lock_remaining_ = 0.0;
    const auto selected = replication_.selected_tool();
    if (!selected.has_value()) {
        spread_ = 0.0;
        minigun_interval_ = 0.0;
        minigun_rotation_ = 0.0;
        return;
    }
    const auto& weapon = weapon_catalog()[*selected];
    spread_ = weapon.retail.aim.spread_min.value_or(1.0);
    minigun_interval_ = named_value(
        weapon, "MINIGUN_SHOOT_INTERVAL", weapon.fire_interval);
    minigun_rotation_ = 0.0;
}

std::vector<WeaponAction> WeaponRuntime::take_actions() {
    return std::exchange(actions_, {});
}

const WeaponReplicationState& WeaponRuntime::replication() const noexcept {
    return replication_;
}

bool WeaponRuntime::swap_locked() const noexcept { return swap_lock_remaining_ > 0.0; }

bool WeaponRuntime::take_auto_switch_request() noexcept {
    return std::exchange(auto_switch_requested_, false);
}

double WeaponRuntime::cooldown_remaining() const noexcept { return cooldown_; }
double WeaponRuntime::reload_remaining() const noexcept { return reload_remaining_; }

double WeaponRuntime::charge_fraction() const noexcept {
    return interaction_active_ ? interaction_value_ : 0.0;
}

double WeaponRuntime::interaction_elapsed() const noexcept {
    return interaction_active_ ? interaction_elapsed_ : -1.0;
}

double WeaponRuntime::current_accuracy() const noexcept {
    const auto selected = replication_.selected_tool();
    if (!selected.has_value()) {
        return 0.0;
    }
    const auto& aim = weapon_catalog()[*selected].retail.aim;
    if (!aim.variable_accuracy) {
        return aim.accuracy.value_or(0.0);
    }
    const double spread_min = aim.spread_min.value_or(1.0);
    const double spread_max = aim.spread_max.value_or(spread_min);
    const double accuracy_min = aim.accuracy_min.value_or(
        aim.accuracy.value_or(0.0));
    const double accuracy_max = aim.accuracy_max.value_or(accuracy_min);
    if (spread_max <= spread_min) {
        return accuracy_min;
    }
    const double ratio = std::clamp(
        (spread_ - spread_min) / (spread_max - spread_min), 0.0, 1.0);
    return accuracy_min + ratio * (accuracy_max - accuracy_min);
}

double WeaponRuntime::crosshair_radius_pixels(double viewport_height,
                                              double fov_y_degrees,
                                              bool zoomed) const noexcept {
    const auto selected = replication_.selected_tool();
    if (!selected.has_value()) {
        return 0.0;
    }

    // get_accuracy/get_static_accuracy live on Weapon only. Plain Tool
    // subclasses (spade and other DiggingTools, BlockTool, GrenadeTool,
    // PrefabTool, ...) never reach them and keep the 1 px minimum, the small
    // square retail draws for blocks, melee, grenades and prefabs (live A/B
    // 2026-09-29: 18 px extent for keys 1/2/5/6). Every retail Weapon
    // subclass name ends in "Weapon".
    if (!weapon_catalog()[*selected].retail.class_name.ends_with("Weapon")) {
        return 1.0;
    }
    const auto& aim = weapon_catalog()[*selected].retail.aim;
    // Python's `accuracy_zoom != accuracy` is significant here: a firearm
    // with numeric accuracy and None for accuracy_zoom still uses the
    // projection path, while melee/tools (None == None) use static spread.
    const bool projection_based =
        aim.variable_accuracy || aim.accuracy_zoom != aim.accuracy;
    if (!projection_based) {
        return std::max(0.0, spread_ * 6.0);
    }

    double accuracy = current_accuracy();
    if (zoomed && aim.accuracy_zoom.has_value()) {
        accuracy = *aim.accuracy_zoom;
    }
    const double zoom_modifier = zoomed ? 1.0 : 2.0;
    const double clamped_height = std::max(1.0, viewport_height);
    const double clamped_fov = std::clamp(fov_y_degrees, 1.0, 179.0);
    const double half_fov_radians =
        clamped_fov * 0.5 * std::numbers::pi / 180.0;
    const double focal_pixels = clamped_height * 0.5 / std::tan(half_fov_radians);

    // weapon.py:get_variable_accuracy projects an offset direction then
    // subtracts 3.5 pixels for the authored sprite inset, with a 1 px floor.
    return std::max(focal_pixels * std::max(0.0, accuracy) * zoom_modifier - 3.5,
                    1.0);
}

double WeaponRuntime::spin_fraction() const noexcept {
    const auto selected = replication_.selected_tool();
    if (!selected.has_value()) {
        return 0.0;
    }
    const auto& weapon = weapon_catalog()[*selected];
    if (weapon.mechanism != WeaponMechanism::firearm_spinup) {
        return 0.0;
    }
    const double initial = named_value(
        weapon, "MINIGUN_SHOOT_INTERVAL", weapon.fire_interval);
    const double range = named_value(
        weapon, "MINIGUN_SHOOT_INTERVAL_RANGE", -0.2);
    if (std::abs(range) <= 1e-9) {
        return 0.0;
    }
    return std::clamp(std::abs((minigun_interval_ - initial) / range), 0.0, 1.0);
}

double WeaponRuntime::spin_rotation_fraction() const noexcept {
    const auto selected = replication_.selected_tool();
    return selected.has_value() &&
                   weapon_catalog()[*selected].mechanism ==
                       WeaponMechanism::firearm_spinup
               ? minigun_rotation_
               : 0.0;
}

std::uint8_t WeaponRuntime::block_sucker_state() const noexcept {
    return block_sucker_state_;
}

std::array<double, 3U> WeaponRuntime::block_sucker_shake() const noexcept {
    return block_sucker_shake_;
}

void WeaponRuntime::begin_block_sucker_settle() noexcept {
    block_sucker_settle_start_amplitude_ = block_sucker_shake_amplitude_;
    block_sucker_settle_remaining_ = 1.0;
}

void WeaponRuntime::update_block_sucker_animation(
    double dt, const WeaponDefinition& weapon) noexcept {
    if (weapon.mechanism != WeaponMechanism::block_sucker &&
        block_sucker_settle_remaining_ <= 0.0) {
        block_sucker_shake_amplitude_ = 0.0;
        block_sucker_shake_ = {};
        return;
    }

    constexpr double warm_up_start_amplitude{0.06};
    constexpr double full_power_amplitude{0.02};
    if (block_sucker_state_ == 0U) {
        block_sucker_settle_remaining_ =
            std::max(0.0, block_sucker_settle_remaining_ - dt);
        block_sucker_shake_amplitude_ = block_sucker_settle_start_amplitude_ *
                                        block_sucker_settle_remaining_;
    } else if (block_sucker_state_ == 1U) {
        const double warmup = std::max(
            0.001, named_value(weapon, "BLOCK_SUCKER_WARM_UP_DELAY", 1.0));
        const double half = warmup * 0.5;
        if (interaction_elapsed_ < half) {
            block_sucker_shake_amplitude_ =
                warm_up_start_amplitude * (interaction_elapsed_ / half);
        } else {
            const double ratio = std::clamp(
                (interaction_elapsed_ - half) / half, 0.0, 1.0);
            block_sucker_shake_amplitude_ =
                warm_up_start_amplitude -
                (warm_up_start_amplitude - full_power_amplitude) * ratio;
        }
    } else {
        block_sucker_shake_amplitude_ = full_power_amplitude;
    }

    // AnimBlockSucker advances independent random frequencies each update.
    // Keep this stream separate from next_seed(): presentation must never
    // perturb packet-visible spread or shot seeds.
    const auto random_unit = [this]() noexcept {
        block_sucker_random_state_ ^= block_sucker_random_state_ << 13U;
        block_sucker_random_state_ ^= block_sucker_random_state_ >> 17U;
        block_sucker_random_state_ ^= block_sucker_random_state_ << 5U;
        return static_cast<double>(block_sucker_random_state_ & 0xFFFFU) /
               65535.0;
    };
    block_sucker_horizontal_phase_ += dt * (15.0 + random_unit() * 10.0);
    block_sucker_vertical_phase_ += dt * (20.0 + random_unit() * 15.0);
    block_sucker_shake_ = {
        std::sin(block_sucker_horizontal_phase_) * block_sucker_shake_amplitude_,
        std::sin(block_sucker_vertical_phase_) * block_sucker_shake_amplitude_,
        0.0};
    if (block_sucker_state_ == 0U && block_sucker_settle_remaining_ <= 0.0) {
        block_sucker_shake_ = {};
    }
}

} // namespace battlespades::world
