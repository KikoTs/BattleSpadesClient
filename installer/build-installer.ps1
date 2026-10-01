<#
.SYNOPSIS
Builds BattleSpades-Setup-<version>.exe (the main build) and the component update packages.

.DESCRIPTION
1. Installs an already-built CMake tree (default out/build/native-release,
   Release) into a clean staging folder. Build it first, e.g.
   scripts/build.ps1 -Native -Profile Release, which also builds
   BattleSpadesLauncher.exe and BattleSpadesSetupHelper.exe.
2. The installer is the MAIN BUILD: client, runtime and our own assets. The
   server is an optional component players download on demand ("Enable
   hosting" / first Create Match), so it is NOT put into the installer unless
   -IncludeServerInInstaller is given. Its update package is built from
   -ServerBundle (else the newest complete ..\BattleSpades\release-dist\*
   bundle written by the server repository's scripts/package_release.py).
   Retail game files are never packaged or referenced here.
3. Compiles installer\windows\BattleSpades.iss with Inno Setup's ISCC. When
   ISCC is not installed, the pinned Tools.InnoSetup NuGet package is
   downloaded into out\tools (no admin rights, no registry changes) and its
   SHA-256 verified.
4. Unless -SkipUpdatePackage: splits the same payload into the three update
   components (BattleSpades-client-<v>-windows-x64.zip, BattleSpades-server-<v>-windows-x64.zip,
   BattleSpades-assets-<v>.zip) and, with -MirrorBaseUrls, writes <channel>.json
   through make-update-manifest.ps1.

Nothing here touches Steam or the registry.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'out/build/native-release',
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
    [string]$Configuration = 'Release',
    [string]$ServerBundle,
    [switch]$SkipServer,
    [switch]$IncludeServerInInstaller,
    [string]$OutputDir = 'out/installer',
    [string]$StageDir,
    [switch]$SkipUpdatePackage,
    [string]$Channel = 'stable',
    [string[]]$MirrorBaseUrls,
    [string]$InnoSetupVersion = '6.7.1',
    [string]$InnoSetupSha256 = '00a5a07e8a4bd3a30364cb579d744677b37adfc22e365099cb01b8bd1e651828'
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Resolve-FromRoot([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }
    return [IO.Path]::GetFullPath((Join-Path $root $path))
}

$buildPath = Resolve-FromRoot $BuildDir
$outputPath = Resolve-FromRoot $OutputDir
if (-not $StageDir) { $StageDir = Join-Path $outputPath 'stage' }
# Keep the stage path short: ISCC is not long-path aware and the asset tree is deep.
$stagePath = Resolve-FromRoot $StageDir
$payload = Join-Path $stagePath 'bin'

function Find-Iscc {
    if ($env:ISCC -and (Test-Path -LiteralPath $env:ISCC)) { return $env:ISCC }
    $onPath = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    foreach ($candidate in @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    $toolRoot = Join-Path $root "out\tools\innosetup-$InnoSetupVersion"
    $portable = Join-Path $toolRoot 'tools\ISCC.exe'
    if (Test-Path -LiteralPath $portable) { return $portable }

    Write-Host "Inno Setup not found; fetching Tools.InnoSetup $InnoSetupVersion from NuGet"
    New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
    $archive = Join-Path $toolRoot 'package.zip'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $id = 'tools.innosetup'
    Invoke-WebRequest -UseBasicParsing -OutFile $archive `
        -Uri "https://api.nuget.org/v3-flatcontainer/$id/$InnoSetupVersion/$id.$InnoSetupVersion.nupkg"
    $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if ($InnoSetupSha256 -and $actual -ne $InnoSetupSha256.ToUpperInvariant()) {
        Remove-Item -LiteralPath $archive -Force
        throw "Tools.InnoSetup $InnoSetupVersion SHA-256 mismatch: $actual"
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $toolRoot -Force
    Remove-Item -LiteralPath $archive -Force
    if (-not (Test-Path -LiteralPath $portable)) { throw "ISCC.exe missing from the NuGet package" }
    return $portable
}

# The complete bundle scripts/package_release.py writes (release-dist/BattleSpades-<v>-windows-x86_64,
# which has maps/); dist/BattleSpades is only the raw PyInstaller output. Newest wins, as in the
# client's own find_local_server_bundle().
function Find-ServerBundle {
    if ($ServerBundle) { return (Resolve-FromRoot $ServerBundle) }
    $serverRepo = Resolve-FromRoot "../BattleSpades"
    $candidates = @(Get-ChildItem -Directory -Path (Join-Path $serverRepo 'release-dist\*'), (Join-Path $serverRepo 'local-release*\*') -ErrorAction SilentlyContinue) +
                  @(Get-Item -LiteralPath (Join-Path $serverRepo 'dist\BattleSpades') -ErrorAction SilentlyContinue)
    $complete = $candidates | Where-Object {
        $_ -and (Test-Path (Join-Path $_.FullName "BattleSpades.exe")) -and
        (Test-Path (Join-Path $_.FullName "_internal")) -and (Test-Path (Join-Path $_.FullName "maps"))
    } | Sort-Object { (Get-Item (Join-Path $_.FullName "BattleSpades.exe")).LastWriteTimeUtc } -Descending
    if (-not $complete) { throw "No complete packaged server under $serverRepo; run its scripts/package_release.py or pass -ServerBundle." }
    return @($complete)[0].FullName
}

# 1. Stage the CMake install tree.
if (-not (Test-Path -LiteralPath (Join-Path $buildPath 'CMakeCache.txt'))) {
    throw "No configured CMake tree at $buildPath (run scripts/build.ps1 -Native -Profile Release first)."
}
if (Test-Path -LiteralPath $stagePath) { Remove-Item -LiteralPath $stagePath -Recurse -Force }
& cmake --install $buildPath --config $Configuration --prefix $stagePath
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

foreach ($required in @('BattleSpadesClient.exe', 'BattleSpadesLauncher.exe', 'BattleSpadesSetupHelper.exe',
                        'BattleSpadesAssetInstaller.exe', 'battlespades-version.json', 'asset-manifest.json',
                        'LICENSE')) {
    if (-not (Test-Path -LiteralPath (Join-Path $payload $required))) {
        throw "Staged payload lacks $required; build the full native tree (all targets) first."
    }
}

# 2. The server: an optional component, packaged separately.
$serverTarget = Join-Path $payload 'server'
$serverPath = $null
if (-not $SkipServer) {
    $serverPath = Find-ServerBundle
    foreach ($part in @('BattleSpades.exe', '_internal', 'maps', 'VERSION')) {
        if (-not (Test-Path -LiteralPath (Join-Path $serverPath $part))) {
            throw "Not a complete packaged server: $serverPath (missing $part). Run the server's scripts/package_release.py or pass -ServerBundle."
        }
    }
}
if ($IncludeServerInInstaller) {
    if (-not $serverPath) { throw '-IncludeServerInInstaller needs a server bundle (drop -SkipServer).' }
    if (-not (Test-Path -LiteralPath (Join-Path $serverTarget 'BattleSpades.exe'))) {
        Write-Host "Bundling server into the installer from $serverPath"
        Copy-Item -LiteralPath $serverPath -Destination $serverTarget -Recurse
    }
} elseif (Test-Path -LiteralPath $serverTarget) {
    # A CMake tree configured with AOS_BUNDLED_SERVER_ROOT installs it; the
    # main build stays small, so keep it out of the installer payload.
    Remove-Item -LiteralPath $serverTarget -Recurse -Force
}
# Retail files must never ship: refuse a payload that contains an import.
if (Test-Path -LiteralPath (Join-Path $payload 'assets\original')) {
    throw "The staged payload contains assets\original (retail files). Rebuild the stage from a clean CMake install."
}

$version = (Get-Content -LiteralPath (Join-Path $payload 'battlespades-version.json') -Raw | ConvertFrom-Json).version
if ($version -notmatch '^(\d+)\.(\d+)\.(\d+)') { throw "Unexpected version '$version'" }
$numeric = "$($Matches[1]).$($Matches[2]).$($Matches[3]).0"
Write-Host "BattleSpades $version"

# 3. Installer.
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$iscc = Find-Iscc
Write-Host "Using $iscc"
& $iscc /Qp "/DAppVersion=$version" "/DNumericVersion=$numeric" "/DStageDir=$payload" "/O$outputPath" `
    (Join-Path $root 'installer\windows\BattleSpades.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }
$setup = Join-Path $outputPath "BattleSpades-Setup-$version.exe"
$setupHash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$setup.sha256", "$setupHash  $([IO.Path]::GetFileName($setup))`n",
                        (New-Object Text.UTF8Encoding $false))
Write-Host "Installer: $setup"

# 4. Component update packages (stable.json schema 2) - client, server and
#    our own assets/client packs, each a ZIP with one top folder.
function New-ComponentZip([string]$name, [scriptblock]$fill) {
    $zipRoot = Join-Path $outputPath 'zip-root'
    if (Test-Path -LiteralPath $zipRoot) { Remove-Item -LiteralPath $zipRoot -Recurse -Force }
    $top = Join-Path $zipRoot $name
    New-Item -ItemType Directory -Force -Path $top | Out-Null
    & $fill $top
    $zip = Join-Path $outputPath "$name.zip"
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    # Windows' bsdtar writes portable forward-slash entry names (Compress-Archive
    # on PowerShell 5.1 may not).
    # tar.exe opens paths through the ANSI code page, so it only ever sees
    # relative ASCII names here (the repository path may contain Cyrillic).
    Push-Location -LiteralPath $zipRoot
    try {
        & "$env:SystemRoot\System32\tar.exe" -a -c -f "$name.zip" $name
        if ($LASTEXITCODE -ne 0) { throw "creating $zip failed" }
    } finally {
        Pop-Location
    }
    Move-Item -LiteralPath (Join-Path $zipRoot "$name.zip") -Destination $zip -Force
    Remove-Item -LiteralPath $zipRoot -Recurse -Force
    Write-Host "Component package: $zip"
    return $zip
}

if (-not $SkipUpdatePackage) {
    $versionInfo = Get-Content -LiteralPath (Join-Path $payload 'battlespades-version.json') -Raw | ConvertFrom-Json
    $assetsVersion = if ($versionInfo.components -and $versionInfo.components.assets) { $versionInfo.components.assets } else { $version }
    $packages = @{}

    # client: everything except the server and our asset packs.
    $packages.ClientPackage = New-ComponentZip "BattleSpades-client-$version-windows-x64" {
        param($top)
        Get-ChildItem -LiteralPath $payload -Force | Where-Object { $_.Name -ne 'server' -and $_.Name -ne 'assets' } |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $top -Recurse }
        $assets = Join-Path $payload 'assets'
        if (Test-Path -LiteralPath $assets) {
            New-Item -ItemType Directory -Force -Path (Join-Path $top 'assets') | Out-Null
            Get-ChildItem -LiteralPath $assets -Force | Where-Object { $_.Name -ne 'client' -and $_.Name -ne 'original' } |
                ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $top 'assets') -Recurse }
        }
    }
    if ($serverPath) {
        $serverVersion = (Get-Content -LiteralPath (Join-Path $serverPath 'VERSION') -Raw).Trim()
        $packages.ServerPackage = New-ComponentZip "BattleSpades-server-$serverVersion-windows-x64" {
            param($top)
            Get-ChildItem -LiteralPath $serverPath -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $top -Recurse }
        }
    }
    $assetsSource = Join-Path $payload 'assets\client'
    if (Test-Path -LiteralPath $assetsSource) {
        $packages.AssetsPackage = New-ComponentZip "BattleSpades-assets-$assetsVersion" {
            param($top)
            Get-ChildItem -LiteralPath $assetsSource -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $top -Recurse }
        }
    }
    if ($MirrorBaseUrls) {
        & (Join-Path $PSScriptRoot 'make-update-manifest.ps1') @packages -MirrorBaseUrls $MirrorBaseUrls -Channel $Channel `
            -Output (Join-Path $outputPath "$Channel.json")
    } else {
        Write-Host 'No -MirrorBaseUrls given: run installer/make-update-manifest.ps1 after uploading (docs/INSTALLER_AND_UPDATER.md).'
    }
}

Write-Host ''
Write-Host "Outputs in $outputPath :"
Get-ChildItem -LiteralPath $outputPath -File | ForEach-Object { Write-Host "  $($_.Name)" }
