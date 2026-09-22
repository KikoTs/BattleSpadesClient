[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'RelWithDebInfo',
    [string]$BuildPreset = 'native-dev',
    [string]$Destination = "$PSScriptRoot\..\dist\bin",
    [switch]$RefreshLocalization
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $root "out\build\$BuildPreset\src\$Configuration"
$executable = Join-Path $build 'BattleSpadesClient.exe'
$shaderSource = Join-Path $root 'src\render\shaders\bin'
$clientAssetSource = Join-Path $root 'assets\client'
$configSource = Join-Path $root 'config'
$destinationRoot = [System.IO.Path]::GetFullPath($Destination)

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "Built client not found: $executable"
}
if (-not (Test-Path -LiteralPath $shaderSource -PathType Container)) {
    throw "Compiled shader tree not found: $shaderSource"
}

[System.IO.Directory]::CreateDirectory($destinationRoot) | Out-Null
Copy-Item -LiteralPath $executable -Destination $destinationRoot -Force
# Keep the original Steam/shortcut executable name on the same client build.
Copy-Item -LiteralPath $executable -Destination (Join-Path $destinationRoot 'aos.exe') -Force
Get-ChildItem -LiteralPath $build -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $destinationRoot -Force
}

# OpenAL Soft 1.25's MSVC Debug mixer can trip an internal checked-iterator
# assertion ("cannot subtract incompatible span iterators") on its playback
# thread. Its public boundary is the stable OpenAL C ABI, so player-facing dev
# staging deliberately uses the Release DLL even beside a Debug client. Keep
# the Debug DLL in the CMake target directory for debugger-only investigations,
# but never put it in the folder testers launch.
if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    $stagedOpenAl = Join-Path $destinationRoot 'OpenAL32.dll'
    if ($Configuration -eq 'Debug' -and (Test-Path -LiteralPath $stagedOpenAl -PathType Leaf)) {
        $releaseOpenAl = Join-Path $root 'out\vcpkg_installed\x64-windows\bin\OpenAL32.dll'
        if (-not (Test-Path -LiteralPath $releaseOpenAl -PathType Leaf)) {
            throw "Release OpenAL runtime required for safe Debug staging: $releaseOpenAl"
        }
        Copy-Item -LiteralPath $releaseOpenAl -Destination $stagedOpenAl -Force
    }
}

$shaderDestination = Join-Path $destinationRoot 'shaders'
[System.IO.Directory]::CreateDirectory($shaderDestination) | Out-Null
Copy-Item -Path (Join-Path $shaderSource '*') `
          -Destination $shaderDestination -Recurse -Force

$clientAssetDestination = Join-Path $destinationRoot 'assets\client'
[System.IO.Directory]::CreateDirectory($clientAssetDestination) | Out-Null
Copy-Item -Path (Join-Path $clientAssetSource '*') `
          -Destination $clientAssetDestination -Recurse -Force

# These files are intentionally external runtime content. Preserve edits
# in an existing playable folder; only seed a file when staging into a clean
# destination. This is what makes UI/translation iteration survive rebuilding.
foreach ($editableName in @('ui-layout.json')) {
    $source = Join-Path $configSource $editableName
    $destination = Join-Path $destinationRoot $editableName
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Editable client configuration missing: $source"
    }
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        Copy-Item -LiteralPath $source -Destination $destination
    }
}

$localizationSource = Join-Path $root 'config\localization'
$localizationTarget = Join-Path $destinationRoot 'localization'
[System.IO.Directory]::CreateDirectory($localizationTarget) | Out-Null

# Language packs moved from one root-level localization.json file to the
# editable localization/<locale>.json directory.  A reused staging directory
# can otherwise retain the legacy catalogue indefinitely and mislead testers
# (or an older launcher) into loading stale, incorrectly encoded strings.
$legacyLocalization = Join-Path $destinationRoot 'localization.json'
if (Test-Path -LiteralPath $legacyLocalization -PathType Leaf) {
    Remove-Item -LiteralPath $legacyLocalization -Force
}

Get-ChildItem -LiteralPath $localizationSource -Filter '*.json' -File | ForEach-Object {
    $destination = Join-Path $localizationTarget $_.Name
    if ($RefreshLocalization -or
        -not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }
}

$mismatches = [System.Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath $shaderSource -Recurse -Filter '*.bin' -File | ForEach-Object {
    # Windows PowerShell 5.1 has no [IO.Path]::GetRelativePath; both paths are full.
    $relative = $_.FullName.Substring($shaderSource.TrimEnd('').Length + 1)
    $staged = Join-Path $shaderDestination $relative
    if (-not (Test-Path -LiteralPath $staged -PathType Leaf) -or
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash) {
        $mismatches.Add($relative)
    }
}
if ($mismatches.Count -ne 0) {
    throw "Staged shader verification failed: $($mismatches -join ', ')"
}

$hash = (Get-FileHash -LiteralPath (Join-Path $destinationRoot 'BattleSpadesClient.exe') `
                     -Algorithm SHA256).Hash
Write-Host "Staged BattleSpadesClient and verified shaders at $destinationRoot"
Write-Host "SHA256 $hash"
