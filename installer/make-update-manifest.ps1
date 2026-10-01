<#
.SYNOPSIS
Writes the update manifest (stable.json, schema 2) for the client, server and
assets component packages, with every mirror URL, size and SHA-256.

.DESCRIPTION
Each package is a ZIP with one top folder (build-installer.ps1 writes them as
BattleSpades-client-<v>-windows-x64.zip, BattleSpades-server-<v>-windows-x64.zip
and BattleSpades-assets-<v>.zip). Versions are read from the file names unless
given. Mirror URLs are built from -MirrorBaseUrls in the order given (the
launcher tries them in that order):

  * a base without placeholders gets "/<file name>" appended;
  * {file}, {component}, {version} and {tag} are substituted otherwise.

Example (GitHub Releases first, Cloudflare R2 second):

  ./installer/make-update-manifest.ps1 `
     -ClientPackage out/installer/BattleSpades-client-0.3.0-windows-x64.zip `
     -ServerPackage out/installer/BattleSpades-server-0.3.0-windows-x64.zip `
     -AssetsPackage out/installer/BattleSpades-assets-0.3.0.zip `
     -MirrorBaseUrls 'https://github.com/KikoTs/BattleSpadesClient/releases/download/{tag}', 'https://updates.aosplay.net/{component}' `
     -HostingServer '>=0.3.0' -Protocol 168 -Verify

Components roll out independently. -HostingServer is the minimum server the
new client needs for hosting (Create Match / Map Creator); it never blocks
playing. -ServerProtocol may differ from -Protocol (the client's).

retail_assets (the original game files, optional "Download game assets") is
hosted by Kiril himself and NEVER packaged by our scripts: pass only its
metadata (-RetailAssetsVersion, -RetailAssetsUrls, -RetailAssetsSize,
-RetailAssetsSha256 and optionally -RetailAssetsRoot). Without them the
manifest has no retail_assets and players can only import from a folder.

-Verify sends a HEAD request to every URL and fails unless each answers 200
with the package's exact size (run it after uploading).
#>
[CmdletBinding()]
param(
    [string]$ClientPackage,
    [string]$ServerPackage,
    [string]$AssetsPackage,
    [string]$ClientVersion,
    [string]$ServerVersion,
    [string]$AssetsVersion,
    [Parameter(Mandatory = $true)][string[]]$MirrorBaseUrls,
    [string]$Tag,
    [string]$Channel = 'stable',
    [string]$NotesUrl,
    [switch]$Required,
    [string[]]$RequiredComponents = @(),
    [int]$Protocol = 0,
    [int]$ServerProtocol = 0,
    [string]$HostingServer,
    [string]$RetailAssetsVersion,
    [string[]]$RetailAssetsUrls,
    [long]$RetailAssetsSize = 0,
    [string]$RetailAssetsSha256,
    [string]$RetailAssetsRoot = '',
    [string[]]$ClientPreserve = @('ui-layout.json'),
    [string[]]$ClientMirror = @('shaders'),
    [string[]]$ClientRemove = @(),
    [string[]]$ServerPreserve = @('config.toml', 'bans.json', 'fleet.toml', 'logs/**'),
    [string[]]$ServerMirror = @('_internal'),
    [string[]]$ServerRemove = @(),
    [string[]]$AssetsMirror,
    [string]$Output = 'out/installer/stable.json',
    [switch]$Verify
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$semver = '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$'

function Get-ZipLayout([string]$path) {
    $zip = [IO.Compression.ZipFile]::OpenRead($path)
    try {
        $names = $zip.Entries | ForEach-Object { $_.FullName -replace '\\', '/' }
    } finally { $zip.Dispose() }
    $tops = @($names | ForEach-Object { ($_ -split '/')[0] } | Sort-Object -Unique)
    $root = ''
    if ($tops.Count -eq 1 -and ($names | Where-Object { $_ -like "$($tops[0])/*" }).Count -eq $names.Count) {
        $root = $tops[0]
    }
    $prefix = if ($root) { "$root/" } else { '' }
    $folders = @($names | Where-Object { $_.StartsWith($prefix) -and $_.Substring($prefix.Length).Contains('/') } |
                 ForEach-Object { ($_.Substring($prefix.Length) -split '/')[0] } | Sort-Object -Unique)
    return @{ Root = $root; Folders = $folders }
}

function New-Component([string]$name, [string]$path, [string]$version, [string]$pattern) {
    $resolved = (Resolve-Path -LiteralPath $path).Path
    $file = [IO.Path]::GetFileName($resolved)
    if (-not $version) {
        if ($file -notmatch $pattern) { throw "Cannot infer the $name version from '$file'; pass -$((Get-Culture).TextInfo.ToTitleCase($name))Version." }
        $version = $Matches[1]
    }
    if ($version -notmatch $semver) { throw "$name version '$version' is not semver" }
    $layout = Get-ZipLayout $resolved
    $tagValue = if ($Tag) { $Tag } else { "v$version" }
    $urls = foreach ($base in $MirrorBaseUrls) {
        if ($base -notmatch '^https://') { throw "Mirror base must be https: $base" }
        if ($base -match '\{(file|component|version|tag)\}') {
            $url = $base.Replace('{component}', $name).Replace('{version}', $version).Replace('{tag}', $tagValue)
            if ($url.Contains('{file}')) { $url.Replace('{file}', $file) } else { $url.TrimEnd('/') + "/$file" }
        } else {
            $base.TrimEnd('/') + "/$file"
        }
    }
    $entry = [ordered]@{
        version  = $version
        package  = $file
        size     = (Get-Item -LiteralPath $resolved).Length
        sha256   = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
        root     = $layout.Root
        urls     = @($urls)
    }
    if ($Required -or $RequiredComponents -contains $name) { $entry.required = $true }
    return @{ Entry = $entry; Folders = $layout.Folders; Path = $resolved }
}

$components = [ordered]@{}
$files = @{}
if ($ClientPackage) {
    $c = New-Component 'client' $ClientPackage $ClientVersion '^BattleSpades-client-(.+)-windows-x64\.zip$'
    if ($Protocol -gt 0) { $c.Entry.protocol = $Protocol }
    if ($HostingServer) { $c.Entry.hosting_server = $HostingServer }
    $c.Entry.preserve = @($ClientPreserve); $c.Entry.mirror_directories = @($ClientMirror); $c.Entry.remove = @($ClientRemove)
    $components.client = $c.Entry; $files.client = $c.Path
}
if ($ServerPackage) {
    $s = New-Component 'server' $ServerPackage $ServerVersion '^BattleSpades-server-(.+)-windows-x64\.zip$'
    $s.Entry.target = 'server'
    $serverProtocolValue = if ($ServerProtocol -gt 0) { $ServerProtocol } else { $Protocol }
    if ($serverProtocolValue -gt 0) { $s.Entry.protocol = $serverProtocolValue }
    $s.Entry.preserve = @($ServerPreserve); $s.Entry.mirror_directories = @($ServerMirror); $s.Entry.remove = @($ServerRemove)
    $components.server = $s.Entry; $files.server = $s.Path
}
if ($AssetsPackage) {
    $a = New-Component 'assets' $AssetsPackage $AssetsVersion '^BattleSpades-assets-(.+)\.zip$'
    $a.Entry.target = 'assets/client'
    # Our packs are replaced as a whole: every top-level folder is mirrored.
    $a.Entry.mirror_directories = if ($PSBoundParameters.ContainsKey('AssetsMirror')) { @($AssetsMirror) } else { @($a.Folders) }
    $components.assets = $a.Entry; $files.assets = $a.Path
}
if ($RetailAssetsVersion) {
    if ($RetailAssetsVersion -notmatch $semver) { throw "retail_assets version '$RetailAssetsVersion' is not semver" }
    if (-not $RetailAssetsUrls -or $RetailAssetsSize -le 0 -or $RetailAssetsSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'retail_assets needs -RetailAssetsUrls, -RetailAssetsSize and a 64-digit -RetailAssetsSha256.'
    }
    foreach ($url in $RetailAssetsUrls) { if ($url -notmatch '^https://') { throw "retail_assets URL must be https: $url" } }
    $retailFile = [IO.Path]::GetFileName(([Uri]$RetailAssetsUrls[0]).AbsolutePath)
    $retail = [ordered]@{
        version = $RetailAssetsVersion
        package = $retailFile
        size    = $RetailAssetsSize
        sha256  = $RetailAssetsSha256.ToLowerInvariant()
        root    = $RetailAssetsRoot
        target  = 'assets/original'
        urls    = @($RetailAssetsUrls)
    }
    if ($RequiredComponents -contains 'retail_assets') { $retail.required = $true }
    $components.retail_assets = $retail
}
if ($components.Count -eq 0) { throw 'Pass at least one of -ClientPackage, -ServerPackage, -AssetsPackage, -RetailAssetsVersion.' }

$manifest = [ordered]@{
    schema     = 2
    product    = 'BattleSpades'
    channel    = $Channel
    published  = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
}
if ($NotesUrl) { $manifest.notes_url = $NotesUrl }
if ($Required) { $manifest.required = $true }
$manifest.components = $components

if ($Verify) {
    $failures = @()
    foreach ($name in $components.Keys) {
        foreach ($url in $components[$name].urls) {
            try {
                $response = Invoke-WebRequest -UseBasicParsing -Method Head -Uri $url -MaximumRedirection 5
                $length = [int64]($response.Headers['Content-Length'] | Select-Object -First 1)
                if ($length -ne $components[$name].size) { $failures += "$url : size $length, expected $($components[$name].size)" }
                else { Write-Host "ok   $url" }
            } catch {
                $failures += "$url : $($_.Exception.Message)"
            }
        }
    }
    if ($failures) { throw "Mirror verification failed:`n  " + ($failures -join "`n  ") }
}

$outputPath = if ([IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path (Get-Location) $Output }
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
$json = $manifest | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($outputPath, ($json -replace "`r`n", "`n") + "`n", (New-Object Text.UTF8Encoding $false))
Write-Host "Wrote $outputPath"
foreach ($name in $components.Keys) {
    Write-Host ("  {0,-7} {1,-12} {2,12:N0} bytes  {3}" -f $name, $components[$name].version, $components[$name].size, $components[$name].sha256)
}
