# Steam accounts and recovery

When Steam is ready, the client automatically signs in to an already-linked
Steam account. A saved unlinked password/guest account is verified first and
offers **Link Steam**, **Keep account**, or **Use separate Steam profile**.
The linking choice preserves the existing account's progress and identity;
it requires the existing session and a fresh verified Steam ticket. Conflicting
bindings leave both profiles intact and require support to resolve.
The client requests a Web API ticket with the `aosplay` audience,
waits for Valve's callback, and exchanges the ticket with the selected master.
The master verifies the AppID and SteamID with Valve and reads the persona from
Valve. A local Steam persona alone never creates an authenticated AoSPlay session.

Account identity is its stable account ID and SteamID. The verified Steam display
name appears in game; changing it does not change the registered username or move
progress between accounts. Signing into Steam never automatically links or merges
an unrelated password account. Guest/account buttons remain usable while Steam
starts; choosing one cancels automatic sign-in for that visit. Explicit offline
launches skip Steam authentication entirely.

Missing Steam, ticket failures or an unavailable authentication service leave the
normal account, guest and recovery controls available. Existing account records
and saved guest keys are retained. A failed login cannot adopt a mismatched
SteamID response.

## Backup files

The first verified Steam account login initializes a recovery code. Registered
account creation and successful Steam recovery also return a code. The client
shows it and saves a private text file under **Documents/BattleSpades**, outside
the game installation. Each backup contains the service address, stable account
ID, registered name, SteamID (when applicable), code and recovery instructions.
File names do not use display names and are created exclusively: another account
or code never overwrites an existing file. Windows files have a protected
owner/System ACL; Unix files use mode 0600.

A second copy uses Windows DPAPI or an owner-only launcher state file on Linux
and macOS; the latter is not encrypted. State files are written privately from
creation and replaced atomically. If writing Documents fails, the UI asks the player to keep the shown code;
the next Steam sign-in can retry from protected local state after checking the
server's current backup version. A code replaced on another PC is never restored
as a current backup. Ordinary sign-ins do not
rotate codes. Existing codes held on another PC are not silently replaced; the
backend supports an explicit replacement with fresh Steam proof.

Without Steam, choose **Recover account**, enter the SteamID and code from the
file, and select Recover. The master revokes old sessions/join tickets and issues
a replacement recovery code, which is saved to a new file. Password accounts use
the website's existing username/password recovery. Recovery restores the same
account; it does not create a second account with the old display name.

## Server admission and deployment

AoSPlay login tickets and game-server tickets are separate handles and lifetimes.
App 480 may identify a Steam user to a master configured to verify that app; it
does not prove ownership of app 224540. Retail game-server tickets are issued only
by a verified 224540 runtime and are canceled when the connection ends.
Server-owned bans, passwords and ownership requirements still apply.

The website ticket/recovery routes and migration
`0019_steam_launcher_identity.sql` must be deployed together before automatic
account sign-in is available. See the website's `docs/STEAM_LAUNCHER_AUTH.md`.

Regression tests cover callback ownership/lifetimes, immutable account identity,
unexpected subjects, recovery backups and replacement, and the existing identity
menu. The loopback HTTP fixture never contacts Valve or a production account.
