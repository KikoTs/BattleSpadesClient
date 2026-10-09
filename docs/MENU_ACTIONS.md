# Menu actions and UX contract

This is the button map for the native client. Labels may be translated. Repeated
rows, arrows, tabs and selectors are grouped by function. Production routing is
owned by [native_frontend_module.cpp](../src/frontend/native_frontend_module.cpp);
the corresponding menu model owns hit testing and enabled state. An implemented
handler is not evidence that an external service accepted an operation.

## Common behavior

Buttons activate on release; route transitions gate input. Back/Escape returns
to the parent unless a modal or committed operation owns input. Lists clamp
selection and scrolling. A pending network operation must show progress and
settle into confirmed state or a retryable error. Empty/offline state must not
pretend to be a successful request. The reference canvas is 800 by 600 and is
letterboxed at other aspect ratios.

## Identity and main menu

| Button/control | Result and availability |
| --- | --- |
| Username / Password | Focus and edit bounded text; password is masked. Tab changes focus. |
| Sign In | Asynchronous AoSPlay login; repeated submission blocked while busy; failure stays in the form. |
| Register | Registers the entered account; presents recovery information before entering the menu. |
| Continue | Acknowledges the registration recovery screen. |
| Sign In Through Steam | Native Steam identity flow; shown only when the integration is available. |
| Play as Guest | Enters with guest identity; account-owned operations remain capability-gated. |
| Tutorial | Starts the local playable tutorial. |
| Friends | Opens the social friends/invitations screen. |
| Leaderboard | Opens the asynchronous score view. |
| Settings | Opens a draft of confirmed settings. |
| Map Creator | Opens Create / Publish / Workshop selection. |
| Player Profile | Loads the current profile and its statistic/inventory tabs. |
| Create Match | Opens the lobby immediately and requests social creation; editing/start waits for authoritative membership. Existing membership reopens that lobby. |
| Join Match | Opens the join-method menu. |
| Logout | Cancels account inventory work, clears account UI state and returns to identity. |
| Quit | Requests normal client shutdown, including owned-session cleanup. |

Sources: [identity](../src/frontend/identity_menu.cpp),
[main menu](../src/frontend/main_menu.cpp).

## Join, server browser and Quick Play

| Button/control | Result and availability |
| --- | --- |
| Server Browser | Loads public discovery and opens the list. |
| Direct Connect | Opens the address field; Connect validates the endpoint before entering the normal loader. |
| Add Favourite (direct address) | Validates and persists the endpoint without joining it. |
| Random Match / Quick Play | Opens playlists and starts asynchronous discovery. |
| Custom Match, where exposed | Opens the same Friends/social route used by Create Match. |
| Source arrows | Cycle All, Official, Community, Favourites, History, Friends and Local; refresh the chosen source. Friends uses current social server IDs; Local uses LAN discovery. |
| Region selector | Filters supported internet sources; irrelevant source types ignore the region. |
| Show Full / Show Empty | Filter the list locally; they do not alter server state. |
| Column headings | Sort by name, players, map, mode or ping. |
| Server row | Selects details; double-click uses the same validated connection path as Connect. |
| List scroll arrows / wheel / scrollbar | Move the visible list within its bounds. |
| Refresh | Starts a new discovery generation; old results cannot replace the current source. |
| Add / Remove Favourite | Persists the selected endpoint's favourite state. Disabled without a usable selection. |
| Connect | Joins the selected compatible server through ticket acquisition and Protocol 168 loading. |
| Playlist / discovered server row | Selects a playlist or concrete server and shows its details. |
| Quick Play Start | Uses a matching discovered server or starts the playlist search. Unavailable/searching states gate repeated requests. |
| Quick Play Buy, if exposed | Opens the legacy Mafia DLC Steam store page; it does not buy or unlock content itself. |
| Back | Cancels the active menu search where applicable and returns to the join menu. |

