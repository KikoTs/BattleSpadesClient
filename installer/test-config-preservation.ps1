<# Exercise the production [Files] rules without Steam, shortcuts or registry changes. #>
[CmdletBinding()]
param([string]$Iscc)
$ErrorActionPreference = 'Stop'
if (-not $Iscc) {
    $onPath = Get-Command iscc.exe -ErrorAction SilentlyContinue
    $Iscc = @($env:ISCC, $onPath.Source,
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "$PSScriptRoot/../out/tools/innosetup-6.7.1/tools/ISCC.exe") |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if (-not $Iscc) { throw 'Build the installer first or pass -Iscc' }
$root = Join-Path ([IO.Path]::GetTempPath()) ('battlespades-installer-qa-' + [guid]::NewGuid().ToString('N'))
$stage = Join-Path $root 'stage'
$upgrade = Join-Path $root 'upgrade'
$fresh = Join-Path $root 'fresh'
New-Item -ItemType Directory -Force "$stage/server/logs", "$upgrade/server/logs", $fresh | Out-Null
foreach ($file in 'config.toml', 'fleet.toml', 'bans.json') {
    Set-Content -LiteralPath "$stage/server/$file" -Value "default $file"
    Set-Content -LiteralPath "$upgrade/server/$file" -Value "custom $file"
}
Set-Content -LiteralPath "$stage/ui-layout.json" -Value 'default layout'
Set-Content -LiteralPath "$upgrade/ui-layout.json" -Value 'custom layout'
Set-Content -LiteralPath "$stage/server/logs/session.log" -Value 'packaged log must not ship'
Set-Content -LiteralPath "$upgrade/server/logs/session.log" -Value 'existing log'
Set-Content -LiteralPath "$stage/payload.dat" -Value 'new executable payload'
Set-Content -LiteralPath "$upgrade/payload.dat" -Value 'old executable payload'
Set-Content -LiteralPath "$stage/helper.exe" -Value 'unused extraction fixture'
$production = Get-Content -LiteralPath "$PSScriptRoot/windows/BattleSpades.iss" -Raw
$files = [regex]::Match($production, '(?s)\[Files\]\r?\n(.*?)\r?\n\[Icons\]').Groups[1].Value
if (-not $files) { throw 'Production installer [Files] section was not found' }
$harness = @"
#define StageDir "$stage"
#define HelperExe "helper.exe"
[Setup]
AppName=BattleSpades installer file preservation QA
AppVersion=1
DefaultDirName=$upgrade
PrivilegesRequired=lowest
Uninstallable=no
CreateUninstallRegKey=no
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir=$root
OutputBaseFilename=preservation-test
Compression=none
[Files]
$files
"@
$source = Join-Path $root 'preservation-test.iss'
Set-Content -LiteralPath $source -Value $harness
& $Iscc /Q $source
if ($LASTEXITCODE -ne 0) { throw 'Installer preservation harness failed to compile' }
foreach ($destination in @($upgrade, $fresh)) {
    $process = Start-Process -FilePath "$root/preservation-test.exe" -WindowStyle Hidden -Wait -PassThru `
        -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$destination`"", "/LOG=`"$destination/install.log`"")
    if ($process.ExitCode -ne 0) { throw "Installer QA failed: $($process.ExitCode) ($destination)" }
    if ((Get-Content -LiteralPath "$destination/payload.dat" -Raw).Trim() -ne 'new executable payload') {
        throw 'The normal payload was not installed'
    }
    $kind = if ($destination -eq $upgrade) { 'custom' } else { 'default' }
    foreach ($file in 'config.toml', 'fleet.toml', 'bans.json') {
        if ((Get-Content -LiteralPath "$destination/server/$file" -Raw).Trim() -ne "$kind $file") {
            throw "Server configuration was overwritten or missing: $file"
        }
    }
    if ((Get-Content -LiteralPath "$destination/ui-layout.json" -Raw).Trim() -ne "$kind layout") {
        throw 'The HUD layout was overwritten or missing'
    }
}
if ((Get-Content -LiteralPath "$upgrade/server/logs/session.log" -Raw).Trim() -ne 'existing log' -or
    (Test-Path -LiteralPath "$fresh/server/logs/session.log")) { throw 'Packaged logs overwrote user data' }
Write-Output "PASS: fresh defaults, preserved server settings/bans/logs and HUD layout, updated binaries. QA files: $root"
