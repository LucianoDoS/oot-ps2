param(
    [switch]$SkipBuild,
    [int]$Start = 0,
    [int]$Count = 388,
    [int]$Frames = 45,
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if ($Start -lt 0 -or $Count -lt 1 -or $Frames -lt 30) { throw 'Invalid room selection.' }
$reportRoot = Join-Path $projectRoot ('build\PS2\room-regression\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[IO.Directory]::CreateDirectory($reportRoot) | Out-Null
$reportPath = Join-Path $reportRoot 'report.json'
$report = [ordered]@{status='RUNNING'; fullProgressionValidated=$false; realPS2Validated=$false; tests=@()}
function Save-Report { $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8 }
function Read-Log([string]$path) {
    if (!(Test-Path -LiteralPath $path)) { return '' }
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
$ownedPid = 0
Push-Location $projectRoot
try {
    Save-Report
    if (!$SkipBuild) {
        & wsl -d Ubuntu -- bash tools/build_ps2_room_smoke.sh --start $Start --count $Count --frames $Frames *> (Join-Path $reportRoot 'build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Room diagnostic build failed.' }
    }
    $builtRoot = Join-Path $projectRoot 'build\PS2-room-test'
    $fixtures = Get-Content (Join-Path $builtRoot 'cases.json') -Raw | ConvertFrom-Json
    if ($fixtures.start -ne $Start -or $fixtures.cases.Count -ne [math]::Min($Count, $fixtures.total_rooms-$Start) -or
        $fixtures.cases[0].frames -ne $Frames) { throw 'Built fixtures differ from requested selection.' }
    foreach ($file in 'oot_ps2_room_smoke.elf','oot.z64','cases.json') {
        Copy-Item -LiteralPath (Join-Path $builtRoot $file) -Destination (Join-Path $reportRoot $file)
    }
    if ((Get-FileHash -LiteralPath (Join-Path $reportRoot 'oot.z64')).Hash.ToLowerInvariant() -ne $fixtures.image_sha256) {
        throw 'Data image differs from generated fixtures.'
    }
    $elf = Join-Path $reportRoot 'oot_ps2_room_smoke.elf'
    $report.elfSha256 = (Get-FileHash -LiteralPath $elf).Hash
    $report.rooms = $fixtures.cases.Count
    $report.start = $Start
    $dataRoot = Join-Path $reportRoot 'emulator'
    $configDir = Join-Path $dataRoot 'PCSX2\inis'
    $biosDir = Join-Path $dataRoot 'PCSX2\bios'
    [IO.Directory]::CreateDirectory($configDir) | Out-Null
    [IO.Directory]::CreateDirectory($biosDir) | Out-Null
    $templateRoot = Join-Path $projectRoot 'build\PCSX2-diagnostic\PCSX2'
    Copy-Item (Join-Path $templateRoot 'bios\*') $biosDir
    $ini = [IO.File]::ReadAllText((Join-Path $templateRoot 'inis\PCSX2.ini'))
    $ini = $ini -replace '(?m)^(Slot[12]_Enable) = .*$', '$1 = false'
    $ini = $ini -replace '(?m)^(StandardVolume|FastForwardVolume) = .*$', '$1 = 0'
    $ini = $ini -replace '(?m)^HostFs = .*$', 'HostFs = true'
    $ini = $ini -replace '(?m)^StartPaused = .*$', 'StartPaused = false'
    $ini = $ini -replace '(?m)^EnableDiscordPresence = .*$', 'EnableDiscordPresence = false'
    foreach ($slot in 1,2) {
        if ($ini -notmatch "(?m)^Slot${slot}_Enable = false\s*$") { throw 'Both player cards must be disabled.' }
    }
    [IO.File]::WriteAllText((Join-Path $configDir 'PCSX2.ini'), $ini)
    $log = Join-Path $reportRoot 'runtime.log'
    $process = Start-Process -FilePath $Emulator -WindowStyle Hidden -PassThru -ArgumentList @(
        '-batch','-nofullscreen','-datapath',('"'+$dataRoot+'"'),'-logfile',('"'+$log+'"'),'-elf',('"'+$elf+'"'))
    $ownedPid = $process.Id
    $report.pid = $ownedPid
    Save-Report
    Write-Output "Room diagnostic PID: $ownedPid"
    Write-Output "Report: $reportPath"
    $deadline = (Get-Date).AddSeconds([math]::Max(180, $fixtures.cases.Count * 15))
    $lastCases = 0
    $lastProgress = Get-Date
    while ((Get-Date) -lt $deadline) {
        $text = Read-Log $log
        if ($text -match '\[PS2 [^\r\n]*\] FAIL\b|\[PS2 FAULT\]|TLB Miss|Address ?Error|branch in delay slot') { throw 'Room diagnostic reported a runtime failure.' }
        $completed = [regex]::Matches($text, '\[PS2 ROOM SWEEP\] PASS case=').Count
        if ($completed -ne $lastCases) {
            $lastCases = $completed
            $lastProgress = Get-Date
            $report.completedRooms = $completed
            Save-Report
            if ($completed % 20 -eq 0) { Write-Output "Rooms passed: $completed/$($fixtures.cases.Count)" }
        }
        if ($text -match '\[PS2 ROOM SWEEP RESULT\] PASS cases=\d+ failures=0') { break }
        if (!(Get-Process -Id $ownedPid -ErrorAction SilentlyContinue)) { throw 'Diagnostic exited without completion.' }
        if (((Get-Date)-$lastProgress).TotalSeconds -gt 90) { throw 'No room progress for 90 seconds.' }
        Start-Sleep -Milliseconds 500
    }
    if ($text -notmatch '\[PS2 ROOM SWEEP RESULT\] PASS cases=\d+ failures=0') { throw 'Room sweep timed out.' }
    & python tools/check_ps2_room_sweep.py --log $log --cases (Join-Path $reportRoot 'cases.json') --captures $reportRoot --output (Join-Path $reportRoot 'rooms.json')
    if ($LASTEXITCODE -ne 0) { throw 'Independent room/capture validation failed.' }
    & python tools/report_ps2_rooms.py --report (Join-Path $reportRoot 'rooms.json')
    if ($LASTEXITCODE -ne 0) { throw 'Room gallery generation failed.' }
    $report.status = 'PASS'
} catch {
    $report.status = 'FAIL'; $report.error = $_.Exception.Message
    if ($log -and (Test-Path -LiteralPath $log)) {
        & python tools/check_ps2_room_sweep.py --allow-partial --log $log --cases (Join-Path $reportRoot 'cases.json') --captures $reportRoot --output (Join-Path $reportRoot 'rooms.json')
        if ($LASTEXITCODE -eq 0) { & python tools/report_ps2_rooms.py --report (Join-Path $reportRoot 'rooms.json') }
    }
    throw
} finally {
    if ($ownedPid -gt 0) {
        $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$ownedPid"
        if ($owned) {
            if ($owned.Name -ne 'pcsx2-qt.exe' -or !$owned.CommandLine.Contains($elf) -or !$owned.CommandLine.Contains($dataRoot)) { throw 'Diagnostic identity differs; refusing to stop it.' }
            Stop-Process -Id $ownedPid
            Wait-Process -Id $ownedPid -Timeout 10 -ErrorAction SilentlyContinue
        }
    }
    $report.finished = (Get-Date).ToUniversalTime().ToString('o')
    Save-Report
    Pop-Location
    Write-Output "Report: $reportPath"
}