Sources: [join/browser](../src/frontend/join_match_menu.cpp),
[Quick Play](../src/frontend/quick_play_menu.cpp),
[loading](../src/frontend/loading_screen.cpp).

## Match loading

| Button/control | Result and availability |
| --- | --- |
| Map / Mode / Scores | Selects a tab and stops automatic tab cycling. Drawing and hit testing share the same tab bounds inside the loading frame. Training/editor sessions expose only their applicable tabs. |
| Map | Shows the selected map's loading art and name within the content frame. |
| Mode | Shows the mode infographic with three captions supplied by server `InitialInfo`; missing captions use the stock mode's localized text. |
| Scores group heading / plus-minus | Expands or collapses the mode-specific or generic score group. This is a stock scoring reference filtered by mode and friendly-fire settings, not the current match scoreboard or a server-supplied custom scoring table. |
| Scores wheel / scrollbar arrows / track | Scrolls the score reference within its visible rows; collapsing a group keeps the scroll position valid. |
| Start | Enters the match only after the actual connection, map synchronization and asset preparation are ready. A full-looking progress bar or visible Start button does not bypass readiness. |
| Cancel / Back | Cancels the current connection/hosting generation; late completion must not enter a match. |

Loading also displays local server startup, public hosting and authentication
progress before the connection is ready. Errors remain readable in the footer
with a route back to the lobby. Map art, infographic captions, score rows and
footer notices stay within the shared frame at each supported window size.
Back during hosting queues the lobby's `start_failed` recovery before releasing
the owned session. It also invalidates the canceled identity ticket, so a retry
at the same endpoint requests a ticket for the new server.
Returning during startup replaces the old starting notice with a cancellation
confirmation. A retry while the previous worker is still closing reports cleanup
rather than telling the player that a canceled match is still starting.
Transport and server shutdown run on bounded cleanup workers. Back does not
join the connection resolver or wait for server shutdown; a retry can briefly
report that earlier resources are still closing when the capacity is full.
A canceled startup failure cannot replace the current menu notice or fail a
different lobby. If a hosted match disappears while Pause or Settings is open,
the entire match route and its overlays return to their lobby/menu ancestor.

Sources: [loading model](../src/frontend/loading_screen.cpp),
[loading presentation](../src/frontend/loading_presentation.cpp).

## Friends and match lobby

