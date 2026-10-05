param([string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$root = Join-Path $project ('build\PS2\usb-host-checks\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[IO.Directory]::CreateDirectory($root) | Out-Null
$results = @()
foreach ($case in 'valid','wrong-size','wrong-block','valid-game','missing-usb','missing-usb-game') {
    $folder = Join-Path $root $case
    $config = Join-Path $folder 'emulator\PCSX2\inis'
    $bios = Join-Path $folder 'emulator\PCSX2\bios'
    [IO.Directory]::CreateDirectory($config) | Out-Null
    [IO.Directory]::CreateDirectory($bios) | Out-Null
    $template = Join-Path $project 'build\PCSX2-diagnostic\PCSX2'
    Copy-Item (Join-Path $template 'bios\*') $bios
    $ini = [IO.File]::ReadAllText((Join-Path $template 'inis\PCSX2.ini'))
    $ini = $ini -replace '(?m)^(Slot[12]_Enable) = .*$', '$1 = false'
    $ini = $ini -replace '(?m)^(StandardVolume|FastForwardVolume) = .*$', '$1 = 0'
    $ini = $ini -replace '(?m)^HostFs = .*$', 'HostFs = true'
    $ini = $ini -replace '(?m)^StartPaused = .*$', 'StartPaused = false'
    foreach ($slot in 1,2) { if ($ini -notmatch "(?m)^Slot${slot}_Enable = false\s*$") { throw 'Player cards must be disabled.' } }
    [IO.File]::WriteAllText((Join-Path $config 'PCSX2.ini'), $ini)
    $elf = Join-Path $folder 'check.elf'
    $data = Join-Path $folder 'OOT.Z64'
    $builtElf = if ($case -eq 'valid-game') { 'oot_ps2_usb_game_host_test.elf' } elseif ($case -eq 'missing-usb') { 'oot_ps2_usb_diagnostic.elf' } elseif ($case -eq 'missing-usb-game') { 'oot_ps2_usb.elf' } else { 'oot_ps2_usb_host_test.elf' }
    Copy-Item -LiteralPath (Join-Path $project ('build\PS2-ntsc12\'+$builtElf)) $elf
    Copy-Item -LiteralPath (Join-Path $project 'build\PS2-ntsc12\oot-full-next.z64') $data
    if ($case -in 'wrong-size','wrong-block') {
        $stream = [IO.File]::Open($data, [IO.FileMode]::Open, [IO.FileAccess]::Write)
        try {
            if ($case -eq 'wrong-size') { $stream.SetLength(4096) }
            else { $stream.Position=0; $stream.WriteByte(0) }
        } finally { $stream.Dispose() }
    }
    $log = Join-Path $folder 'runtime.log'
    $dataRoot = Join-Path $folder 'emulator'
    $process = Start-Process -FilePath $Emulator -WindowStyle Hidden -PassThru -ArgumentList @(
        '-batch','-nofullscreen','-datapath',('"'+$dataRoot+'"'),'-logfile',('"'+$log+'"'),'-elf',('"'+$elf+'"'))
    try {
        $deadline = (Get-Date).AddSeconds(50)
        $text = ''
        do {
            Start-Sleep -Milliseconds 500
            if (Test-Path -LiteralPath $log) {
                $s = [IO.FileStream]::new($log,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
                $reader = [IO.StreamReader]::new($s)
                try { $text=$reader.ReadToEnd() } finally { $reader.Dispose() }
            }
        } while ($text -notmatch '\[PS2 USB DIAGNOSTIC\] PASS|\[PS2 USB BOOT\] FAIL|\[PS2 FRAME\].*frame 1' -and (Get-Date) -lt $deadline)
        if ($text -match 'TLB Miss|Address ?Error|\[PS2 FAULT\]') { throw 'Native boot check faulted.' }
        if ($case -in 'missing-usb','missing-usb-game') {
            if ($text -notmatch '\[PS2 USB MODULE\] USBD id=\d+ rv=0' -or $text -notmatch '\[PS2 USB MODULE\] USBHDFSD id=\d+ rv=0' -or $text -notmatch '\[PS2 USB BOOT\] FAIL No encuentro OOT.Z64') { throw 'USB driver startup or bounded missing-file handling failed.' }
            if ($case -eq 'missing-usb-game' -and $text -notmatch 'Modulos IOP cargados\.') { throw 'Game IOP modules were not initialized before USB.' }
        } elseif ($case -eq 'valid-game') {
            if ($text -notmatch '\[PS2 USB BOOT\] PASS' -or $text -notmatch '\[osRom\] ROM abierta: host:OOT.Z64' -or $text -notmatch '\[PS2 FRAME\].*frame 1') { throw 'Game did not boot through the validated data path.' }
        } elseif ($case -eq 'valid') {
            if ($text -notmatch '\[PS2 USB DIAGNOSTIC\] PASS data=1 cardWrites=0 hardwareValidated=0') { throw 'Valid data was rejected.' }
        } else {
            $expected = if ($case -eq 'wrong-size') { 'Tamano incorrecto' } else { 'Datos incorrectos o corruptos' }
            if ($text -notmatch "\[PS2 USB BOOT\] FAIL $expected" -or $text -match '\[PS2 USB BOOT\] PASS') { throw 'Invalid data was accepted or wrong failure.' }
        }
        $results += @{case=$case;status='PASS';log=$log}
        Write-Output "$case PASS"
    } finally {
        $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
        if ($owned) {
            if ($owned.Name -ne 'pcsx2-qt.exe' -or !$owned.CommandLine.Contains($elf) -or !$owned.CommandLine.Contains($dataRoot)) { throw 'Process identity changed.' }
            Stop-Process -Id $process.Id
            Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
        }
    }
}
@{status='PASS'; physicalUSBValidated=$false; realPS2Validated=$false; tests=$results} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'report.json') -Encoding utf8
Write-Output "Report: $root"
