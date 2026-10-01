param(
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$gameRoot = Join-Path $projectRoot 'build\PS2-ntsc12'
$elf = Join-Path $gameRoot 'oot_ps2.elf'
$dataRoot = Join-Path $projectRoot 'build\PCSX2-diagnostic'
$pidFile = Join-Path $gameRoot 'pcsx2-game.pid'
$log = Join-Path $gameRoot 'pcsx2-game.log'
if (!(Test-Path -LiteralPath $elf)) { throw 'Build the game ELF first.' }
if (!(Test-Path -LiteralPath (Join-Path $gameRoot 'oot.z64'))) { throw 'Run prepare_ps2_rom.py first.' }
$config = Join-Path $dataRoot 'PCSX2\inis\PCSX2.ini'
if (!(Test-Path -LiteralPath $config)) { throw 'The isolated PCSX2 configuration is missing.' }
if ([IO.File]::ReadAllText($config) -notmatch '(?m)^HostFs = true\s*$') {
    throw 'Enable HostFs in the isolated PCSX2 configuration to load the data image.'
}
if (Test-Path -LiteralPath $pidFile) {
    $oldPid = [int](Get-Content -LiteralPath $pidFile)
    $oldProcess = Get-CimInstance Win32_Process -Filter "ProcessId=$oldPid"
    if ($oldProcess -and $oldProcess.Name -eq 'pcsx2-qt.exe' -and $oldProcess.CommandLine -and $oldProcess.CommandLine.Contains($elf)) {
        Stop-Process -Id $oldPid
    }
}
if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination ($log + '.previous') -Force }
$process = Start-Process -FilePath $Emulator -ArgumentList @(
    '-batch', '-nofullscreen', '-datapath', ('"' + $dataRoot + '"'),
    '-logfile', ('"' + $log + '"'), '-elf', ('"' + $elf + '"')
) -WindowStyle Hidden -PassThru
$process.Id | Set-Content -LiteralPath $pidFile
Write-Output "Game PCSX2 PID: $($process.Id)"
Write-Output "Log: $log"
