param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release")]
    [string]$Configuration = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

if (-not $IsWindows -and $PSVersionTable.PSEdition -eq "Core") {
    throw "The retail steam_api.dll bridge is Windows-only."
}

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$vswhere = Join-Path ${env:ProgramFiles(x86)} `
    "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found."
}

$visualStudio = (& $vswhere `
    -latest `
    -products "*" `
    -version "[17.0,)" `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath).Trim()
if (-not $visualStudio) {
    throw "Visual Studio 2022 or newer with the x86 C++ tools is required."
}

$devCommand = Join-Path $visualStudio "Common7\Tools\VsDevCmd.bat"
$environmentDump = & cmd.exe /d /c `
    "call `"$devCommand`" -arch=x86 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) {
    throw "Failed to activate the Visual Studio x86 C++ environment."
}
foreach ($line in $environmentDump) {
    $separator = $line.IndexOf("=")
    if ($separator -gt 0) {
        [Environment]::SetEnvironmentVariable(
            $line.Substring(0, $separator),
            $line.Substring($separator + 1),
            "Process"
        )
    }
}

$ninja = Get-ChildItem `
    (Join-Path $visualStudio "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja") `
    -Recurse `
    -Filter ninja.exe | Select-Object -First 1
if (-not $ninja) {
    throw "Visual Studio's Ninja executable was not found."
}
$env:PATH = "$($ninja.DirectoryName);$env:PATH"

$buildRoot = Join-Path $projectRoot "out\build\steam-bridge-x86"
& cmake `
    -S (Join-Path $projectRoot "steam_bridge") `
    -B $buildRoot `
    -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration"
if ($LASTEXITCODE -ne 0) {
    throw "Steam bridge CMake configuration failed."
}
& cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Steam bridge build failed."
}

$source = Join-Path $buildRoot "BattleSpadesSteamBridge32.exe"
$destinationRoot = Join-Path $projectRoot "out\steam-bridge"
New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
$destination = Join-Path $destinationRoot "BattleSpadesSteamBridge32.exe"
Copy-Item -LiteralPath $source -Destination $destination -Force

$bytes = [IO.File]::ReadAllBytes($destination)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
if ($machine -ne 0x014C) {
    throw "Steam bridge is not an x86 PE (machine=0x$($machine.ToString('X4')))."
}
Write-Host "Built x86 Steam bridge: $destination"
