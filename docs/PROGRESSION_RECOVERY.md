# Profile progression recovery

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](README.md) for present operating instructions and recheck historical findings against current source.

## Implemented boundary

The retail profile model is a read-only presentation of AoSPlay-owned statistics.
It does not calculate authoritative account awards. Current native hosting also
retries credential-free server-generated result reports through authenticated
service APIs; the backend validates their allocation/identity evidence and owns
stat/XP ingestion. That newer integration is described in [INVENTORY.md](INVENTORY.md).

The signed-in player path is:

1. `RevivalIdentityService` retains the protected bearer token.
2. **Player Profile** requests `GET /api/profile/me` on a worker thread.
3. The identity service returns only the validated profile; the token never
   crosses into the menu or renderer.
4. The standalone **Player Profile** menu may resolve a public numeric
   `POST /profile` record; retail leaderboard rows only select/highlight in
   place and do not navigate to profiles.
5. The native profile model calculates retail levels/ranks locally from the
   authoritative count/score pairs.

The recovery below establishes retail mastery calculations. Server statistics,
hosted-result ingestion and account XP have their own maintained contracts;
the original client-only investigation did not define their complete lifecycle.

## Retail evidence

The source of truth is the recovered Python 2 client:

- `shared/playerStat.py`: nonlinear level calculation and value modifiers.
- `shared/constants_playerprofile.py`: 182 profile `PlayerStat` declarations,
  17 summary rank tables, categories, filters, and equipment-stat generation.
- `shared/constants.py`: score-reason ordinals, labels, and the 1000/2000/3000
  weapon-stat namespaces.
- `aoslib/scenes/frontend/playerProfileMenu.py`: profile response handling,
  K/D calculation, rank comparisons, tab/filter order, and zero-row behavior.
- `aoslib/scenes/frontend/playerProfileListItems.py`: level-bar values.
- `shared/packet.pyx` plus the recovered game-scene strings: packet 66
  `RankUps`, packet 67 `GameStats`, and packet 53 `ShowGameStats`.

`tools/generate_player_progression.py` turns the first three sources into the
checked-in `src/frontend/player_progression.generated.inc`. The shipped binary
does not depend on decompiled files.

## Stored statistic model

AoSPlay stores every statistic as `[count, score]`:

- IDs `0..250`: score reasons and persistent counters.
- IDs `1000 + tool`: shots.
- IDs `2000 + tool`: hits.
- IDs `3000 + tool`: accumulated equipment points.

Retail uses `count` for level bars and most displayed counters. Definitions
marked `show_score` use `score`. Accuracy is `hits / shots`, truncated to an
integer percentage. Official-map playtime is stored in minutes and displayed
as hours with two decimals.

The profile screen includes configured zero-valued rows. Hiding them, as the
early native adapter did, changes both the menu structure and the meaning of an
empty/new profile.

## Exact level curve

Every progress-bar statistic starts at level 1. Its first threshold is
`level1_requirement * 2`. Promotion happens only after the count passes the
threshold; a value exactly on the threshold renders 100% while retaining the
old level. Subsequent thresholds use:

```text
next = round((previous + requirement * ((level - 1) * multiplier))
             * LEVEL_EASE_FACTOR[level])
```

The 100-entry ease table contains fifteen `2` values followed by `3..87`.
After the table is exhausted, the multiplier term continues without an ease
factor. The C++ implementation advances by level rather than iterating once per
hit, but preserves the Python 2 boundary behavior.

## Class and mode ranks

The summary has 17 independent rows: seven classes and ten modes. A row is
Intermediate only when every recovered Intermediate criterion's statistic
level is strictly greater than its required level. Advanced is checked only
after Intermediate succeeds. Total score is not a substitute and is never
copied across rows.

Two obvious retail-data defects are stabilized deliberately:

- Engineer Advanced contains two authored criteria while the menu hardcoded a
  count of three. The native client requires all two recovered criteria so the
  rank is attainable without inventing a third.
- VIP declares the `INTERMEDIATE` dictionary key twice. The 5-level tuple is
  treated as Intermediate and the 20-level tuple as Advanced, avoiding the
  original missing-key crash.

One ordinal bug is preserved because it affects stored data: the retail score
tuple names `SPECIALIST_AUTOPISTOL_KILLS` twice. Python tuple assignment leaves
the symbol bound to ID 250, so ID 238 has no reachable retail label.

## End-of-match progression

Packet 66 has the recovered variable layout:

```text
u8    packet_id = 66
i32   count
repeat count:
  i32 score_reason
  cstring old_score
  cstring new_score
```

The decoder bounds the record count and string sizes, requires decimal
non-negative values, rejects decreases/trailing bytes, and feeds the retained
result screen. The screen uses the authored
`view_game_stats_rankup_bar_stroke.png` asset and the same level calculator as
the profile menu.

## Backend contract and remaining dependency

The existing AoSPlay backend already provides the correct trust split:

- `GET /api/profile/me`: authenticated player's complete profile.
- `POST /profile`: public legacy-compatible profile by durable numeric ID.
- `POST /leaderboard`: recovered retail leaderboard presets.
- `/api/master/stats`: authenticated, idempotent server-side stat ingestion.

If a profile remains all zeroes, the display path is not the cause. The trusted
server bridge must resolve the connected identity and submit the real match
deltas. The client must never fabricate or upload its own progression.

## Verification

Focused tests cover the threshold edge, all 182 stat definitions, all 17 rank
definitions, map-hour conversion, weapon accuracy/points, the duplicate retail
ordinal, strict RankUps decoding, retained end-match data, and the authored
rank-up bar. Run:

```powershell
ctest --preset native-dev -R "aos_(player_progression|protocol168_runtime|match_overlays|player_profile_menu|aosplay_scores|revival_identity)_tests" --output-on-failure
```