| Button/control | Result and availability |
| --- | --- |
| Friends / Requests / Invites tabs | Change the visible relationship/invitation view. |
| Search field / Search | Looks up the entered player; disabled while busy or disconnected. Results open the Friends tab. Editing the query removes old search-only rows, and a late result cannot replace a newer search. |
| Friend row / invitation row | Selects one target; choosing either clears the other selection. |
| Up / Down; Left / Right; Tab / Shift-Tab | Moves and reveals the selected row, or cycles the three tabs. Enter searches only while the search field is focused; otherwise it activates the selected primary action. Escape uses the same Back path as the button. |
| Friends / invitations wheel | Scrolls the panel under the pointer, including the invitation panel when the Friends tab is selected. |
| Primary friend button | Accepts an incoming request, adds a search result, joins a friend's game/lobby, invites an online friend to the current lobby, or creates a lobby before inviting. Label and enabled state use the exact same intent as the click handler. |
| Request Sent / Friend Offline / Already in Lobby / Lobby Full / Your Profile | Informational disabled primary states; the label explains why the selected action is unavailable. |
| Secondary friend button | Declines an incoming request, cancels an outgoing request or removes an accepted friend. |
| Accept / Decline invitation | Acts on the selected invitation, including expiry/service validation. Leave an existing lobby before accepting an invitation to another one. Seven visible invitation rows match paging and hit testing. |
| Create Lobby | Requests one social lobby; pending requests block duplicate submission. |
| Open Lobby / Join Game (no friend selected) | Reopens current lobby membership, or joins its already-published match. Returning from Friends does not create another lobby or host. |
| Friends Back | Returns to the prior route and cancels the pending join/navigation intent. Membership remains available through Open Lobby. The same canceled match is not automatically retried; explicit Join Game or a new host start can retry. |
| Invite (match lobby) | Opens Friends for selecting an invitee. |
| Roster previous / next | Pages eight players at a time; controls sit below the roster and do not overlap Invite. |
| Player Team / Kick | Owner-only team assignment or removal of a lobby member; rechecks the pressed member and current authority on release. In-game players cannot be edited and the owner cannot be kicked. |
| Chat input / Enter | Sends lobby chat through the social service; pending/invalid submissions are gated. |
| Privacy / maximum players / duration / other setting choices | Change the owner's draft and queue revision-checked updates. Members see the authoritative values. |
| Game Mode | Opens available modes; selecting one applies its compatible maps/rules. |
| Map | Opens compatible maps; selecting one returns its choice to the draft. |
| Game Rules / category / rule value | Expands rule groups and changes supported values. Owner/forming-state authority applies to every edit. |
| Defaults | Restores the owner's match draft defaults, or clears rule overrides on the Rules page. |
| Subpage Back | Returns to Match Settings without leaving the lobby. |
| Subpage Confirm | Accepts the current mode/map/rule page and returns to Match Settings. |
| Start Game | Starts the online lobby. Pending settings save first; one launch identity survives retries. No confirmed lobby means creation/retry, with visible status. |
| Join Game (published lobby) | Replaces Start Game when the current lobby has a ready match. Owners and members join that match; this never launches another server or grants settings authority. |
| Local Match | Explicitly starts a server on this computer without a public relay. Requires the bundled server, owner authority and at most one lobby member. |
| Leave Lobby / lobby Back | Leaves authoritative membership. If creation is still pending, completes cleanup when that result arrives instead of leaving an orphan lobby. |

Match Lobby uses a compact, single-line status strip in the free space above
navigation, clear of Start, Leave and the roster. Friends retains a separate
heading and bounded two-line notice beside its action buttons. Long messages
end with an ellipsis instead of reducing the font to unreadable text; notice
surfaces stay inside the design canvas.
Friends uses the same retail frame, tabs, textured buttons and selection
highlights as the other menus. Rows distinguish incoming/outgoing requests,
search results, online/offline friends, lobbies and games. The selected target
appears above the action buttons; hover, pressed and disabled visuals follow
the model's actionable state.
Service polling reconnects automatically; failed actions can be retried with
their original button when available again.
Full diagnostic log tails stay in logs. Friends retains a short completion
debounce; ordinary polling does not
reset it, and a changed action between mouse press/release is rejected.

Sources: [friends](../src/frontend/friends_lobby_menu.cpp),
[lobby](../src/frontend/create_match_menu.cpp),
[social transport](../src/network/revival_social.cpp).

## Profile, leaderboard and inventory

| Button/control | Result and availability |
| --- | --- |
| Player Stats / Game Modes / Classes / Equipment / Inventory | Selects the profile section; statistics and cosmetic ownership are separate data contracts. |
| Profile filter dropdown / stat row / arrows | Filters or selects the associated statistic detail and scrolls the bounded rows. |
| Achievements | Opens `https://www.aosplay.net/account` in the external browser. |
| Leaderboard type / scope selectors | Requests the chosen statistic and scope. Open dropdowns own input over covered rows. |
| Leaderboard headings / row and column arrows | Sorts or scrolls the displayed results. |
| Leaderboard player row | Selects/highlights the row. This is informational and does not open a profile. |
| Collection / Crates / History / Skin Packs | Switches inventory sections and resets section selection/paging. |
| Owned / Kind / Rarity | Filters collection cards and resets to a valid selection. |
| Item card / crate card | Selects an inspector or sealed crate; does not equip or spend by itself. |
| Previous / Next | Pages six cards; crate/history boundaries fetch cursor pages when available. |
| Pool selector / Inspect item | Browses that crate pool or locates a cosmetic in the collection. |
| Class / slot selector | Chooses the equip destination allowed by the cosmetic. |
| Equip / Unequip | Changes the selected owned cosmetic using authoritative revision checks. Weapon view/world slots reconcile as a pair. Disabled for guests, offline/unloaded state, feature-disabled state or a busy operation. |
| Sight / barrel choices / Reset variants | Saves supported per-skin local variant choices; no account ownership mutation. |
| Rotate/Pause / Team / Reset View | Controls the model preview; dragging rotates and wheel changes preview zoom. |
| Refresh | Reloads the account snapshot; single-flight and blocked by a reward overlay. |
| Open Crate | Opens the selected sealed crate once using a stable operation identity. The reveal appears only after the committed receipt. |
| Skip / Continue | Finishes the animation or closes the committed reward. Escape follows the same reveal flow. Background account actions are blocked. |
| Creators | Opens `https://www.aosplay.net/creators/skin-packs`. |
| Back / profile tabs | Returns to the parent or another profile section. |

