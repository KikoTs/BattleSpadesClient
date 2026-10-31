[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $SourceDirectory,
    [string] $AssetRoot = (Join-Path $PSScriptRoot '../assets/original')
)

$ErrorActionPreference = 'Stop'
$sourceRoot = (Resolve-Path -LiteralPath $SourceDirectory).Path
$destinationRoot = [System.IO.Path]::GetFullPath($AssetRoot)
$catalog = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot '../assets/catalog/menu-backgrounds.json') | ConvertFrom-Json
# Verify the complete collection before copying. Original PNG bytes stay unchanged.
foreach ($entry in $catalog) {
    $sourceFile = Join-Path $sourceRoot $entry.source
    if ((Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -ne $entry.sha256) {
        throw "Background checksum mismatch: $($entry.name)"
    }
}
foreach ($entry in $catalog) {
    $destination = Join-Path $destinationRoot $entry.asset
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceRoot $entry.source) -Destination $destination -Force
}
Write-Output "Imported $($catalog.Count) verified menu backgrounds into $destinationRoot"
