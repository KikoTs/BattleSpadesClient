# UI and Audio Parity Evidence (2026-08-05)

## Server favourites

- Retail `aoslib/scenes/frontend/serverMenu.py` creates a `FAVORITE` button at
  `(417, 130)` and calls `SteamAddFavouriteServer` / `SteamRemoveFavouriteServer`.
- Retail stores identity as the address and game port; the native client uses
  the canonical equivalent `aos://host:port`.
- Steam persistence is not available in the standalone client, so
  `FavoriteServerStore` supplies a bounded text-file replacement. It accepts
  only canonical identifiers, deduplicates on load, and ignores malformed rows.
- The requested direct-IP `Add Favorite` control is a BattleSpades extension.
  It does not connect; the application adapter must validate the endpoint, add
  its canonical identifier, save the set, and refresh the Favourites source.

## Select Team / Select Class audio

- Retail `selectTeam.py:on_start` and `selectClass.py:on_start` stop the prior
  bed and play `SECONDARY_MENU_MUSIC1`.
- `shared/constants_audio.py` resolves that constant to
  `secondary_menu_bed_001`.
- `selectClass.py:on_class_selected`, loadout selection, page changes, and the
  class scrollbar play `menu_scrollA`; Select plays `menu_confirmA`; Back plays
  `menu_backA`.
- `ClassSelectionMenuModel::take_audio_cue` now exposes those exact edge events
  without putting an OpenAL dependency in the renderer-neutral model.

## Zoom, respawn, and Zombie countdown cues

- The shipped retail bank contains distinct `zoom_in.ogg` and `zoom_out.ogg`
  samples. They are preloaded and exposed through `play_zoom_toggle`; the caller
  must invoke it only when the ADS toggle actually changes.
- `zombie_timer_countdown.ogg` is the only authored spoken countdown in the
  recovered bank and `shared/constants_audio.py` identifies it as
  `ZOMBIE_TIMER_COUNTDOWN_SOUND`. It is not a generic respawn cue. The Zombie
  mode server emits it through authoritative `PlaySound(23)` when the outbreak
  countdown starts; ordinary TDM `KillAction(46)` must never infer or play it.
- Ordinary death has a separate client-side three-beat countdown recovered
  from `Character.update_respawn_time` (`character.pyx:1689-1694`). On each
  whole-second transition it plays `beep2` for displayed 3 and 2, followed by
  `beep1` for displayed 1. The timer is seeded by `KillAction(46).respawn_time`;
  it is not a `PlaySound(23)` event. Zero and `NEVER_RESPAWN_TIME` are silent.
- The current class's `SPAWN_VO` is a second, independent cue owned by the
  local `Character` created from `CreatePlayer(28)`: a uniform 1-2 second delay,
  forced after the first spawn or a class change, and the authored 25/101 chance
  after a same-class respawn. It must not be used as the death countdown.

## Required application wiring

The high-contention native application module was intentionally not edited in
this focused pass. It must:

1. load/save `FavoriteServerStore` beside `settings.toml`;
2. handle `DirectConnectActionKind::add_favourite` before the connect path;
3. consume `ClassSelectionMenuModel::take_audio_cue()` after class-menu input;
4. start `play_secondary_menu_music()` on the initial team/class selection flow;
5. call `play_zoom_toggle` on real ADS edges; and
6. drain local respawn beep edges from `GameHudModel` while keeping mode-owned
   countdown audio on the existing `PlaySound(23)` path.
