param(
    [switch]$SkipBuild,
    [switch]$IncludeSceneSweep,
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$reportRoot = Join-Path $projectRoot ('build\PS2\regression\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($reportRoot) | Out-Null
$report = [ordered]@{
    started = (Get-Date).ToUniversalTime().ToString('o')
    status = 'RUNNING'
    fullProgressionValidated = $false
    realPS2Validated = $false
    scope = 'Native graphics/audio/SRAM tests, isolated card persistence/repair, Skulltula AI/contact; optional scene entrance sweep'
    tests = @()
}
$reportPath = Join-Path $reportRoot 'report.json'
function Save-Report { $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8 }
function Read-Log([string]$path) {
    if (!(Test-Path -LiteralPath $path)) { return '' }
    try {
        $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        $reader = [IO.StreamReader]::new($stream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } catch [IO.IOException] { return '' }
}
function Build-Test([string]$script, [string[]]$arguments = @()) {
    $buildLog = Join-Path $reportRoot ([IO.Path]::GetFileNameWithoutExtension($script) + '.log')
    Write-Output "Building $script"
    & wsl -d Ubuntu -- bash $script @arguments *> $buildLog
    if ($LASTEXITCODE -ne 0) { throw "Build failed; see $buildLog" }
}
function Run-Test([string]$name, [string]$elfRelative, [string]$logRelative,
                  [string]$pidRelative, [string]$passPattern, [int]$timeout, [scriptblock]$launch) {
    $elf = Join-Path $projectRoot $elfRelative
    $log = if ([IO.Path]::IsPathRooted($logRelative)) { $logRelative } else { Join-Path $projectRoot $logRelative }
    $pidPath = Join-Path $projectRoot $pidRelative
    $record = [ordered]@{ name = $name; status = 'RUNNING'; elfSha256 = (Get-FileHash -LiteralPath $elf).Hash }
    $report.tests += $record
    Save-Report
    $ownedPid = 0
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        # A separate logfile per launch prevents accepting an earlier PASS and
        # avoids reopening a file which an exiting process has not released yet.
        if (Test-Path -LiteralPath $log) { throw "Expected a fresh diagnostic logfile: $log" }
        & $launch | Write-Output
        $ownedPid = [int](Get-Content -LiteralPath $pidPath)
        $deadline = (Get-Date).AddSeconds($timeout)
        while ((Get-Date) -lt $deadline) {
            $text = Read-Log $log
            if ($text -match '(?m)\[PS2 [^\r\n]*\]\s*FAIL\b|\[PS2 FAULT\]|TLB Miss|Address Error|branch in delay slot') {
                throw "Runtime failure in $name"
            }
            if ($text -match $passPattern) { $record.status = 'PASS'; break }
            if (!(Get-Process -Id $ownedPid -ErrorAction SilentlyContinue)) { throw "$name exited without a PASS result" }
            Start-Sleep -Milliseconds 500
        }
        if ($record.status -ne 'PASS') { throw "$name timed out after $timeout seconds" }
    } catch {
        $record.status = 'FAIL'
        $record.error = $_.Exception.Message
        throw
    } finally {
        $record.elapsedSeconds = [math]::Round($timer.Elapsed.TotalSeconds, 2)
        if ($ownedPid -gt 0) {
            $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$ownedPid"
            if ($owned) {
                if ($owned.Name -ne 'pcsx2-qt.exe' -or !$owned.CommandLine -or !$owned.CommandLine.Contains($elf)) {
                    throw "Diagnostic PID no longer identifies $elf; refusing to stop it"
                }
                Stop-Process -Id $ownedPid
                Wait-Process -Id $ownedPid -ErrorAction SilentlyContinue
            }
            0 | Set-Content -LiteralPath $pidPath
        }
        if (Test-Path -LiteralPath $log) {
            $record.log = Join-Path $reportRoot ($name + '.log')
            Copy-Item -LiteralPath $log -Destination $record.log
        }
        Save-Report
    }
    Write-Output "PASS $name ($($record.elapsedSeconds) s)"
}
Push-Location $projectRoot
try {
    $smokeRoot = Join-Path $projectRoot 'build\PCSX2-smoke-only'
    $smokeIni = [IO.File]::ReadAllText((Join-Path $smokeRoot 'PCSX2\inis\PCSX2.ini'))
    foreach ($slot in 1, 2) {
        if ($smokeIni -notmatch "(?m)^Slot${slot}_Enable = false\s*$") { throw 'Renderer test configuration must disable both memory cards' }
    }
    if (!$SkipBuild) {
        Build-Test 'tools/build_ps2_smoke.sh'
        Build-Test 'tools/build_ps2_memorycard_smoke.sh'
        Build-Test 'tools/build_ps2_skulltula_smoke.sh'
        if ($IncludeSceneSweep) { Build-Test 'tools/build_ps2_scene_smoke.sh' @('--start', '0', '--count', '191', '--frames', '60') }
    }
    & python tools/check_ps2_save_icon.py --folder (Join-Path $projectRoot 'build\PS2\memorycard-icon') --report (Join-Path $reportRoot 'icon-format.json')
    if ($LASTEXITCODE -ne 0) { throw 'Independent icon format validation failed' }
    $coreLog = Join-Path $reportRoot 'native-core.runtime.log'
    Run-Test 'native-core' 'build\PS2\oot_ps2_smoke.elf' $coreLog 'build\PS2\pcsx2-diagnostic.pid' '\[PS2 SMOKE\] PASS: 120 frames submitted' 240 {
        & tools/run_ps2_smoke.ps1 -Emulator $Emulator -DataRoot $smokeRoot -LogPath $coreLog
    }
    $cardRoot = Join-Path $reportRoot 'card-runtime'
    $report.testCard = Join-Path $cardRoot 'PCSX2\memcards\oot-port.ps2'
    $cardLog = Join-Path $reportRoot 'card-write-repair.runtime.log'
    Run-Test 'card-write-repair' 'build\PS2\oot_ps2_memorycard_smoke.elf' $cardLog 'build\PS2\pcsx2-memorycard.pid' '\[PS2 CARD TEST\] PASS:' 120 {
        & tools/run_ps2_memorycard_smoke.ps1 -Emulator $Emulator -DataRoot $cardRoot -InitializeNewCard -LogPath $cardLog
    }
    $coldLog = Join-Path $reportRoot 'card-cold-restart.runtime.log'
    Run-Test 'card-cold-restart' 'build\PS2\oot_ps2_memorycard_smoke.elf' $coldLog 'build\PS2\pcsx2-memorycard.pid' '\[PS2 CARD TEST\] PASS: cold restart:' 120 {
        & tools/run_ps2_memorycard_smoke.ps1 -Emulator $Emulator -DataRoot $cardRoot -LogPath $coldLog
    }
    $spiderLog = Join-Path $reportRoot 'skulltula-contact.runtime.log'
    Run-Test 'skulltula-contact' 'build\PS2-skulltula-test\oot_ps2_skulltula_smoke.elf' $spiderLog 'build\PS2-skulltula-test\pcsx2-skulltula.pid' '\[PS2 SKULLTULA RESULT\] PASS cases=7 failures=0' 240 {
        & tools/run_ps2_skulltula_smoke.ps1 -Emulator $Emulator -LogPath $spiderLog
    }
    if ($IncludeSceneSweep) {
        $sceneLog = Join-Path $reportRoot 'scene-entrances.runtime.log'
        Run-Test 'scene-entrances' 'build\PS2-full-test\oot_ps2_scene_smoke.elf' $sceneLog 'build\PS2-full-test\pcsx2-scene-sweep.pid' '\[PS2 SCENE SWEEP RESULT\] PASS cases=191 failures=0' 1500 {
            & tools/run_ps2_scene_smoke.ps1 -Emulator $Emulator -LogPath $sceneLog
        }
        & python tools/check_ps2_scene_sweep.py --log $sceneLog --output (Join-Path $reportRoot 'scenes.json')
        if ($LASTEXITCODE -ne 0) { throw 'Independent scene log validation failed' }
    }
    $report.status = 'PASS'
} catch {
    $report.status = 'FAIL'
    $report.error = $_.Exception.Message
    throw
} finally {
    $report.finished = (Get-Date).ToUniversalTime().ToString('o')
    Save-Report
    Pop-Location
    Write-Output "Report: $reportPath"
}
