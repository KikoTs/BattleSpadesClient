<#
.SYNOPSIS
Writes battlespades-release.json (and a CPack-style .sha256) for one Windows
update package, ready to upload as GitHub release assets.

.EXAMPLE
./installer/make-release-manifest.ps1 -Package out/installer/BattleSpadesClient-0.3.0-beta.1-Windows-AMD64.zip

The package is the CPack ZIP layout: <PackageBase>/bin/... . The launcher maps
the "root" folder onto the install folder. Format: docs/INSTALLER_AND_UPDATER.md.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [string]$Version,
    [string]$Root,
    [string]$Channel = 'beta',
    [string]$NotesUrl,
    [string]$Repository = 'KikoTs/BattleSpadesClient',
    [string[]]$Preserve = @('ui-layout.json'),
    [string[]]$MirrorDirectories = @('shaders', 'server/_internal'),
    [string[]]$Remove = @(),
    [string]$Output
)

$ErrorActionPreference = 'Stop'
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$name = [IO.Path]::GetFileName($packagePath)
$base = [IO.Path]::GetFileNameWithoutExtension($packagePath)

if (-not $Version) {
    if ($name -notmatch '^BattleSpadesClient-(.+)-Windows-(AMD64|x64)\.zip$') {
        throw "Cannot infer the version from '$name'; pass -Version."
    }
    $Version = $Matches[1]
}
if ($Version -notmatch '^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?$') {
    throw "Version '$Version' is not semver."
}
if (-not $Root) { $Root = "$base/bin" }
if (-not $NotesUrl) { $NotesUrl = "https://github.com/$Repository/releases/tag/v$Version" }
if (-not $Output) { $Output = Join-Path ([IO.Path]::GetDirectoryName($packagePath)) 'battlespades-release.json' }

$hash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $packagePath).Length

$manifest = [ordered]@{
    schema             = 1
    product            = 'BattleSpadesClient'
    version            = $Version
    channel            = $Channel
    notes_url          = $NotesUrl
    platforms          = [ordered]@{
        'windows-x64' = [ordered]@{
            package = $name
            size    = $size
            sha256  = $hash
            root    = $Root
        }
    }
    preserve           = @($Preserve)
    mirror_directories = @($MirrorDirectories)
    remove             = @($Remove)
}
$json = $manifest | ConvertTo-Json -Depth 6
# UTF-8 without BOM, LF endings.
[IO.File]::WriteAllText($Output, ($json -replace "`r`n", "`n") + "`n", (New-Object Text.UTF8Encoding $false))
[IO.File]::WriteAllText("$packagePath.sha256", "$hash  $name`n", (New-Object Text.UTF8Encoding $false))

Write-Host "Wrote $Output"
Write-Host "Wrote $packagePath.sha256"
Write-Host "SHA256 $hash  ($size bytes)"
