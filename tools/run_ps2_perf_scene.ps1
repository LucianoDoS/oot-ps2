param(
    [switch]$SkipBuild,
    [ValidateSet('house_path','title_cinematic')][string]$Mode = 'house_path',
    [int]$TimeoutSeconds = 120,
    [string]$FixtureRoot = 'build\PS2-perf-scene',
    [string]$Baseline = '',
    [string]$Python = 'C:\Users\Lucia\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe',
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$reportRoot = Join-Path $projectRoot ('build\PS2\perf-scenes\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
[IO.Directory]::CreateDirectory($reportRoot) | Out-Null
$reportPath = Join-Path $reportRoot 'report.json'
$report = [ordered]@{status='RUNNING'; realPS2Validated=$false; cardsDisabled=$true; inputDisabled=$true; tests=@()}
function Save-Report { $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $reportPath -Encoding utf8 }
function Read-Log([string]$path) {
    if (!(Test-Path -LiteralPath $path)) { return '' }
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
$ownedPid = 0
$playerCard = Join-Path $projectRoot 'build\PCSX2-diagnostic\PCSX2\memcards\oot-port.ps2'
$playerBefore = if (Test-Path -LiteralPath $playerCard) { (Get-FileHash -LiteralPath $playerCard).Hash } else { $null }
Push-Location $projectRoot
try {
    if ($TimeoutSeconds -lt 30) { throw 'Timeout must be at least 30 seconds.' }
    Save-Report
    if (!$SkipBuild) {
        & wsl -d Ubuntu -- env "PS2_PERF_SCENE_MODE=$Mode" bash tools/build_ps2_perf_scene.sh *> (Join-Path $reportRoot 'build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Scene benchmark build failed.' }
    }
    $builtRoot = if ([IO.Path]::IsPathRooted($FixtureRoot)) { $FixtureRoot } else { Join-Path $projectRoot $FixtureRoot }
    foreach ($file in 'oot_ps2_perf_scene.elf','oot.z64','fixture.json') {
        Copy-Item -LiteralPath (Join-Path $builtRoot $file) -Destination (Join-Path $reportRoot $file)
    }
    $fixture = Get-Content -LiteralPath (Join-Path $reportRoot 'fixture.json') -Raw | ConvertFrom-Json
    $fixtureMode = if ($fixture.PSObject.Properties.Name -contains 'mode') { $fixture.mode } else { 'house_path' }
    if ($fixtureMode -ne $Mode) { throw ('Fixture mode mismatch: requested ' + $Mode + ', found ' + $fixtureMode) }
    $report.mode = $fixtureMode
    $completion = '\[PS2 PERF SCENE RESULT\] PASS views=' + $fixture.views + ' failures=0'
    foreach ($item in @(@('oot_ps2_perf_scene.elf','elf_sha256'),@('oot.z64','image_sha256'))) {
        if ((Get-FileHash -LiteralPath (Join-Path $reportRoot $item[0])).Hash.ToLowerInvariant() -ne $fixture.($item[1])) {
            throw ('Fixture hash mismatch: ' + $item[0])
        }
    }
    $elf = Join-Path $reportRoot 'oot_ps2_perf_scene.elf'
    $report.elfSha256 = $fixture.elf_sha256
    $report.imageSha256 = $fixture.image_sha256
    $report.emulatorSha256 = (Get-FileHash -LiteralPath $Emulator).Hash
    $dataRoot = Join-Path $reportRoot 'emulator'
    $configDir = Join-Path $dataRoot 'PCSX2\inis'
    $biosDir = Join-Path $dataRoot 'PCSX2\bios'
    [IO.Directory]::CreateDirectory($configDir) | Out-Null
    [IO.Directory]::CreateDirectory($biosDir) | Out-Null
    $templateRoot = Join-Path $projectRoot 'build\PCSX2-diagnostic\PCSX2'
    Copy-Item (Join-Path $templateRoot 'bios\*') $biosDir
    $ini = [IO.File]::ReadAllText((Join-Path $templateRoot 'inis\PCSX2.ini'))
    $ini = $ini -replace '(?m)^(Slot[12]_Enable) = .*$', '$1 = false'
    $ini = $ini -replace '(?m)^(Multitap[12]_Slot[234]_Enable) = .*$', '$1 = false'
    $ini = $ini -replace '(?m)^(StandardVolume|FastForwardVolume) = .*$', '$1 = 0'
    $ini = $ini -replace '(?m)^HostFs = .*$', 'HostFs = true'
    $ini = $ini -replace '(?m)^StartPaused = .*$', 'StartPaused = false'
    $ini = $ini -replace '(?m)^EnableDiscordPresence = .*$', 'EnableDiscordPresence = false'
    $ini = $ini -replace '(?m)^(EECycleRate|EECycleSkip) = .*$', '$1 = 0'
    $ini = $ini -replace '(?m)^NominalScalar = .*$', 'NominalScalar = 1'
    $ini = [regex]::Replace($ini, '(?ms)^\[Pad([1-8])\]\r?\n.*?(?=^\[|\z)', {
        param($match)
        "[Pad$($match.Groups[1].Value)]`r`nType = None`r`n`r`n"
    })
    foreach ($slot in 1,2) {
        if ($ini -notmatch "(?m)^Slot${slot}_Enable = false\s*$") { throw 'Both player cards must be disabled.' }
    }
    foreach ($slot in 1..8) {
        if ($ini -notmatch "(?m)^\[Pad${slot}\]\r?\nType = None\s*$") { throw 'All controllers must be disabled.' }
    }
    [IO.File]::WriteAllText((Join-Path $configDir 'PCSX2.ini'), $ini)
    $report.iniSha256 = (Get-FileHash -LiteralPath (Join-Path $configDir 'PCSX2.ini')).Hash
    $log = Join-Path $reportRoot 'runtime.log'
    $process = Start-Process -FilePath $Emulator -WindowStyle Hidden -PassThru -WorkingDirectory $reportRoot -ArgumentList @(
        '-batch','-nofullscreen','-datapath',('"'+$dataRoot+'"'),'-logfile',('"'+$log+'"'),'-elf',('"'+$elf+'"'))
    $ownedPid = $process.Id
    $report.pid = $ownedPid
    Save-Report
    Write-Output "Scene benchmark PID: $ownedPid"
    Write-Output "Report: $reportPath"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastViews = 0
    while ((Get-Date) -lt $deadline) {
        $text = Read-Log $log
        if ($text -match '\[PS2 [^\r\n]*\] FAIL\b|\[PS2 FAULT\]|TLB Miss|Address ?Error|branch in delay slot') { throw 'Scene benchmark reported a runtime failure.' }
        $completed = [regex]::Matches($text, '\[PS2 PERF SCENE VIEW\] PASS view=').Count
        if ($completed -ne $lastViews) {
            $lastViews = $completed
            $report.completedViews = $completed
            Save-Report
            Write-Output "Views completed: $completed/$($fixture.views)"
        }
        if ($text -match $completion) { break }
        if (!(Get-Process -Id $ownedPid -ErrorAction SilentlyContinue)) { throw 'Benchmark exited without completion.' }
        Start-Sleep -Milliseconds 500
    }
    if ($text -notmatch $completion) { throw 'Scene benchmark timed out.' }
    $verifyArgs = @((Join-Path $PSScriptRoot 'verify_ps2_perf_scene.py'), $reportRoot)
    if ($Baseline) { $verifyArgs += @('--baseline', $Baseline) }
    & $Python -X utf8 @verifyArgs
    if ($LASTEXITCODE -ne 0) { throw 'Independent scene/capture validation failed.' }
    $report.status = 'PASS'
} catch {
    $report.status = 'FAIL'; $report.error = $_.Exception.Message
    throw
} finally {
    if ($ownedPid -gt 0) {
        $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$ownedPid"
        if ($owned) {
            if ($owned.Name -ne 'pcsx2-qt.exe' -or !$owned.CommandLine.Contains($elf) -or !$owned.CommandLine.Contains($dataRoot)) {
                throw 'Benchmark identity differs; refusing to stop it.'
            }
            Stop-Process -Id $ownedPid
            Wait-Process -Id $ownedPid -Timeout 10 -ErrorAction SilentlyContinue
        }
    }
    $playerAfter = if (Test-Path -LiteralPath $playerCard) { (Get-FileHash -LiteralPath $playerCard).Hash } else { $null }
    $report.playerCardUnchanged = $playerBefore -eq $playerAfter
    if (!$report.playerCardUnchanged) { $report.status = 'FAIL'; $report.error = 'Player card changed during private benchmark.' }
    $report.finished = (Get-Date).ToUniversalTime().ToString('o')
    Save-Report
    Pop-Location
    Write-Output "Report: $reportPath"
}