Current and next-page thumbnails are queued in the background. Visible work is
prioritized and at most two completed thumbnails upload each frame. Equipped
assets warm a bounded, thread-safe verified-model cache. File identity, size,
mtime and expected digest distinguish cached resources. Changed/missing assets
are revalidated and retain base-model fallback. Preview changes clear the old
hero immediately; old completions cannot install another selected item's image.
Canceled inventory results cannot repopulate a cleared model, even for the same
account. A rejected mutation preserves an otherwise usable online snapshot.

Sources: [profile](../src/frontend/player_profile_menu.cpp),
[leaderboard](../src/frontend/leaderboard_menu.cpp),
[inventory view](../src/frontend/inventory_view.cpp),
[inventory session](../src/frontend/inventory_session.cpp). See [Inventory](INVENTORY.md).

## Settings and in-game menus

| Button/control | Result and availability |
| --- | --- |
| Main / Graphics / Controls tabs | Change the settings section; preserve the current draft. |
| Language, volume, fullscreen, invert mouse, favourite server, skin visibility, weapon motion | Edit the relevant draft preference; supported live previews apply immediately. |
| Music on silent servers | Optional retail gameplay music for live servers without music cues. Off by default, uses Music Volume, previews immediately and restores on Cancel. Server PlayMusic/StopMusic takes priority for the map. |
| Ragdoll corpses | Enables the articulated death-soldier body for new Deuce deaths in Classic modes. Gravity applies with either setting; other classes retain their own character models. |
| Resolution, graphics API, antialiasing, effects, draw distance, shaders, texture/model quality, VSync, compatibility shader | Edit supported rendering/display options. Restart-only choices are identified by settings handling. |
| Mouse sensitivity | Edits the draft sensitivity. |
| Binding rows | Capture a keyboard/mouse binding for the named movement, weapon, chat, map, scoreboard, class/team, vote, HUD or editor action. Capture owns input; Escape cancels capture. |
| Category headers / list scrollbar | Expand or scroll the settings list without applying it. |
| Defaults | Resets the active settings draft through the settings session. |
| Cancel / Back | Restores confirmed values, including previewed settings. |
| Done | Commits/persists the draft; risky display changes enter confirmation. |
| Keep / Revert display | Confirms the new mode or rolls it back; timeout also rolls back. |
| Pause Resume | Closes the menu and restores gameplay input. |
| Pause Settings | Opens the same draft settings flow in the in-game context. |
| Pause Change Class | Opens server-advertised classes; loadout selection and Submit use server authority. |
| Class / weapon / equipment row; class Submit / Back | Selects a valid class/loadout and submits, or returns according to initial-join/pause context. |
| Pause Change Team; team buttons / Back | Requests a permitted team change; forced team, VIP and match-end rules can restrict choices. |
| Pause Disconnect | Returns to the main menu and tears down the owned connection/server/tunnel. |
| Editor Constructs / Game Data | Opens the editor loadout or host-authorized world settings in the corresponding editor context. |

