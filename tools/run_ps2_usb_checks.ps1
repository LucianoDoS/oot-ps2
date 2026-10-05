param([string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$root = Join-Path $project ('build\PS2\usb-host-checks\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[IO.Directory]::CreateDirectory($root) | Out-Null
$results = @()
foreach ($case in 'valid','wrong-size','wrong-block','valid-game','runtime-game','runtime224-game','missing-usb','missing-usb-game') {
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
    $builtElf = if ($case -eq 'runtime224-game') { 'oot_ps2_usb_runtime224_host_test.elf' } elseif ($case -eq 'runtime-game') { 'oot_ps2_usb_runtime_host_test.elf' } elseif ($case -eq 'valid-game') { 'oot_ps2_usb_game_host_test.elf' } elseif ($case -eq 'missing-usb') { 'oot_ps2_usb_diagnostic.elf' } elseif ($case -eq 'missing-usb-game') { 'oot_ps2_usb.elf' } else { 'oot_ps2_usb_host_test.elf' }
    Copy-Item -LiteralPath (Join-Path $project ('build\PS2-ntsc12\'+$builtElf)) $elf
    Copy-Item -LiteralPath (Join-Path $project 'build\PS2-ntsc12\oot-full-next.z64') $data
    $bootLog = Join-Path $folder 'BOOT.LOG'
    $priorHash = $null
    if ($case -eq 'valid') {
        # A log longer than the reader's buffer must survive BOOTTEST unchanged.
        $fixture = (1..120 | ForEach-Object { "$_ JUEGO: etapa anterior de prueba`n" }) -join ''
        [IO.File]::WriteAllText($bootLog, $fixture, [Text.Encoding]::ASCII)
        $priorHash = (Get-FileHash -LiteralPath $bootLog -Algorithm SHA256).Hash
    }
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
            $done = $text -match '\[PS2 USB DIAGNOSTIC\] PASS|\[PS2 USB BOOT\] FAIL|\[PS2 FRAME\].*frame 1'
            if ($case -in 'valid-game','runtime-game','runtime224-game' -and $done) {
                $done = (Test-Path -LiteralPath $bootLog) -and ([IO.File]::ReadAllText($bootLog) -match '\[PS2 FRAME\].*frame 1\r?\n')
            }
            if ($case -in 'runtime-game','runtime224-game' -and $done) {
                $runtimeLog = Join-Path $folder 'DIAG.LOG'
                $runtimeText = if (Test-Path -LiteralPath $runtimeLog) { [IO.File]::ReadAllText($runtimeLog) } else { '' }
                $done = $runtimeText -match 'FLUSH bytes=' -and $runtimeText -match 'PLAY_INIT_READY' -and $runtimeText -match 'ROOM_READY'
            }
        } while (!$done -and (Get-Date) -lt $deadline)
        if ($text -match 'TLB Miss|Address ?Error|\[PS2 FAULT\]') { throw 'Native boot check faulted.' }
        if ($case -in 'missing-usb','missing-usb-game') {
            if ($text -notmatch '\[PS2 USB MODULE\] USBD id=\d+ rv=0' -or $text -notmatch '\[PS2 USB MODULE\] USBHDFSD id=\d+ rv=0' -or $text -notmatch '\[PS2 USB BOOT\] FAIL No encuentro OOT.Z64') { throw 'USB driver startup or bounded missing-file handling failed.' }
            if ($case -eq 'missing-usb-game' -and $text -notmatch 'Modulos IOP cargados\.') { throw 'Game IOP modules were not initialized before USB.' }
        } elseif ($case -in 'valid-game','runtime-game','runtime224-game') {
            if ($text -notmatch '\[PS2 USB BOOT\] PASS' -or $text -notmatch '\[osRom\] ROM abierta: host:OOT.Z64' -or $text -notmatch '\[PS2 FRAME\].*frame 1') { throw 'Game did not boot through the validated data path.' }
            $bootText = [IO.File]::ReadAllText($bootLog)
            foreach ($stage in 'MAIN: ejecutable iniciado','ACTORES: datos iniciales conservados','IOP: reinicio terminado','USB: DATOS OK','BOOT.LOG: registro activo','VIDEO: listo','MANDO init resultado=','GUARDADO: inicializacion terminada','AUDIO: inicializacion terminada','VBLANK: activo','DATOS: archivo abierto para el juego','LOGO N64: segmento cargado','LOGO N64: inicializacion terminada','ESTADO: mensaje grafico enviado','\[PS2 STATE\] Init completed','\[PS2 FRAME\].*frame 1') {
                if ($bootText -notmatch $stage) { throw "BOOT.LOG is missing stage: $stage" }
            }
            if ($bootText -match 'ERROR:|BOOT.LOG: fallo|BOOT.LOG: no pude') { throw 'Startup tracing failed.' }
            if ($case -in 'runtime-game','runtime224-game') {
                $runtimeText = [IO.File]::ReadAllText($runtimeLog)
                foreach ($stage in 'runtime log v2','LOGGER_START .*wake=vblank','LOGGER_ENTER wake=vblank','LOGO N64: inicializacion terminada','SAMPLE .*frames=[1-9]','USB reads=','AUDIO queueBytesMin=','FLUSH bytes=','PLAY_INIT_READY','ROOM_SETUP_BEGIN','ROOM_READY') {
                    if ($runtimeText -notmatch $stage) { throw "DIAG.LOG is missing: $stage" }
                }
                if ($runtimeText -match 'lost=[1-9]|lostRecords=[1-9]|ERROR logger|ERROR writing|clockFallback=1') { throw 'Runtime logger lost records, failed or used the clock fallback.' }
                $video = if ($case -eq 'runtime224-game') { 'render=640x224 displayHeight=448 interlace=1 field=0 magV=1' } else { 'render=640x448 displayHeight=448 interlace=1 field=0 magV=0' }
                if ($runtimeText -notmatch [regex]::Escape($video)) { throw 'Unexpected render/display configuration.' }
            } elseif (Test-Path -LiteralPath (Join-Path $folder 'DIAG.LOG')) { throw 'Normal game must not create DIAG.LOG.' }
        } elseif ($case -eq 'valid') {
            if ($text -notmatch '\[PS2 USB DIAGNOSTIC\] PASS data=1 cardWrites=0 hardwareValidated=0') { throw 'Valid data was rejected.' }
            if ($text -notmatch '\[PS2 CACHE CHECK\] PASS neighbours=1 cpuPayload=1 dmaAlias=1 interruptState=1') { throw 'Cache/payload check failed.' }
            if ($text -notmatch '\[PS2 USB PREVIOUS LOG\]' -or $text -notmatch '120 JUEGO: etapa anterior de prueba' -or $text -match '110 JUEGO: etapa anterior de prueba') { throw 'Previous log tail was not displayed correctly.' }
            if ((Get-FileHash -LiteralPath $bootLog -Algorithm SHA256).Hash -ne $priorHash) { throw 'BOOTTEST modified the previous log.' }
        } else {
            $expected = if ($case -eq 'wrong-size') { 'Tamano incorrecto' } else { 'Datos incorrectos o corruptos' }
            if ($text -notmatch "\[PS2 USB BOOT\] FAIL $expected" -or $text -match '\[PS2 USB BOOT\] PASS') { throw 'Invalid data was accepted or wrong failure.' }
        }
        if ($case -notin 'valid','valid-game','runtime-game','runtime224-game' -and (Test-Path -LiteralPath $bootLog)) { throw 'Unexpected diagnostic log write.' }
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
