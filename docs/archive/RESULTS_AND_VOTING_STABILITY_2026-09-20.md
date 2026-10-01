# Results, scoreboard and map voting stabilization

The existing native ViewScores, ViewGameStats and GenericVotingHUD paths are
retained. The supplied retail screenshots use the same authored score and
statistics frames, class/death icons, two team columns and bottom statistics
panel already installed with the client. This pass fixes missing roster behavior
and state errors instead of introducing a second set of screens.

## Source evidence

- `aceofspades_source/aoslib/scenes/ingame_menus/__init__.py`,
  `draw_player_list` and `draw_game_stats`: sixteen player rows per column,
  optional extra players at the bottom, actual team/spectator colors, three
  awards per team, and a retained `game_stat.player` object.
- `hud/hud.pyd.i64`, `ViewScores.update` at `0x1005EBE0`: sort each team and
  spectators; if one team has at least sixteen players, put spectators followed
  by that team's overflow in the other column. Otherwise split spectators
  between available bottom rows. This is the original 32-row screen capacity;
  it does not introduce a page or scrolling behavior absent from the source.
- `ViewScores.draw` at `0x1005FBD0`: the score frame uses RGBA `(1,1,1,0.8)`;
  the player-list helper then restores the separate head/row colors.
- `ViewGameStats.draw` at `0x10067A90`: its frame uses full modulation opacity,
  unlike ViewScores. Its authored PNG already carries transparency.
- `ViewGameStats.set_mode_text` at `0x10066870`: the shipping client resolves
  the game-mode title through `MODE_TITLE`. The supplied screenshot says
  `GAMESTATS`; this implementation preserves the extracted shipping client's
  mode title rather than claiming the two versions are identical.
- Earlier verified `GameScene.process_packet_game_stats` at `0x102479C0`
  appends records into the packet-selected team list. Team 2/3 selects the
  award column, not the match winner. Empty team packets still count as an
  authoritative list.

## Changes

- Scoreboard now uses vacant opposite-column rows for players beyond sixteen
  on one team, and includes spectators in neutral gray. Their class icon,
  status and text color follow their actual team. The ordinary ChangeTeam
  screen retains its original behavior.
- Scoreboard frame opacity now follows retail's 0.8 modulation.
- Mode and result banner text keeps the original center and baseline but is
  fitted when it exceeds the frame width. Ordinary text retains its font size.
- An explicit equal-score snapshot stays a draw even if only one award packet
  arrived. The historical single-packet fallback applies only when no score
  snapshot was supplied.
- GameStats rejects malformed signed player/team IDs and unknown award
  ordinals instead of truncating them into valid identifiers. A pair of team
  packets remains authoritative even if one contains no winners.
- Live packet receipt snapshots each award winner's name and team. Disconnects
  or subsequent reuse of a player ID cannot delete or transfer that award.
- Vote count updates preserve the selected exact candidate token, even if rows
  are reordered. Updates after CLOSED cannot erase the displayed result, and
  updates before START cannot seed an unrelated ballot. Pressing F3 when only
  two options exist no longer starts the unanswered ballot's hide timer.

The last voting behaviors deliberately harden brittle retail behavior. Valid
START/CAST/UPDATE/CLOSED packets, exact candidate tokens, three-option key
bindings, normal geometry and five/six-second lifetimes remain compatible.

## Integration and verification

Live GameStats handling must call
`match_results.apply(packet, tutorial_roster)` so identities are captured at
packet receipt. `team_roster_state` must include wire-team-zero players in
`spectator_players`. The root implementation owns those two frontend hooks.

Regression targets: `aos_match_overlays_tests` and `aos_pause_menu_tests`.
Both targets rebuilt successfully and both CTests passed on 2026-09-20
(0.30 seconds total). Build log:
`BattleSpades/tmp/results-voting-build-20260920.log`.
New cases cover reordered count broadcasts, missing F3 candidates, late
updates, an authoritative draw with one stats list, malformed IDs, empty team
lists, disconnect/slot reuse, a seventeenth teammate, spectators, and frame
opacity. Existing tests continue to cover the 800x600 canvas, class/dead/VIP
icons, signed scores, score limits, localization and result layering.

This document does not claim a new interactive or pixel-identical retail
capture. The server owns score arithmetic, award selection and map-vote
results; this client work consumes that authority.
