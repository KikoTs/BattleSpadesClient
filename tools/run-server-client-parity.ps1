[CmdletBinding()]
param(
    [string]$ServerRoot = "G:\AoSRevival\BattleSpades",
    [string]$BuildRoot = "G:\AoSRevival\BattleSpadesClient\out\build\native-dev",
    [int]$Port = 32768,
    [int]$Seconds = 12,
    [ValidateRange(0, 17)]
    [int]$Class = 1,
    [string]$TracePath = "G:\AoSRevival\BattleSpadesClient\out\movement-parity.csv",
    # Optional probe flags, including bounded uplink/downlink delay and jitter.
    [string[]]$ClientArguments = @()
)

$ErrorActionPreference = "Stop"
$serverRoot = (Resolve-Path -LiteralPath $ServerRoot).Path
$client = Join-Path $BuildRoot "src\RelWithDebInfo\aos_protocol168_movement_parity.exe"
$config = Join-Path $serverRoot "tools\protocol168-parity.toml"
if (-not (Test-Path -LiteralPath $client)) {
    throw "Movement parity executable was not built: $client"
}
if (-not (Test-Path -LiteralPath $config)) {
    throw "Parity server profile is missing: $config"
}

$traceDirectory = Split-Path -Parent $TracePath
if ($traceDirectory) {
    New-Item -ItemType Directory -Path $traceDirectory -Force | Out-Null
}
$stdout = Join-Path $env:TEMP "battlespades-parity-server-$Port.stdout.log"
$stderr = Join-Path $env:TEMP "battlespades-parity-server-$Port.stderr.log"
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) {
    throw "Parity port UDP $Port is already in use. Choose a free -Port."
}
# Own the actual server process, not py.exe's short-lived launcher.
$python = (& py.exe -3.12 -c 'import sys; print(sys.executable)').Trim()
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $python -PathType Leaf)) {
    throw 'Python 3.12 could not be resolved'
}
$arguments = @(
    "run_server.py",
    "--config", ('"' + $config + '"'),
    "--port", $Port
)

$server = Start-Process -FilePath $python -ArgumentList $arguments `
    -WorkingDirectory $serverRoot -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        if ($server.HasExited) {
            throw "Parity server exited early. stdout=$stdout stderr=$stderr"
        }
        $udp = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue |
            Where-Object OwningProcess -eq $server.Id
    } while (-not $udp -and [DateTime]::UtcNow -lt $deadline)
    if (-not $udp) {
        throw "Parity server did not bind UDP $Port. stdout=$stdout stderr=$stderr"
    }

    & $client "127.0.0.1" $Port $Seconds $TracePath "--class=$Class" @ClientArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Server/client parity gate failed with exit code $LASTEXITCODE"
    }
}
finally {
    if (-not $server.HasExited) {
        Stop-Process -Id $server.Id -Force
        $server.WaitForExit()
    }
}

Write-Host "Movement trace: $TracePath"
Write-Host "Server stdout: $stdout"
Write-Host "Server stderr: $stderr"
