# Building on GitHub Actions

`.github/workflows/build.yml` ("Build client") builds, tests and packages the
client for every supported platform. It runs **only when started by hand**;
pushes never trigger it. The repository is public, so the standard
GitHub-hosted runners it uses cost nothing. It never uses larger (paid)
runners.

For local builds see the README ("Building from source") and
[RUNBOOK.md](RUNBOOK.md).

## Running it

Actions tab -> **Build client** -> **Run workflow**, or from a terminal:

```sh
gh workflow run build.yml                                   # all six targets, smoke tests
gh workflow run build.yml -f targets=linux-x64,macos-arm64  # a subset
gh workflow run build.yml -f run_tests=full                 # complete CTest on linux-x64
gh workflow run build.yml -f publish=draft-release -f version=0.2.0-beta.1
gh run watch                                                # follow the newest run
```

Starting a new run on the same branch cancels the one already running there.

### Inputs

| Input | Values | Meaning |
| --- | --- | --- |
| `targets` | `all` (default) or a comma list | `windows-x64`, `windows-arm64`, `linux-x64`, `linux-arm64`, `macos-arm64`, `macos-x64` |
| `run_tests` | `smoke` (default), `full` | `smoke`: on every target, check the installed payload, run `BattleSpadesClient --version`, `--headless --ticks 1`, `BattleSpadesAssetInstaller --help` and the fast unit tests in `SMOKE_TESTS`. `full`: linux-x64 runs the whole CTest suite instead (others still run smoke) |
| `publish` | `none` (default), `draft-release` | `draft-release` needs `targets=all` and every target green, then attaches all packages and `SHA256SUMS` to a **draft** release `v<version>`. It never publishes; you review and press Publish. An already published release is never touched |
| `version` | empty or e.g. `0.2.0-beta.1` | For `draft-release`. Must equal the version the source builds (`AOS_RELEASE_VERSION` in `CMakeLists.txt`); empty means "use it". A mismatch fails rather than mislabels |

## Targets

| Target | Runner | Preset | vcpkg triplet |
| --- | --- | --- | --- |
| windows-x64 | `windows-2025` | `native-release` | `x64-windows` |
| windows-arm64 | `windows-11-arm` | `native-windows-arm64-release` | `arm64-windows` |
| linux-x64 | `ubuntu-24.04` | `native-linux-release` | `x64-linux` |
| linux-arm64 | `ubuntu-24.04-arm` | `native-linux-release` | `arm64-linux` |
| macos-arm64 | `macos-15` (Xcode 26.3) | `native-macos-release` | `arm64-osx` (overlay in `cmake/triplets`) |
| macos-x64 | `macos-15-intel` (Xcode 26.3) | `native-macos-release` | `x64-osx` (overlay in `cmake/triplets`) |

Notes:

* Windows on Arm has no Steamworks runtime from Valve, so that build ships
  without `steam_api64.dll` and friends play through the AoSPlay relay. The
  32-bit Steam bridge (`BattleSpadesSteamBridge32.exe`) is still included; it
  runs under the built-in x86 emulation.
* macOS needs Xcode 26 (libc++ 20) for `std::jthread`/`std::stop_token`.
  The job fails with the list of installed Xcodes if the image ever drops
  26.3; update the path in the workflow.
* Retail game files are never on a runner and never in an artifact; the
  payload check fails if `assets/original` appears in a package.

## Outputs

Each target uploads one artifact, `BattleSpadesClient-<target>`, kept for
14 days:

| Target | Files |
| --- | --- |
| windows-x64 | `BattleSpadesClient-<v>-windows-x64.zip` (portable), `BattleSpades-Setup-<v>.exe` (Inno Setup installer, built without the server), `BattleSpades-client-<v>-windows-x64.zip` and `BattleSpades-assets-<v>.zip` (updater components) |
| windows-arm64 | `BattleSpadesClient-<v>-windows-arm64.zip` |
| linux-* | `BattleSpadesClient-<v>-linux-<arch>.tar.gz` (tar keeps the executable bits) |
| macos-* | `BattleSpadesClient-<v>-macos-<arch>.tar.gz` containing the unsigned `BattleSpadesClient.app` |

Every artifact also has `SHA256SUMS-<target>` and `VERSION`. With
`publish=draft-release` the release gets one combined `SHA256SUMS`, and an
`update-manifest-draft` artifact holds a `stable.json` that lists only the
GitHub release mirror. Nothing is uploaded to R2: upload the component zips
yourself and regenerate `stable.json` with both mirrors as described in
[INSTALLER_AND_UPDATER.md](INSTALLER_AND_UPDATER.md).

Unsigned macOS builds: right-click the app -> Open the first time.

## Caches

* **vcpkg binary cache**, one per target. Key: target + compiler version +
  hash of `vcpkg.json` and `cmake/triplets`. It is restored before vcpkg runs
  and saved right after the dependencies install, before our own code
  compiles, so a compile error in the client never throws away a dependency
  build. If vcpkg itself fails, the packages that did build are saved as a
  partial cache for the next attempt. A new compiler (runner image update) or
  manifest change starts from the newest older cache and vcpkg rebuilds only
  the packages whose ABI changed.
* **ccache** for the client's own sources on every target (MSVC included:
  Release builds carry no `/Zi`), saved after every build attempt, so a run
  that fixes one file recompiles only what changed.
* **apt downloads** (Linux), keyed on `.github/ci/linux-packages.txt`.
* vcpkg is pinned to the manifest's `builtin-baseline` commit, every action
  to a commit SHA, and runner images by name.

GitHub keeps 10 GB of caches per repository and drops entries unused for
7 days. Caches can be listed and deleted under Actions -> Caches or with
`gh cache list` / `gh cache delete`.

## Reliability rules

* `fail-fast: false`: one broken platform never cancels the others.
* Network steps (git clone, apt, brew, vcpkg downloads, FetchContent,
  Inno Setup download) retry up to three times, **only** when the output
  shows a network failure (`.github/ci/retry.sh`, `.github/ci/ci.ps1`). A
  compile or test failure fails immediately. Jobs are never re-run
  automatically.
* `.github/ci/full-tests-skip.txt` lists the CTest names `full` skips and why
  (only tests that need retail assets or a GPU belong there).
* When vcpkg fails, its build logs are uploaded as `vcpkg-logs-<target>`;
  when the full suite fails, the JUnit report as `ctest-report-linux-x64`.

## Durations

Measured on the first runs (October 2026). "Cold" is a target's first run with
no caches (vcpkg builds every dependency from source); "warm" is the next run
after a code change, with the vcpkg and ccache caches restored.

| Target | Cold | Warm | Notes |
| --- | --- | --- | --- |
| windows-x64 | ~27 min | 7 min | vcpkg 13 min cold; warm time is mostly linking, the Steam bridge and the installer |
| windows-arm64 | ~27 min | 7 min | vcpkg 14 min cold |
| linux-x64 | ~25 min | 3 min | vcpkg ~16 min cold (libsystemd, alsa, dbus from source); apt took up to 10 min on a slow mirror before its cache existed |
| linux-arm64 | ~23 min | 2 min | vcpkg ~18 min cold |
| macos-arm64 | ~16 min | 3 min | vcpkg 9 min cold |
| macos-x64 | ~40 min | 5.5 min | Intel runners are slow: vcpkg 26 min cold |

Jobs run in parallel, so a warm `targets=all` run takes about 7 minutes of
wall-clock time and a cold one about 40 (bounded by macos-x64). With
`run_tests=full`, linux-x64 runs 140 tests in about 40 seconds more. The
caches together use about 2 GB of the 10 GB allowance.