Sources: [settings](../src/frontend/settings_menu.cpp),
[pause](../src/frontend/pause_menu.cpp),
[class selection](../src/frontend/class_selection_menu.cpp),
[team selection](../src/frontend/change_team_menu.cpp). Per-option behavior is in [Settings](SETTINGS.md).

## Map Creator and publishing

| Button/control | Result and availability |
| --- | --- |
| Create Map | Opens editor lobby discovery. |
| Publish Map | Scans saved projects into the publishing screen. |
| Subscribe / Workshop | Opens the native CSS-styled Workshop browser: Steam / AoSPlay / My Subscriptions, search, download, account subscription sync, and removal of browser-owned files. |
| Editor source filter / Refresh | Selects open/friend editor lobbies and refreshes their authoritative list. |
| Lobby row / Join | Selects and joins an available editor lobby; full/invalid rows cannot launch. |
| New Lobby | Creates the editor lobby; the local editor path remains available when online discovery is unavailable. |
| Editor privacy, player limit, map, prefab set, target mode, title | Changes the host configuration; members cannot overwrite it. |
| Editor Invite | Opens Friends for the current editor lobby. |
| Start Editor | Saves/reconciles the configuration, launches the owned editor server and waits for readiness. |
| Editor Leave / Back | Leaves membership and returns to the browser with pending-creation cleanup. |
| Constructs library / category / item / slot | Selects an allowed prefab/tool and destination slot; pagination and scrolling remain bounded. |
| Constructs Select / Back | Submits the selected editor loadout or returns. |
| World sky / water / mode / title / preview controls | Edit the host-only world draft; preview capture uses the current scene. Apply sends the draft; Cancel restores the opening snapshot. |
| Saved-project row | Selects local project details and publishability. |
| Publish / title Continue | Validates metadata and opens the publish confirmation. |
| Publish Confirm / Cancel | Confirms the asynchronous Revival Workshop upload or returns without uploading. Successful upload opens the returned item page. |
| Delete / Confirm / Cancel | Explicitly confirms deleting the selected local repository project, or cancels. |
| Operation OK / Back | Dismisses error/completion dialogs; uploading/deleting blocks duplicate operations. |

Sources: [editor menus](../src/frontend/ugc_editor_menu.cpp),
[constructs](../src/frontend/ugc_loadout_menu.cpp),
[publishing](../src/frontend/ugc_publish_menu.cpp). See [Map Creator](UGC_MAP_CREATOR.md).

## Native hosting and verification boundaries

The client supplies a unique session directory and private configuration to its
owned server. New bundles publish atomic `host-status.json` snapshots using
schema 1: session, port, mode and lifecycle state. The client validates identity
and size before consuming readiness. The server publishes ready only after
map/mode/services/bots initialize. No identity token is written to the status
file. Existing stdin shutdown remains the parent-to-server control channel;
older bundles retain LAN readiness fallback. This is a local lifecycle bridge,
not an extra public server API.

Regression coverage includes Friends button/action agreement, press/release
changes, invitation paging, lobby control geometry, stale social revisions,
inventory cancellation, cache invalidation, lost replies, GPU preview loading
and bridge session validation. Loading checks cover shared frame/tab bounds,
mode-caption fallback, bounded score groups and readiness gating. The server lifecycle tests cover readiness and
startup failure. The local host smoke can require the new bridge:

```powershell
ctest --preset native-dev -R 'friends_lobby|create_match|inventory|local_server|revival_social' --output-on-failure
& .\out\build\native-dev\src\RelWithDebInfo\aos_local_server_host_smoke.exe <server-bundle> --native-bridge
```

The retained `CustomMatchMenu` model is an offline developer fixture, not a
second production social adapter. F11 and parity/debug screens are developer
tools documented separately. External web pages, Steam ownership, real account
mutations and multi-client public-relay acceptance need their corresponding
live environment; local tests do not certify them. No live purchases, invites,
crate openings or public uploads are required for this regression pass.
