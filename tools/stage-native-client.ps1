[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'RelWithDebInfo',
    [string]$BuildPreset = 'native-dev',
    [string]$Destination = "$PSScriptRoot\..\dist\bin"
)

$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $root "out\build\$BuildPreset\src\$Configuration"
$executable = Join-Path $build 'BattleSpadesClient.exe'
$shaderSource = Join-Path $root 'src\render\shaders\bin'
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
Get-ChildItem -LiteralPath $build -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $destinationRoot -Force
}

$shaderDestination = Join-Path $destinationRoot 'shaders'
[System.IO.Directory]::CreateDirectory($shaderDestination) | Out-Null
Copy-Item -Path (Join-Path $shaderSource '*') `
          -Destination $shaderDestination -Recurse -Force

# These two files are intentionally external runtime content. Preserve edits
# in an existing playable folder; only seed a file when staging into a clean
# destination. This is what makes UI/translation iteration survive rebuilding.
foreach ($editableName in @('ui-layout.json', 'localization.json')) {
    $source = Join-Path $configSource $editableName
    $destination = Join-Path $destinationRoot $editableName
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Editable client configuration missing: $source"
    }
    if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
        Copy-Item -LiteralPath $source -Destination $destination
    }
}

$mismatches = [System.Collections.Generic.List[string]]::new()
Get-ChildItem -LiteralPath $shaderSource -Recurse -Filter '*.bin' -File | ForEach-Object {
    $relative = [System.IO.Path]::GetRelativePath($shaderSource, $_.FullName)
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
