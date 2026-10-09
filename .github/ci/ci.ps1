# Helpers for the Windows jobs of .github/workflows/build.yml. Dot-source it:
#
#   . .github/ci/ci.ps1
#   Enter-MsvcEnvironment -Arch arm64
#   Invoke-WithNetworkRetry { cmake --preset native-release }

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

# Activates the Visual Studio developer environment for the target
# architecture in this PowerShell process. Each step runs in a new process,
# so every step that compiles calls this again (it takes a few seconds).
function Enter-MsvcEnvironment {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Arch,
        [ValidatePattern('^[0-9.]+$')][string]$ToolsetVersion
    )

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $component = if ($Arch -eq 'arm64') {
        'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
    } else {
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    }
    $vs = (& $vswhere -latest -products '*' -requires $component -property installationPath | Select-Object -First 1)
    if (-not $vs) { throw "No Visual Studio installation with $component" }
    $vsArch = if ($Arch -eq 'arm64') { 'arm64' } else { 'amd64' }
    $devCmd = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'

    # VsDevCmd points VCPKG_ROOT at Visual Studio's private vcpkg; keep ours.
    $keepVcpkgRoot = $env:VCPKG_ROOT
    $toolsetArg = if ($ToolsetVersion) { "-vcvars_ver=$ToolsetVersion" } else { '' }
    $dump = & cmd.exe /d /c "call `"$devCmd`" -arch=$vsArch -host_arch=$vsArch $toolsetArg -no_logo >nul 2>nul && set"
    if ($LASTEXITCODE -ne 0) { throw "VsDevCmd failed for $Arch" }
    foreach ($line in $dump) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            [Environment]::SetEnvironmentVariable($line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
    if ($keepVcpkgRoot) { $env:VCPKG_ROOT = $keepVcpkgRoot }
    $cl = (Get-Command cl.exe -ErrorAction Stop).Source
    Write-Host "MSVC $env:VCToolsVersion ($Arch) from $vs"
    Write-Host "cl.exe: $cl"
}

# Runs a script block up to three times, retrying ONLY when its output shows
# a network failure. Any other failure stops at once.
function Invoke-WithNetworkRetry {
    param([Parameter(Mandatory = $true)][scriptblock]$Command)

    $pattern = 'Failed to download|failed to download|Could not resolve host|Connection timed out|Connection reset|Operation timed out|timed out after|SSL connect error|unexpected EOF|early EOF|RPC failed|The requested URL returned error: 5|HTTP error 5|status code 5[0-9][0-9]|curl: \([0-9]+\)|error: downloading|Unable to connect to the remote server|The operation has timed out|An existing connection was forcibly closed|No such host is known'
    $delays = @(0, 20, 60)
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        if ($attempt -gt 1) {
            Write-Host "::warning::Network failure detected; retry $attempt/3 in $($delays[$attempt - 1])s"
            Start-Sleep -Seconds $delays[$attempt - 1]
        }
        $global:LASTEXITCODE = 0
        $output = [Collections.Generic.List[string]]::new()
        $failed = $false
        try {
            & $Command 2>&1 | ForEach-Object { $text = "$_"; $output.Add($text); Write-Host $text }
            if ($LASTEXITCODE -ne 0) { $failed = $true }
        } catch {
            $output.Add("$_"); Write-Host "$_"
            $failed = $true
        }
        if (-not $failed) { return }
        if (-not (($output -join "`n") -match $pattern)) {
            throw "Command failed (exit $LASTEXITCODE) without a network signature; not retrying."
        }
    }
    throw 'Command still failing after 3 attempts.'
}

# Fails unless the PE file targets the expected machine.
function Assert-PeMachine {
    param([string]$Path, [ValidateSet('x64', 'arm64', 'x86')][string]$Arch)
    $expected = @{ x64 = 0x8664; arm64 = 0xAA64; x86 = 0x014C }[$Arch]
    $bytes = [IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne $expected) {
        throw "$Path is machine 0x$($machine.ToString('X4')), expected $Arch"
    }
}
