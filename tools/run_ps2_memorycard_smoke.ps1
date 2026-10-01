param([switch]$InitializeNewCard)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$elf = Join-Path $root 'build\PS2\oot_ps2_memorycard_smoke.elf'
$dataRoot = Join-Path $root 'build\PCSX2-memorycard'
$configRoot = Join-Path $dataRoot 'PCSX2'
$card = Join-Path $configRoot 'memcards\oot-port.ps2'
$token = Join-Path $root 'build\PS2\ps2-memorycard-format.allow'
$pidFile = Join-Path $root 'build\PS2\pcsx2-memorycard.pid'
$log = Join-Path $root 'build\PS2\pcsx2-memorycard.log'
if (!(Test-Path -LiteralPath $elf)) { throw 'Build the memory-card diagnostic first.' }
if ($InitializeNewCard -and (Test-Path -LiteralPath $card)) { throw 'Refusing to overwrite an existing test card.' }
if (Test-Path -LiteralPath $pidFile) {
    $oldPid = [int](Get-Content -LiteralPath $pidFile)
    $old = Get-CimInstance Win32_Process -Filter "ProcessId=$oldPid"
    if ($old -and $old.Name -eq 'pcsx2-qt.exe' -and $old.CommandLine -and $old.CommandLine.Contains($elf)) {
        Stop-Process -Id $oldPid
    }
}
if ($InitializeNewCard) {
    New-Item -ItemType Directory -Path (Join-Path $configRoot 'inis'), (Join-Path $configRoot 'bios'),
        (Join-Path $configRoot 'memcards') -Force | Out-Null
    $base = Join-Path $root 'build\PCSX2-diagnostic\PCSX2'
    Copy-Item (Join-Path $base 'bios\*') (Join-Path $configRoot 'bios') -Force
    $ini = [IO.File]::ReadAllText((Join-Path $base 'inis\PCSX2.ini'))
    $ini = [regex]::Replace($ini, '(?m)^Slot1_Filename = .*$', 'Slot1_Filename = oot-port.ps2')
    [IO.File]::WriteAllText((Join-Path $configRoot 'inis\PCSX2.ini'), $ini)
    $blank = [byte[]]::new(8650752)
    [Array]::Fill[byte]($blank, 255)
    [IO.File]::WriteAllBytes($card, $blank)
    [IO.File]::WriteAllText($token, "OOT_PS2_NEW_TEST_CARD`n")
} else {
    if (!(Test-Path -LiteralPath $card)) { throw 'Initialize a new dedicated test card first.' }
    $ini = [IO.File]::ReadAllText((Join-Path $configRoot 'inis\PCSX2.ini'))
    if ($ini -notmatch '(?m)^Slot1_Filename = oot-port\.ps2\s*$') { throw 'Unexpected test-card configuration.' }
    if (Test-Path -LiteralPath $token) { Remove-Item -LiteralPath $token }
}
if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination ($log + '.previous') -Force }
$process = Start-Process -FilePath 'C:\Program Files\PCSX2\pcsx2-qt.exe' -ArgumentList @(
    '-batch', '-nofullscreen', '-datapath', ('"' + $dataRoot + '"'),
    '-logfile', ('"' + $log + '"'), '-elf', ('"' + $elf + '"')
) -WindowStyle Hidden -PassThru
$process.Id | Set-Content -LiteralPath $pidFile
Write-Output "Memory-card diagnostic PID: $($process.Id)"
Write-Output "Log: $log"
