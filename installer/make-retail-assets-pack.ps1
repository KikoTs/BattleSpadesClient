<#
.SYNOPSIS
Builds the retail_assets pack ("Download game assets") from YOUR OWN Ace of
Spades installation, in exactly the layout the launcher and
BattleSpadesAssetInstaller expect, and prints its size, SHA-256 and root.

.DESCRIPTION
The pack is a ZIP with ONE top folder (the "root", default
BattleSpades-retail-assets-<version>) that holds:

  * every file listed in assets/catalog/original-assets.json (shipped as
    asset-manifest.json), at the same relative path: png/, maps/, kv6/,
    sounds/, ... list.pnq. Each one is checked for its exact size and SHA-256
    first; a modded or missing file stops the script with the list of
    problems (fix them with Steam's "Verify integrity of game files").
  * the optional Japanese UI fonts, when your copy has the exact retail ones.
  * steam_api.dll and steam_appid.txt when present (the importer copies the
    32-bit Steam runtime into steam\win32 for the Steam bridge).

Nothing else from the game folder is packed: no executables, no .pyd/.i64,
no configs, logs, mods or BattleSpades subfolder.

The launcher downloads the ZIP (resumable, size + SHA-256 checked), finds the
root and runs the SAME importer validation as "Use my Ace of Spades folder"
on it. -TestImport runs that importer on the finished ZIP right here.

The script never uploads anything and refuses to write the pack inside the
git repository except under out\ (ignored by git). Host the ZIP yourself, then
pass the printed values to installer/make-update-manifest.ps1.

It also runs on a gaming PC without a checkout of this repository: copy this
one script anywhere (Windows PowerShell 5.1 is enough). The asset catalog is
then taken from -Manifest, an installed BattleSpades (asset-manifest.json), or
downloaded from the public repository; the pack is written to .\retail-assets.

.EXAMPLE
./installer/make-retail-assets-pack.ps1 -Version 1.0.0

.EXAMPLE
./installer/make-retail-assets-pack.ps1 -GameDir 'D:\SteamLibrary\steamapps\common\aceofspades' `
    -Version 1.0.0 -OutputDir 'D:\packs' -TestImport

.EXAMPLE
# On a PC with only the game and BattleSpades installed:
powershell -ExecutionPolicy Bypass -File .\make-retail-assets-pack.ps1 -Version 1.0.0 `
    -PublicUrl 'https://files.example.net/BattleSpades-retail-assets-1.0.0.zip' -TestImport
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [string]$Version = '1.0.0',
    [string]$Manifest,
    [string]$OutputDir = 'out/retail-assets',
    [string]$RootName,
    [switch]$TestImport,
    [string]$Importer,
    # Where you will host the ZIP; only used to print the exact manifest command.
    [string[]]$PublicUrl = @(),
    [string]$CatalogUrl = 'https://raw.githubusercontent.com/KikoTs/BattleSpadesClient/main/assets/catalog/original-assets.json'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# A checkout of this repository, or just this script copied to a gaming PC.
$inRepo = Test-Path -LiteralPath (Join-Path $repo 'assets\catalog\original-assets.json')
if (-not $inRepo -and -not $PSBoundParameters.ContainsKey('OutputDir')) { $OutputDir = 'retail-assets' }

function Resolve-FromRepo([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
    $base = if ($inRepo) { $repo } else { (Get-Location).Path }
    return [IO.Path]::GetFullPath((Join-Path $base $path))
}

# Installed BattleSpades folders (the installer's default sits in the game folder).
function Get-BattleSpadesInstalls([string]$gameDir) {
    $dirs = @()
    if ($gameDir) { $dirs += (Join-Path $gameDir 'BattleSpades') }
    if ($env:LOCALAPPDATA) { $dirs += (Join-Path $env:LOCALAPPDATA 'Programs\BattleSpades') }
    foreach ($root in @(${env:ProgramFiles}, ${env:ProgramFiles(x86)})) { if ($root) { $dirs += (Join-Path $root 'BattleSpades') } }
    $dirs += $PSScriptRoot
    return @($dirs | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
}

# Ace of Spades (app 224540) through the Steam registry and libraryfolders.vdf.
function Find-GameDir {
    $roots = @()
    foreach ($key in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam')) {
        $item = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($item -and $item.SteamPath) { $roots += $item.SteamPath }
        if ($item -and $item.InstallPath) { $roots += $item.InstallPath }
    }
    $libraries = @()
    foreach ($root in $roots) {
        $libraries += $root
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw -Encoding UTF8), '"path"\s+"([^"]+)"')) {
                $libraries += $match.Groups[1].Value.Replace('\\', '\')
            }
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $acf = Join-Path $library 'steamapps\appmanifest_224540.acf'
        if (-not (Test-Path -LiteralPath $acf)) { continue }
        $installdir = [regex]::Match((Get-Content -LiteralPath $acf -Raw -Encoding UTF8), '"installdir"\s+"([^"]+)"').Groups[1].Value
        if (-not $installdir) { $installdir = 'aceofspades' }
        $dir = Join-Path $library "steamapps\common\$installdir"
        if (Test-Path -LiteralPath $dir) { return $dir }
    }
    return $null
}

function Get-Sha256([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# --- inputs ---------------------------------------------------------------
if (-not $GameDir) {
    $GameDir = Find-GameDir
    if (-not $GameDir) { throw 'Ace of Spades was not found through Steam; pass -GameDir <folder that contains aos.exe>.' }
}
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
if (-not $Manifest) {
    if ($inRepo) {
        $Manifest = Join-Path $repo 'assets\catalog\original-assets.json'
    } else {
        foreach ($install in (Get-BattleSpadesInstalls $GameDir)) {
            $candidate = Join-Path $install 'asset-manifest.json'
            if (Test-Path -LiteralPath $candidate) { $Manifest = $candidate; break }
        }
        if (-not $Manifest) {
            $Manifest = Join-Path ([IO.Path]::GetTempPath()) 'battlespades-original-assets.json'
            Write-Host "Downloading the asset catalog from $CatalogUrl"
            [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -UseBasicParsing -Uri $CatalogUrl -OutFile $Manifest
        }
    }
}
$Manifest = (Resolve-Path -LiteralPath $Manifest).Path
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$') { throw "-Version '$Version' is not semver" }
if (-not $RootName) { $RootName = "BattleSpades-retail-assets-$Version" }
if ($RootName -notmatch '^[A-Za-z0-9._-]+$') { throw "-RootName may only use letters, digits, '.', '_' and '-'" }

$outputPath = Resolve-FromRepo $OutputDir
$repoPrefix = $repo.TrimEnd('\') + '\'
$allowed = (Join-Path $repo 'out').TrimEnd('\') + '\'
if ($inRepo -and ($outputPath.TrimEnd('\') + '\').StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase) -and
    -not ($outputPath.TrimEnd('\') + '\').StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Retail files must never land in the repository: choose an -OutputDir outside it or under out\ ($outputPath)."
}
$gamePrefix = $GameDir.TrimEnd('\') + '\'
if (($outputPath.TrimEnd('\') + '\').StartsWith($gamePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Do not write the pack into the game folder itself ($outputPath)."
}

$catalog = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
if ($catalog.schema -ne 1 -or -not $catalog.files) { throw "$Manifest is not an asset manifest (schema 1)" }
Write-Host "Ace of Spades: $GameDir"
Write-Host "Manifest:      $Manifest ($($catalog.file_count) files, $([math]::Round($catalog.total_bytes / 1MB)) MB)"

# --- verify every catalogued file -------------------------------------------
$entries = New-Object System.Collections.Generic.List[object]
$problems = New-Object System.Collections.Generic.List[string]
$index = 0
foreach ($file in $catalog.files) {
    $index++
    if ($index % 250 -eq 0) {
        Write-Progress -Activity 'Verifying the original game files' -Status "$index of $($catalog.files.Count)" `
            -PercentComplete ([int](100 * $index / $catalog.files.Count))
    }
    $source = Join-Path $GameDir ($file.path -replace '/', '\')
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { $problems.Add("missing:  $($file.path)"); continue }
    $item = Get-Item -LiteralPath $source
    if ($item.Length -ne [int64]$file.size) { $problems.Add("size:     $($file.path) ($($item.Length) bytes, expected $($file.size))"); continue }
    if ((Get-Sha256 $source) -ne $file.sha256.ToLowerInvariant()) { $problems.Add("modified: $($file.path)"); continue }
    $entries.Add([pscustomobject]@{ Source = $source; Entry = $file.path })
}
Write-Progress -Activity 'Verifying the original game files' -Completed
if ($problems.Count -gt 0) {
    $shown = ($problems | Select-Object -First 40) -join "`n  "
    $more = if ($problems.Count -gt 40) { "`n  ... and $($problems.Count - 40) more" } else { '' }
    throw ("$($problems.Count) file(s) do not match the retail version BattleSpades expects:`n  $shown$more`n" +
           "Run Steam > Ace of Spades > Properties > Installed Files > Verify integrity of game files, then try again.")
}

# Optional extras the importer understands (same hashes as asset_install.cpp).
$optionalFonts = @{
    'fonts/Gen_Shin_Gothic_Monospace_Bold.ttf' = '6c2e1490357ab477c7e9663244689dd365b2aaaffb9ef88a01c6bdaa99cc9250'
    'fonts/NotoSansJP-SemiBold.ttf'            = '4881d1b63b7300452b9385f69a70d00c3607ff0728c35a0c5ce99705b97c2b2a'
}
foreach ($font in $optionalFonts.Keys) {
    $source = Join-Path $GameDir ($font -replace '/', '\')
    if ((Test-Path -LiteralPath $source -PathType Leaf) -and (Get-Sha256 $source) -eq $optionalFonts[$font] -and
        -not ($entries | Where-Object { $_.Entry -eq $font })) {
        $entries.Add([pscustomobject]@{ Source = $source; Entry = $font })
    }
}
foreach ($runtime in @('steam_api.dll', 'steam_appid.txt')) {
    $source = Join-Path $GameDir $runtime
    if (Test-Path -LiteralPath $source -PathType Leaf) { $entries.Add([pscustomobject]@{ Source = $source; Entry = $runtime }) }
}

# --- write the ZIP (forward-slash entry names, one top folder) --------------
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$zipPath = Join-Path $outputPath "$RootName.zip"
$partial = "$zipPath.partial"
if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
$zip = [IO.Compression.ZipFile]::Open($partial, [IO.Compression.ZipArchiveMode]::Create)
try {
    $index = 0
    foreach ($entry in $entries) {
        $index++
        if ($index % 250 -eq 0) {
            Write-Progress -Activity "Writing $RootName.zip" -Status "$index of $($entries.Count)" `
                -PercentComplete ([int](100 * $index / $entries.Count))
        }
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $entry.Source, "$RootName/$($entry.Entry)",
                                                                       [IO.Compression.CompressionLevel]::Optimal)
    }
} finally {
    $zip.Dispose()
    Write-Progress -Activity "Writing $RootName.zip" -Completed
}
Move-Item -LiteralPath $partial -Destination $zipPath -Force

# Re-read the archive: one root, every catalogued path present.
$check = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $names = @($check.Entries | ForEach-Object { $_.FullName })
} finally { $check.Dispose() }
$roots = @($names | ForEach-Object { ($_ -split '/')[0] } | Sort-Object -Unique)
if ($roots.Count -ne 1 -or $roots[0] -ne $RootName) { throw "The ZIP does not have the single root '$RootName'." }
$present = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
foreach ($name in $names) { [void]$present.Add($name.Substring($RootName.Length + 1)) }
foreach ($file in $catalog.files) {
    if (-not $present.Contains($file.path)) { throw "The ZIP lacks $($file.path)." }
}

# --- optional: run the real importer on the extracted pack -------------------
if ($TestImport) {
    if (-not $Importer) {
        if ($inRepo) {
            $Importer = @(Get-ChildItem -Path (Join-Path $repo 'out\build') -Recurse -Filter BattleSpadesAssetInstaller.exe -ErrorAction SilentlyContinue |
                          Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1 | ForEach-Object FullName)
        }
        if (-not $Importer) {
            foreach ($install in (Get-BattleSpadesInstalls $GameDir)) {
                $candidate = Join-Path $install 'BattleSpadesAssetInstaller.exe'
                if (Test-Path -LiteralPath $candidate) { $Importer = $candidate; break }
            }
        }
        if (-not $Importer) { throw '-TestImport needs -Importer <BattleSpadesAssetInstaller.exe> (none found under out\build or an installed BattleSpades).' }
    }
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("battlespades-pack-test-" + [guid]::NewGuid().ToString('N'))
    try {
        Write-Host "Test import with $Importer"
        [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, (Join-Path $scratch 'payload'))
        # The importer copies steam_api.dll next to ITSELF (steam\win32); keep
        # retail binaries out of the build folder during this check.
        foreach ($runtime in @('steam_api.dll', 'steam_appid.txt')) {
            Remove-Item -LiteralPath (Join-Path $scratch "payload\$RootName\$runtime") -Force -ErrorAction SilentlyContinue
        }
        $report = Join-Path $scratch 'report.txt'
        $process = Start-Process -FilePath $Importer -Wait -PassThru -WindowStyle Hidden -ArgumentList @(
            '--source', "`"$(Join-Path $scratch "payload\$RootName")`"", '--manifest', "`"$Manifest`"",
            '--destination', "`"$(Join-Path $scratch 'install\assets\original')`"", '--report', "`"$report`"")
        $text = if (Test-Path -LiteralPath $report) { (Get-Content -LiteralPath $report -Raw -Encoding UTF8) } else { '' }
        if ($process.ExitCode -ne 0) { throw "The importer rejected the pack (exit $($process.ExitCode)): $text" }
        Write-Host 'Test import: ok (the importer accepted every file)'
    } finally {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# --- report ----------------------------------------------------------------
$size = (Get-Item -LiteralPath $zipPath).Length
$sha = Get-Sha256 $zipPath
[IO.File]::WriteAllText("$zipPath.sha256", "$sha  $([IO.Path]::GetFileName($zipPath))`n", (New-Object Text.UTF8Encoding $false))
Write-Host ''
Write-Host "retail_assets pack: $zipPath"
Write-Host "  files:   $($entries.Count) ($($catalog.files.Count) catalogued + $($entries.Count - $catalog.files.Count) optional)"
Write-Host "  size:    $size bytes ($([math]::Round($size / 1MB, 1)) MB)"
Write-Host "  sha256:  $sha"
Write-Host "  root:    $RootName"
Write-Host "  version: $Version"
Write-Host ''
$urls = if ($PublicUrl.Count -gt 0) { ($PublicUrl | ForEach-Object { "'$_'" }) -join ', ' } else { "'https://<your host>/$RootName.zip'" }
Write-Host 'Upload the ZIP yourself (any HTTPS host that answers Range requests), then regenerate'
Write-Host 'stable.json with your usual component packages plus these exact arguments:'
Write-Host ''
Write-Host "  ./installer/make-update-manifest.ps1 <your usual -ClientPackage/-ServerPackage/-AssetsPackage/-MirrorBaseUrls ...> ``"
Write-Host "      -RetailAssetsVersion $Version ``"
Write-Host "      -RetailAssetsSize $size ``"
Write-Host "      -RetailAssetsSha256 $sha ``"
Write-Host "      -RetailAssetsRoot $RootName ``"
Write-Host "      -RetailAssetsUrls $urls ``"
Write-Host '      -Verify'
Write-Host ''
Write-Host 'Once stable.json with retail_assets is live, BattleSpadesAssetInstaller offers'
Write-Host '"Download game assets" on Windows, macOS and Linux; check it end to end with:'
Write-Host '  BattleSpadesAssetInstaller --download --destination <scratch folder>\assets\original --report report.txt'

