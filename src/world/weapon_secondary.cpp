#include "battlespades/world/weapon_catalog.hpp"

namespace battlespades::world {

WeaponSecondaryBehavior
weapon_secondary_behavior(const WeaponDefinition& weapon) noexcept {
    // Retail MinigunWeapon.can_zoom is false. RMB participates in its barrel
    // motor without invoking Tool.use_secondary or entering an ADS state.
    if (weapon.mechanism == WeaponMechanism::firearm_spinup) {
        return WeaponSecondaryBehavior::spin_up;
    }

    // MG deployment is driven by RMB (or weapon-custom) and only enters its
    // forced sight state after deployment completes in the retail client.
    if (weapon.mechanism == WeaponMechanism::deployed_machine_gun) {
        return WeaponSecondaryBehavior::deploy_machine_gun;
    }

    // Character.use_weapon_secondary opens with a four-term conjunction and
    // RETURNS from it unconditionally:
    //   (not has_secondary) and can_zoom and self.main and sight is not None
    // Every term is load-bearing and none of them is a weapon category. The
    // previous rule here demanded `category == sniper` and a magnification
    // above 1.0; retail asks for neither, and between them those two invented
    // terms denied a right click to all twenty iron-sight weapons -- the
    // rifle, both SMGs, every shotgun, every pistol and the bazookas.
    //
    // `self.main` is the local player's own character. This resolver only ever
    // describes the tool in the local player's hands, so that term is
    // satisfied by construction and has no catalog counterpart.
    //
    // `can_zoom` looks redundant next to `sight`, and is not: the minigun
    // carries a real MINIGUN_SIGHT and ships minigun_sight.kv6, and only
    // can_zoom = False keeps it from being a scoped weapon. Dropping the term
    // and gating on the sight alone ships a minigun with a scope. (It is
    // already handled above, but the term stays because the data says so.)
    if (!weapon.retail.use.has_secondary && weapon.retail.use.can_zoom &&
        !weapon.sight_model_asset.empty()) {
        // The two aiming values differ only in magnification, and retail
        // gives a magnification above 1.0 to the two snipers alone. The rifle
        // aims at exactly 1.0 and still owns a dedicated transition rate,
        // which is what proves iron sights are a first-class aim state rather
        // than a weaker scope.
        return weapon.retail.use.zoom_factor.value_or(1.0) > 1.0
                   ? WeaponSecondaryBehavior::magnified_scope
                   : WeaponSecondaryBehavior::iron_sights;
    }

    // Everything below is the alternate-action branch, which retail reaches
    // only when the aiming conjunction above fails.
    //
    // These mechanisms implement a real Tool.use_secondary override or a
    // recovered secondary path.
    switch (weapon.mechanism) {
    case WeaponMechanism::block_builder:
    case WeaponMechanism::flare_builder:
    case WeaponMechanism::prefab_builder:
    case WeaponMechanism::ugc_prefab_editor:
    case WeaponMechanism::c4:
    case WeaponMechanism::ugc_entity:
    case WeaponMechanism::paintbrush:
        return WeaponSecondaryBehavior::tool_action;
    default:
        break;
    }
    if (weapon.retail.use.has_secondary) {
        return WeaponSecondaryBehavior::tool_action;
    }

    return WeaponSecondaryBehavior::none;
}

} // namespace battlespades::world
