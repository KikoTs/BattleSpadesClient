# Documentation index

Use the maintained guides below for current source behavior and operating
commands. Code, configuration, manifests and regression tests define the
implementation. A historical pass count or deployment record does not certify
the current source, retained executable, package or live service.

## Maintained guides

| Topic | Reference |
| --- | --- |
| Overview and quick start | [Project README](../README.md) |
| Build, test, stage, package and local output layout | [Runbook](RUNBOOK.md) |
| Runtime ownership and subsystem boundaries | [Architecture](ARCHITECTURE.md) |
| Routes, service effects and hosting lifecycle | [Frontend](FRONTEND.md) |
| Every menu control, availability and expected result | [Menu actions](MENU_ACTIONS.md) |
| Options, rebinding, persistence and display rollback | [Settings](SETTINGS.md) |
| F11 layout editor and translation packs | [UI customization](UI_CUSTOMIZATION.md) |
| Editor authority, project files and publishing | [Map Creator](UGC_MAP_CREATOR.md) |
| Account collection, receipts and hosted result recovery | [Inventory](INVENTORY.md) |
| Catalogue, attribution, fallback and remote appearances | [Community cosmetics](COMMUNITY_COSMETICS.md) |
| Arms, variants, aiming, sound, flashes and rendering checks | [Weapon presentation](WEAPON_SIGHTS_AND_SOUND.md) |
| Retail invariants and evidence ordering | [Compatibility contract](COMPATIBILITY.md) |
| Acceptance and future work | [Roadmap](ROADMAP.md) |
| Content import and ownership | [Assets](../assets/README.md) |

[Account progression, inventory and crates](ACCOUNT_PROGRESSION_INVENTORY_CRATES.md)
is the shared product contract with the backend. It includes broader targets;
the Inventory guide describes the native integration. Keep intentional contract
changes synchronized with the backend copy. This documentation update does not
change account balances, catalogue digests or production feature flags.

## Retail recovery and specifications

These retain unique measurements, wire/asset evidence and design constraints.
Their old local captures, implementation updates and session-specific limits
are historical. Reproduce findings on the current code before treating an old
"not implemented" statement as an active task.

- Weapons: [ADS placement](ADS_SIGHT_PLACEMENT.md),
  [secondary-action recovery](WEAPON_SECONDARY_RECOVERY.md),
  [weapon audio](WEAPON_AUDIO.md), [class voice recovery](CLASS_VOICE_RECOVERY.md).
- Gameplay: [entity constants](ENTITY_CONSTANTS.md),
  [entity recovery](ENTITY_PORT_RECOVERY.md),
  [placement and jetpacks](PLACEMENT_AND_JETPACK_INTEGRATION.md),
  [VIP compatibility](VIP_CLIENT_COMPATIBILITY.md).
- Presentation: [HUD recovery](HUD_RECOVERY.md),
  [HUD authority matrix](HUD_AUTHORITY_MATRIX.md),
  [map atmosphere survey](MAP_ATMOSPHERE_SURVEY.md),
  [UI parity browser](UI_PARITY_BROWSER.md).
- Service/security evidence: [retail progression](PROGRESSION_RECOVERY.md),
  [server hardening specification](SERVER_UNTRUSTED_CLIENT_HARDENING.md),
  [historical playtester audit](PLAYTESTER_CHANNEL_AUDIT_2026-08-24.md).
- Frontend evidence: [retail UI map](research/RETAIL_UI_MAP.md),
  [asset audit](research/UI_ASSET_AUDIT.md),
  [scene catalogue](research/RETAIL_FRONTEND_CATALOG.md),
  [Select Menu baseline](research/SELECT_MENU_VISUAL_BASELINE.md),
  [Join Match](research/JOIN_MATCH_FRONTEND.md),
  [Quick Play](research/QUICK_PLAY_FRONTEND.md),
  [Custom Match](research/CUSTOM_MATCH_FRONTEND.md),
  [Create Match](research/CREATE_MATCH_FRONTEND.md),
  [profile](research/PLAYER_PROFILE_FRONTEND.md),
  [leaderboards/loading](research/LEADERBOARDS_LOADING.md),
  [UGC Select](research/UGC_SELECT_FRONTEND.md),
  [UGC lobby](research/UGC_EDITOR_FRONTEND_PARITY.md),
  [UGC publishing](research/UGC_PUBLISH_FRONTEND.md).
- Additional recovery: [gameplay audit](research/GAMEPLAY_PARITY_AUDIT.md),
  [VXL reference boundary](research/OPENSPADES_VXL_REFERENCE.md),
  [jetpack attachment](recovery/JETPACK_ATTACHMENT_2026-08-05.md),
  [UI/audio evidence](recovery/UI_AUDIO_PARITY_2026-08-05.md).

The authoritative server guides are indexed in its
[README](../../BattleSpades/README.md#documentation). Server implementation
claims in old client research must be checked against those guides and source.

## Keeping documentation useful

Update the appropriate guide with behavior, ownership, reproduction commands
and remaining limits. Use relative links to source/references. Do not create a
new dated handoff for every fix or duplicate the current backlog. Put new run
logs, screenshots and timing reports in disposable output. Preserve unique
retail evidence, authored fixtures and asset licence/credit files. No backup
archives or additional status reports are needed for documentation maintenance.
