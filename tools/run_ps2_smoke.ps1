param(
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe',
    [string]$DataRoot = (Join-Path $PSScriptRoot '..\build\PCSX2-diagnostic')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$elf = Join-Path $projectRoot 'build\PS2\oot_ps2_smoke.elf'
$log = Join-Path $projectRoot 'build\PS2\pcsx2-smoke.log'
if (!(Test-Path -LiteralPath $elf)) { throw 'Build the smoke ELF first.' }
if (!(Test-Path -LiteralPath (Join-Path $DataRoot 'PCSX2\inis\PCSX2.ini'))) {
    throw 'Configure the isolated PCSX2 data directory, including your BIOS, first.'
}
$process = Start-Process -FilePath $Emulator -ArgumentList @(
    '-batch', '-nofullscreen', '-datapath', ('"' + $DataRoot + '"'),
    '-logfile', ('"' + $log + '"'), '-elf', ('"' + $elf + '"')
) -WindowStyle Hidden -PassThru
$process.Id | Set-Content (Join-Path $projectRoot 'build\PS2\pcsx2-diagnostic.pid')
Write-Output "Diagnostic PCSX2 PID: $($process.Id)"
Write-Output "Log: $log"
