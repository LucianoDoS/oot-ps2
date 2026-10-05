param([string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe', [string]$LogPath = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$testRoot = Join-Path $projectRoot 'build\PS2-skulltula-test'
$dataRoot = Join-Path $projectRoot 'build\PCSX2-skulltula-test'
$elf = Join-Path $testRoot 'oot_ps2_skulltula_smoke.elf'
$log = Join-Path $testRoot 'pcsx2-skulltula.log'
if ($LogPath) { $log = [IO.Path]::GetFullPath($LogPath) }
$pidFile = Join-Path $testRoot 'pcsx2-skulltula.pid'
if (!(Test-Path -LiteralPath $elf)) { throw 'Build the Skulltula test ELF first.' }
if (!(Test-Path -LiteralPath (Join-Path $testRoot 'oot.z64'))) { throw 'Missing private full data image.' }
$template = Join-Path $projectRoot 'build\PCSX2-smoke-only\PCSX2\inis\PCSX2.ini'
$configDir = Join-Path $dataRoot 'PCSX2\inis'
[IO.Directory]::CreateDirectory($configDir) | Out-Null
$config = [IO.File]::ReadAllText($template)
$config = $config -replace '(?m)^(Slot[12]_Enable) = .*$', '$1 = false'
$config = $config -replace '(?m)^(StandardVolume|FastForwardVolume) = .*$', '$1 = 0'
[IO.File]::WriteAllText((Join-Path $configDir 'PCSX2.ini'), $config)
if (Test-Path -LiteralPath $pidFile) {
    $ownedPid = [int](Get-Content -LiteralPath $pidFile)
    if ($ownedPid -gt 0) {
        $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$ownedPid"
        if ($owned -and $owned.Name -eq 'pcsx2-qt.exe' -and $owned.CommandLine.Contains($elf)) {
            Stop-Process -Id $ownedPid
            Wait-Process -Id $ownedPid -ErrorAction SilentlyContinue
        }
    }
}
if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination ($log + '.previous') -Force }
$process = Start-Process -FilePath $Emulator -WindowStyle Hidden -PassThru -ArgumentList @(
    '-batch', '-nofullscreen', '-datapath', ('"' + $dataRoot + '"'),
    '-logfile', ('"' + $log + '"'), '-elf', ('"' + $elf + '"'))
$process.Id | Set-Content -LiteralPath $pidFile
Write-Output "Skulltula test PID: $($process.Id)"
Write-Output "Log: $log"
