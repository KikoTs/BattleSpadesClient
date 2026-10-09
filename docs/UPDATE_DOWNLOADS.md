# Update and asset download behavior

BattleSpades does not redownload the whole game for every client update. The
Windows launcher reads the schema-2 release manifest and updates independently
versioned ZIP packages:

| Component | Contents | When downloaded |
| --- | --- | --- |
| `client` | Client executable, runtime libraries, launcher, shaders and localization | A newer client version |
| `server` | Optional dedicated server bundle | Hosting installation, or a newer installed server version |
| `assets` | Project-owned files in `assets/client` | A newer asset-pack version |
| `retail_assets` | Original game files, imported and checked against the asset catalog | Explicit first-run installation, or a newer version of a previously downloaded pack |

Files imported from an existing Ace of Spades installation have no download
version and are never automatically replaced by a retail-pack update. The
client component excludes `assets/client`, `assets/original` and the server.
The initial Windows setup executable bundles the client and project assets;
subsequent launcher updates use the separate component ZIPs.

## What “chunks” means here

Transfers stream to disk in small buffers rather than keeping the package in
memory. Interrupted downloads keep their `.partial` file and resume using
`Range: bytes=<saved size>-`. A retry can continue from another mirror of the
same archive, including after closing and reopening the installer or launcher.

This is resumable downloading, not a binary-delta or content-addressed chunk
updater. A changed component still needs its complete new ZIP. Splitting that
same ZIP into parallel requests would not reduce the number of bytes a player
needs. Delta updates would require additional published chunk hashes, stable
packaging and retained base versions, with an ordinary ZIP fallback. The
existing manifest and release publishing format remain compatible.

## Avoiding wasted retries

The launcher and cross-platform asset installer share these rules:

- Reuse an already verified archive. A complete `.partial` is also checked
  locally and reused without an HTTP request, avoiding an empty range, an
  HTTP 416 response and an unnecessary complete redownload.
- Keep the expected size and SHA-256 beside partial data in
  `<package>.partial.identity`. A publisher replacing a package under the same
  filename no longer causes a retry to append a new version to an old prefix.
- Incomplete partials from earlier installers, which have no identity file,
  are adopted once so an upgrade preserves progress. Final SHA-256 checking
  still applies; an incompatible old prefix is discarded if verification fails.
- Accept a resumed HTTP 206 body only when `Content-Range` starts at the
  requested offset and contains a valid size. Broken range replies do not
  overwrite the valid prefix; another mirror can continue it.
- If a mirror ignores Range and returns HTTP 200, restart that file from zero.
  A host without Range support cannot resume an interrupted transfer.
- Bound package writes by the manifest size and check exact size and SHA-256
  before extraction. No unverifiable download reaches the installed files.

The Windows launcher uses WinHTTP. The asset installer uses libcurl on Windows,
Linux and macOS. Neither path performs parallel range downloads. Plain HTTP is
accepted only for the exact local test hosts `localhost` and `127.0.0.1`;
public mirrors use HTTPS.

## Publishing without forcing asset downloads

Keep component versions independent. `AOS_ASSETS_VERSION` is a CMake cache
setting; hold it at the existing asset version when `assets/client` has not
changed. Its default follows the release version, so leaving that default on
every build can cause unnecessary asset-pack updates. The manifest generator
also accepts `-AssetsVersion`, `-ServerVersion` and `-ClientVersion` separately.
Only advance a component version when publishing a changed, complete package.

Publish immutable, versioned URLs on mirrors, each serving the exact same ZIP
bytes. Support GET requests with Range, return a valid `Content-Range` for HTTP
206, and retain the full-file SHA-256 and size in the manifest. Do not recompress
or transform an archive independently on each mirror. The package builder and
manifest commands are described in [INSTALLER_AND_UPDATER.md](INSTALLER_AND_UPDATER.md).

## Verification

`aos_updater_download_tests` covers partial identity, complete-file reuse,
corrupt and oversized partials, range parsing and the loopback URL restriction.
`aos_updater_http_tests` runs the real Windows downloader against a local HTTP
server with valid, ignored, unsatisfied and malformed ranges, interrupted
responses, and oversized responses with and without Content-Length.
`aos_retail_download_tests` and `aos_retail_download_http_tests` exercise synthetic
ZIP download, cancellation, resumption, mirror fallback and verified import,
including malformed range replies through the real libcurl transport.

All test payloads are synthetic; no production download or retail file is needed.
