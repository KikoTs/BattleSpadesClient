# Discord Rich Presence

The native client can display the current server, map, mode, player count and
elapsed session time in the Discord desktop app. **Discord activity** and
**Discord joining** are independent toggles in the existing Main settings tab.
Both preferences persist, preview immediately and follow the menu's normal
Done/Cancel behavior. No bot token, account token or Discord SDK is required.

## Application setup

The client defaults to the existing **BattleSpades** Discord application,
public Application ID `1557497536602701824`. Its name/icon were checked against
[Discord's public application metadata](https://discord.com/api/v10/oauth2/applications/1557497536602701824/rpc).
No artwork key is invented: Discord uses the application's existing branding.
For another application, set `AOS_DISCORD_APPLICATION_ID` in the environment
before launching, or configure builds with
`-DAOS_DISCORD_APPLICATION_ID=<public-application-id>`. The environment value
takes precedence over the build setting; empty values use the bundled ID.
An explicitly invalid ID disables the transport quietly.
The Discord desktop app must be running and the player must permit activity
sharing. The application's configured name/icon supply the presentation;
uploaded artwork is not a prerequisite.

Discord explicitly supports
[Rich Presence without authentication](https://discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence#rich-presence-without-authentication).
Do not put a client secret or bot token into a client build, settings file or
environment variable intended for this integration.

## Joining and privacy

For a public IPv4 server without a password, Discord receives a canonical
`aosbb://host:port` join value with an explicit `:0.75` or `:0.76` suffix when
needed. Native **Join Game** events are validated again and delivered to the
game's normal connection flow on its main thread. Invalid/private targets,
query strings, arguments, credentials and unknown protocols are rejected.
Repeated events for the same target are suppressed for five seconds.

The **Join server** HTTPS button points to
`https://www.aosplay.net/join?server=<host:port>&protocol=<version>`.
The website must deploy that route alongside the client feature. It validates
the address and offers an explicit **Open BattleSpades** link; this supports
starting an installed client too. Native Discord invite delivery targets an
already running client; no `discord-<application-id>` operating-system handler
is registered by this integration. Discord may prioritize native join actions
over custom buttons. Buttons are visible to other users, so profile verification
requires a second account. See
[Discord's button documentation](https://discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence#setting-buttons).

LAN/private, DNS-only and relay endpoints get no join action. Private sessions
use a generic server label and a process-local party identifier so addresses do
not leak through the party field. Password-protected public servers also get
no join action. Passwords, authentication/admission tickets, Steam IDs and
player identity are not accepted by the presence data model. Map/mode text
remains visible when activity sharing is enabled. Disabling joining also stops
accepting Discord join events; disabling activity clears presence and closes
the transport.

## Transport and verification

The implementation follows Discord's
[documented IPC framing](https://discord.com/developers/topics/rpc#rpc-over-ipc)
and [Rich Presence event contract](https://github.com/discord/discord-rpc/blob/master/documentation/hard-mode.md).
Windows uses overlapped named pipes; Linux/macOS use nonblocking local Unix
sockets owned by the current user. One worker owns all IPC. Updates coalesce
at 15-second intervals, while disconnect/privacy transitions clear or revoke
join details promptly. Missing Discord retries every 30 seconds. Handshakes
expire after three seconds, unanswered activity commands after ten seconds,
and frames/nesting are bounded. The game loop never waits for IPC or DNS.

`aos_discord_presence_tests` runs an isolated local fake Discord endpoint and
tests real framing, fragmented reads, heartbeat replies, activity fields,
update coalescing, safe join callbacks, duplicate suppression, privacy changes,
clearing and malformed-frame rejection. It does not connect to Discord or
publish test activity. The settings suites verify persistence and the normal
preview/Cancel/Done behavior. Real profile visibility and Discord's application
configuration require manual verification with the configured application ID.

For an explicit real-client check, build `aos_discord_presence_probe` and run
it with `--publish-and-clear`. This briefly sets a verification activity with
joining disabled and waits for Discord's matching SET_ACTIVITY acknowledgements
for both publication and clearing. It never sends messages, joins a server,
uploads assets, or exposes user data. Without that flag it only prints usage.

On 2026-10-09 the Windows probe against the running Discord client returned
`published_ack=true cleared_ack=true` using the bundled BattleSpades application.
This verifies actual IPC acceptance and cleanup, not another user's visual
profile view or the website's deployment state.
