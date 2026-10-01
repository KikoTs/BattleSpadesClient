<#
.SYNOPSIS
Runs two real Protocol 168 clients against one isolated BattleSpades server.

.DESCRIPTION
Both clients execute the deterministic movement schedule and require remote
WorldUpdate rows. The first connection must also accept the second connection's
dynamic CreatePlayer packet before any of its remote rows are applied. Every
spawned process is owned and stopped by this script.
#>
[CmdletBinding()]
param(
    [string] $ServerRoot = (Join-Path $PSScriptRoot '..\..\BattleSpades'),
    [string] $BuildRoot = (Join-Path $PSScriptRoot '..\out\build\native-dev'),
    [string] $ServerExecutable = '',
    [string[]] $ClientArguments = @(),
    [int] $Port = 32775,
    [ValidateRange(8, 60)][int] $Seconds = 12,
    [ValidateRange(0, 10)][int] $SecondClientDelaySeconds = 5,
    [string] $EvidenceDirectory = (Join-Path $PSScriptRoot '..\out\evidence\two-client-parity')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$serverRootPath = (Resolve-Path -LiteralPath $ServerRoot).Path
$client = Join-Path $BuildRoot 'src\RelWithDebInfo\aos_protocol168_movement_parity.exe'
$config = Join-Path $serverRootPath 'tools\protocol168-parity.toml'
if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
    throw "Movement parity executable was not built: $client"
}
if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
    throw "Parity server profile is missing: $config"
}

$evidence = [IO.Path]::GetFullPath($EvidenceDirectory)
[IO.Directory]::CreateDirectory($evidence) | Out-Null
$serverOut = Join-Path $evidence 'server.stdout.log'
$serverErr = Join-Path $evidence 'server.stderr.log'
$client1Out = Join-Path $evidence 'client-1.stdout.log'
$client1Err = Join-Path $evidence 'client-1.stderr.log'
$client2Out = Join-Path $evidence 'client-2.stdout.log'
$client2Err = Join-Path $evidence 'client-2.stderr.log'
$trace1 = Join-Path $evidence 'client-1.csv'
$trace2 = Join-Path $evidence 'client-2.csv'

if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) {
    throw "Parity port UDP $Port is already in use. Choose a free -Port."
}
$serverArguments = @('--config', ('"' + $config + '"'), '--port', $Port)
$serverWorkingDirectory = $serverRootPath
if ([string]::IsNullOrWhiteSpace($ServerExecutable)) {
    $serverCommand = (& py.exe -3.12 -c 'import sys; print(sys.executable)').Trim()
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $serverCommand -PathType Leaf)) {
        throw 'Python 3.12 could not be resolved'
    }
    $serverArguments = @('run_server.py') + $serverArguments
} else {
    $serverCommand = (Resolve-Path -LiteralPath $ServerExecutable).Path
    $serverWorkingDirectory = Split-Path -Parent $serverCommand
}
$server = $null
$clients = @()
try {
    $server = Start-Process -FilePath $serverCommand `
        -ArgumentList $serverArguments `
        -WorkingDirectory $serverWorkingDirectory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        if ($server.HasExited) {
            throw "Parity server exited early. stdout=$serverOut stderr=$serverErr"
        }
        $udp = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue |
            Where-Object OwningProcess -eq $server.Id
    } while (-not $udp -and [DateTime]::UtcNow -lt $deadline)
    if (-not $udp) {
        throw "Parity server did not bind UDP $Port"
    }

    $common = @('127.0.0.1', $Port, $Seconds)
    $clients += Start-Process -FilePath $client -ArgumentList ($common + @(('"' + $trace1 + '"'), '--require-peer') + $ClientArguments) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $client1Out `
        -RedirectStandardError $client1Err
    # Five seconds exercises ordinary join-in-progress. Zero deliberately
    # races both map handshakes and catches missing CreatePlayer lifecycle
    # packets before their WorldUpdate rows can create invisible soldiers.
    if ($SecondClientDelaySeconds -gt 0) {
        Start-Sleep -Seconds $SecondClientDelaySeconds
    }
    $clients += Start-Process -FilePath $client -ArgumentList ($common + @(('"' + $trace2 + '"'), '--require-peer') + $ClientArguments) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $client2Out `
        -RedirectStandardError $client2Err

    $processDeadline = [DateTime]::UtcNow.AddSeconds($Seconds + 45)
    do {
        Start-Sleep -Milliseconds 100
        $running = @($clients | Where-Object { -not $_.HasExited })
    } while ($running.Count -ne 0 -and [DateTime]::UtcNow -lt $processDeadline)
    if ($running.Count -ne 0) {
        throw 'A parity client exceeded its bounded runtime'
    }
    $clientOutputs = @($client1Out, $client2Out)
    $clientErrors = @($client1Err, $client2Err)
    for ($index = 0; $index -lt $clients.Count; ++$index) {
        $process = $clients[$index]
        $process.WaitForExit()
        $process.Refresh()
        if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
            throw "Parity client $($process.Id) failed with $($process.ExitCode)"
        }
        $stderrText = Get-Content -LiteralPath $clientErrors[$index] -Raw
        $stdoutText = Get-Content -LiteralPath $clientOutputs[$index] -Raw
        if (-not [string]::IsNullOrWhiteSpace($stderrText) -or
            $stdoutText -notmatch 'remote_world_rows=[1-9][0-9]*' -or
            $stdoutText -notmatch 'orphan_world_rows=0') {
            throw "Parity client $($process.Id) did not satisfy its peer gate"
        }
    }
}
finally {
    foreach ($process in $clients) {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        $process.Dispose()
    }
    if ($null -ne $server) {
        if (-not $server.HasExited) {
            Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
            $server.WaitForExit()
        }
        $server.Dispose()
    }
}

Get-Content -LiteralPath $client1Out
Get-Content -LiteralPath $client2Out
Write-Host "Two-client parity evidence: $evidence"
