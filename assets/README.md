# Assets

`original/` is a local, immutable compatibility snapshot of the retail
content layout. It is ignored by Git and excluded from every source and binary
release. Runtime code must not rewrite it. Generated GPU-ready caches belong
in disposable output. The native Map Creator stores authored project triplets
and optional previews under `<settings directory>/hosted_ugc/maps/`; see the
[Map Creator guide](../docs/UGC_MAP_CREATOR.md).

`client/` contains the maintained native UI, fonts and cosmetic resources.
Keep catalogues, attribution, per-pack licences and resource hashes with those
assets. They are not disposable because they were imported or are untracked.

Player releases contain `BattleSpadesAssetInstaller` and
`asset-manifest.json`. On first launch, the client checks the imported tree and
opens the installer when files are missing or have the wrong size. The player
selects an existing Ace of Spades: Battle Builder installation; the installer
copies each required file into a temporary sibling directory while verifying
its SHA-256, then atomically activates the completed tree. Retail content is
never downloaded by or embedded in BattleSpadesClient.

Synchronize the canonical pack:

```powershell
py -3 tools/assets.py sync --source 'C:\Path\To\Owned\AceOfSpades\src'
```

Verify every path, size, and SHA-256 hash:

```powershell
py -3 tools/assets.py verify
```

The catalog at `catalog/original-assets.json` is safe to track. The original
binary content must remain outside Git history and release archives.
