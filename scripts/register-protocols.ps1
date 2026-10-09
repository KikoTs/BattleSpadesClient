param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [switch]$IncludeClassic
)
$ErrorActionPreference = 'Stop'
$clientPath = (Resolve-Path -LiteralPath $Executable).Path
if (-not (Test-Path -LiteralPath $clientPath -PathType Leaf) -or
    [IO.Path]::GetExtension($clientPath) -ne '.exe' -or $clientPath.Contains('"')) {
    throw 'Choose the installed BattleSpadesClient.exe.'
}
$schemes = @('aosbb')
if ($IncludeClassic) { $schemes += 'aos' }
foreach ($scheme in $schemes) {
    $key = 'HKCU:\Software\Classes\' + $scheme
    New-Item -Path ($key + '\shell\open\command') -Force | Out-Null
    Set-Item -LiteralPath $key -Value ('URL:' + $scheme + ' join link')
    New-ItemProperty -LiteralPath $key -Name 'URL Protocol' -PropertyType String -Value '' -Force | Out-Null
    Set-Item -LiteralPath ($key + '\shell\open\command') -Value ('"' + $clientPath + '" --join-url "%1"')
}
Write-Output ('Registered for the current user: ' + ($schemes -join ', '))
