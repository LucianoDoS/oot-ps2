param(
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe',
    [string]$Elf = 'build\PS2-ntsc12\oot_ps2_usb_runtime.elf',
    [int]$Seconds = 45,
    [string]$Python = '/home/lucia/oot-ps2-tools/usb-test-venv/bin/python',
    [switch]$Transition
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if ($Transition) { $Elf = 'build\PS2-ntsc12\oot_ps2_usb_transition_test.elf' }
$root = Join-Path $project ('build\PS2\usb-stack-checks\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$config = Join-Path $root 'emulator\PCSX2\inis'
$bios = Join-Path $root 'emulator\PCSX2\bios'
[IO.Directory]::CreateDirectory($config) | Out-Null
[IO.Directory]::CreateDirectory($bios) | Out-Null
$template = Join-Path $project 'build\PCSX2-diagnostic\PCSX2'
Copy-Item (Join-Path $template 'bios\*') $bios
$ini = [IO.File]::ReadAllText((Join-Path $template 'inis\PCSX2.ini'))
$ini = $ini -replace '(?m)^(Slot[12]_Enable) = .*$', '$1 = false'
$ini = $ini -replace '(?m)^(StandardVolume|FastForwardVolume) = .*$', '$1 = 0'
$ini = $ini -replace '(?m)^HostFs = .*$', 'HostFs = false'
$ini = $ini -replace '(?m)^StartPaused = .*$', 'StartPaused = false'
$image = Join-Path $root 'usb.img'
$ini = $ini -replace '(?ms)\[USB1\].*?(?=\[USB2\])', ("[USB1]`nType = Msd`nMsd_subtype = 0`nMsd_ImagePathMsd = $image`n`n")
$ini = $ini -replace '(?ms)\[USB2\].*?(?=\[)', "[USB2]`nType = None`n`n"
foreach ($slot in 1,2) { if ($ini -notmatch "(?m)^Slot${slot}_Enable = false\s*$") { throw 'Player cards must be disabled.' } }
[IO.File]::WriteAllText((Join-Path $config 'PCSX2.ini'), $ini)
function WslPath([string]$path) { '/mnt/'+$path.Substring(0,1).ToLower()+'/'+$path.Substring(3).Replace('\','/') }
$fixture = WslPath (Join-Path $project 'tools\ps2_usb_test_image.py')
$imageWsl = WslPath $image
$dataWsl = WslPath (Join-Path $project 'build\PS2-ntsc12\oot-full-next.z64')
& wsl -d Ubuntu -- $Python $fixture create --image $imageWsl --data $dataWsl
if ($LASTEXITCODE) { throw 'FAT32 image creation failed.' }
$testElf = Join-Path $root 'check.elf'
Copy-Item -LiteralPath (Join-Path $project $Elf) $testElf
$log = Join-Path $root 'runtime.log'
$dataRoot = Join-Path $root 'emulator'
$process = Start-Process -FilePath $Emulator -WindowStyle Hidden -PassThru -ArgumentList @(
    '-batch','-nofullscreen','-datapath',('"'+$dataRoot+'"'),'-logfile',('"'+$log+'"'),'-elf',('"'+$testElf+'"'))
try {
    $deadline = (Get-Date).AddSeconds($Seconds)
    do { Start-Sleep -Milliseconds 500 } while (!$process.HasExited -and (Get-Date) -lt $deadline)
} finally {
    $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
    if ($owned) {
        if ($owned.Name -ne 'pcsx2-qt.exe' -or !$owned.CommandLine.Contains($testElf) -or !$owned.CommandLine.Contains($dataRoot)) { throw 'Process identity changed.' }
        Stop-Process -Id $process.Id
        Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
    }
}
& wsl -d Ubuntu -- $Python $fixture extract --image $imageWsl --output (WslPath $root) --data $dataWsl
if ($LASTEXITCODE) { throw 'Log extraction failed.' }
$bootText = [IO.File]::ReadAllText((Join-Path $root 'BOOT.LOG'))
$runtimeText = [IO.File]::ReadAllText((Join-Path $root 'DIAG.LOG'))
$emuText = [IO.File]::ReadAllText($log)
$errors = @()
if ($bootText -notmatch '^OoT PS2 boot trace v2 ' -or $runtimeText -notmatch '^OOT PS2 runtime log v3 ') { $errors += 'Invalid log header' }
if ($bootText.Contains([string][char]0) -or $runtimeText.Contains([string][char]0)) { $errors += 'NUL bytes in text log' }
$bootLines = @($bootText -split '\r?\n' | Select-Object -Skip 1 | Where-Object { $_.Trim() })
foreach ($line in $bootLines) {
    $expected = $line.TrimEnd()
    if (!$emuText.Contains('[PS2 BOOT TRACE] '+$expected)) { $errors += "Boot record differs from EE output: $expected" }
}
if ($bootLines.Count -lt 80 -or $bootText -notmatch '\[PS2 FRAME\].*frame 1') { $errors += 'Game did not reach its first frame' }
foreach ($stage in 'LOGGER_START','LOGGER_ENTER','SAMPLE seq=','PLAY_INIT_READY','ROOM_READY','FLUSH bytes=') {
    if (!$runtimeText.Contains($stage)) { $errors += "Missing $stage" }
}
if ($emuText -match 'USBHDFSD: Error|usb_bulk_manage_status.*error|TLB Miss|Address ?Error|\[PS2 FAULT\]') { $errors += 'Emulator reported a USB or EE fault' }
if ($runtimeText -match 'lost=[1-9]|lostRecords=[1-9]|ERROR logger|ERROR writing|clockFallback=1') { $errors += 'Runtime logging failed or lost data' }
if ($Transition) {
    foreach ($phase in 0,1,2) {
        if ($emuText -notmatch "\[PS2 USB TRANSITION\] PASS phase=$phase .*frames=120") { $errors += "Missing transition phase $phase" }
    }
    if (!$emuText.Contains('[PS2 USB TRANSITION RESULT] PASS house-forest-house')) { $errors += 'House/forest round trip did not complete' }
}
@{status=$(if ($errors.Count) {'FAIL'} else {'PASS'}); errors=$errors; elfSha256=(Get-FileHash $testElf).Hash; transitions=[bool]$Transition; hostFs=$false; physicalUSBValidated=$false; realPS2Validated=$false} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'report.json') -Encoding utf8
Write-Output "Report: $root"
if ($errors.Count) { throw ($errors -join '; ') }
Write-Output 'Native USB stack PASS: preserved data, intact logs and ongoing game frames.'
