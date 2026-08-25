<#
.SYNOPSIS
Measures a native client capture against a same-sized retail golden.

.DESCRIPTION
Uses ffmpeg's RGB SSIM implementation so transparent desktop-capture padding
does not distort the score. An optional absolute-difference PNG is written for
visual inspection. The command exits with code 2 when the measured SSIM is
below MinimumSsim, making it suitable for a local acceptance gate.

.EXAMPLE
.\tools\compare-client-captures.ps1 `
    -Reference .\out\evidence\retail-main-menu-client-800x600.png `
    -Candidate .\out\evidence\native-main-menu-800x600.png `
    -DifferencePath .\out\evidence\native-main-menu-800x600-difference.png `
    -MinimumSsim 0.98
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Reference,

    [Parameter(Mandatory = $true)]
    [string] $Candidate,

    [string] $DifferencePath = '',

    [ValidateRange(0.0, 1.0)]
    [double] $MinimumSsim = 0.0
)

$ErrorActionPreference = 'Stop'

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($null -eq $ffmpeg) {
    $fallback = 'C:\ffmpeg\ffmpeg.exe'
    if (-not (Test-Path -LiteralPath $fallback -PathType Leaf)) {
        throw 'ffmpeg is required for capture comparison and was not found.'
    }
    $ffmpegPath = $fallback
} else {
    $ffmpegPath = $ffmpeg.Source
}

$referencePath = (Resolve-Path -LiteralPath $Reference).Path
$candidatePath = (Resolve-Path -LiteralPath $Candidate).Path

if ($DifferencePath) {
    $resolvedDifference = [IO.Path]::GetFullPath($DifferencePath)
    $differenceDirectory = Split-Path -Parent $resolvedDifference
    if ($differenceDirectory) {
        [IO.Directory]::CreateDirectory($differenceDirectory) | Out-Null
    }

    $differenceArguments = @(
        '-hide_banner', '-loglevel', 'error', '-nostdin',
        '-i', $referencePath,
        '-i', $candidatePath,
        '-filter_complex',
        '[0:v]format=rgb24[reference];[1:v]format=rgb24[candidate];[reference][candidate]blend=all_mode=difference,format=rgb24[out]',
        '-map', '[out]', '-frames:v', '1', '-y', $resolvedDifference
    )
    & $ffmpegPath @differenceArguments
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg could not create the difference image (exit $LASTEXITCODE)."
    }
}

$ssimArguments = @(
    '-hide_banner', '-nostdin',
    '-i', $referencePath,
    '-i', $candidatePath,
    '-filter_complex',
    '[0:v]format=rgb24[reference];[1:v]format=rgb24[candidate];[reference][candidate]ssim',
    '-f', 'null', '-'
)
$savedErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    # Windows PowerShell surfaces every redirected native stderr line as a
    # NativeCommandError. ffmpeg writes normal progress/SSIM diagnostics there,
    # so capture it under Continue and judge the native exit code ourselves.
    $diagnostics = (& $ffmpegPath @ssimArguments 2>&1 | Out-String)
    $ffmpegExitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $savedErrorPreference
}
if ($ffmpegExitCode -ne 0) {
    throw "ffmpeg could not compare the captures (exit $LASTEXITCODE).`n$diagnostics"
}

$match = [regex]::Match($diagnostics, 'All:(?<score>[0-9]+(?:\.[0-9]+)?)')
if (-not $match.Success) {
    throw "ffmpeg returned no aggregate SSIM score.`n$diagnostics"
}

$score = [double]::Parse(
    $match.Groups['score'].Value,
    [Globalization.CultureInfo]::InvariantCulture
)
$result = [pscustomobject]@{
    Reference = $referencePath
    Candidate = $candidatePath
    Difference = if ($DifferencePath) { $resolvedDifference } else { $null }
    Ssim = $score
    MinimumSsim = $MinimumSsim
    Passed = $score -ge $MinimumSsim
}
$result

if (-not $result.Passed) {
    Write-Error (
        'Capture SSIM {0:F6} is below the required {1:F6}.' -f `
            $score, $MinimumSsim
    ) -ErrorAction Continue
    exit 2
}
