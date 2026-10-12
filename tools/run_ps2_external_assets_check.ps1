# Private startup validation; does not access physical USB or player cards.
param(
    [string]$Prepared = 'build\PS2\external-prepared',
    [string]$Data = 'build\PS2-ntsc12\oot-full-next.z64',
    [string]$Emulator = 'C:\Program Files\PCSX2\pcsx2-qt.exe',
    [int]$TimeoutSeconds = 60,
    [switch]$NativeUSB
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$root = Join-Path $project ('build\PS2\external-checks\' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$template = Join-Path $project 'build\PCSX2-diagnostic\PCSX2'
$source = Join-Path $project $Prepared
$image = Join-Path $project $Data
$card = Join-Path $template 'memcards\oot-port.ps2'
$cardBefore = (Get-FileHash -LiteralPath $card).Hash
$reports = @()
function WslPath([string]$path) { '/mnt/'+$path.Substring(0,1).ToLower()+'/'+$path.Substring(3).Replace('\','/') }
function Read-Shared([string]$path) {
    if (!(Test-Path -LiteralPath $path)) { return '' }
    $stream = [IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
$original = [IO.File]::ReadAllBytes((Join-Path $source 'PS2DATA.DAT'))
foreach ($case in 'valid','missing','corrupt','truncated') {
    $folder = Join-Path $root $case
    $dataRoot = Join-Path $folder 'emulator'
    $config = Join-Path $dataRoot 'PCSX2\inis'
    $bios = Join-Path $dataRoot 'PCSX2\bios'
    [IO.Directory]::CreateDirectory($config) | Out-Null
    [IO.Directory]::CreateDirectory($bios) | Out-Null
    Copy-Item (Join-Path $template 'bios\*') $bios
    $ini = [IO.File]::ReadAllText((Join-Path $template 'inis\PCSX2.ini'))
    $ini = $ini -replace '(?m)^(Slot[12]_Enable) = .*$', '$1 = false'
    $ini = $ini -replace '(?m)^(Multitap[12]_Slot[234]_Enable) = .*$', '$1 = false'
    $ini = $ini -replace '(?m)^(StandardVolume|FastForwardVolume) = .*$', '$1 = 0'
    $ini = $ini -replace '(?m)^HostFs = .*$', 'HostFs = true'
    if ($NativeUSB) {
        $usbImage = Join-Path $folder 'usb.img'
        $ini = $ini -replace '(?m)^HostFs = .*$', 'HostFs = false'
        $ini = $ini -replace '(?ms)\[USB1\].*?(?=\[USB2\])', ("[USB1]`nType = Msd`nMsd_subtype = 0`nMsd_ImagePathMsd = $usbImage`n`n")
        $ini = $ini -replace '(?ms)\[USB2\].*?(?=\[)', "[USB2]`nType = None`n`n"
    }
    $ini = $ini -replace '(?m)^StartPaused = .*$', 'StartPaused = false'
    $ini = [regex]::Replace($ini, '(?ms)^\[Pad([1-8])\]\r?\n.*?(?=^\[|\z)', { param($match) "[Pad$($match.Groups[1].Value)]`r`nType = None`r`n`r`n" })
    foreach ($slot in 1,2) { if ($ini -notmatch "(?m)^Slot${slot}_Enable = false\s*$") { throw 'Player cards must be disabled.' } }
    [IO.File]::WriteAllText((Join-Path $config 'PCSX2.ini'),$ini)
    Copy-Item -LiteralPath (Join-Path $source 'OOTPS2.ELF') (Join-Path $folder 'check.elf')
    Copy-Item -LiteralPath $image (Join-Path $folder 'OOT.Z64')
    if ($case -ne 'missing') {
        $bytes = $original.Clone()
        if ($case -eq 'corrupt') { $bytes[$bytes.Length-1] = $bytes[$bytes.Length-1] -bxor 1 }
        if ($case -eq 'truncated') { $bytes = $bytes[0..($bytes.Length-2)] }
        [IO.File]::WriteAllBytes((Join-Path $folder 'PS2DATA.DAT'),$bytes)
    }
    if ($NativeUSB) {
        $fixture = WslPath (Join-Path $project 'tools\ps2_usb_test_image.py')
        $arguments = @($fixture,'create','--image',(WslPath $usbImage),'--data',(WslPath $image))
        if ($case -ne 'missing') { $arguments += @('--native-assets',(WslPath (Join-Path $folder 'PS2DATA.DAT'))) }
        & wsl -d Ubuntu -- /home/lucia/oot-ps2-tools/usb-test-venv/bin/python @arguments
        if ($LASTEXITCODE) { throw 'Private USB fixture creation failed.' }
    }
    $log = Join-Path $folder 'runtime.log'
    $elf = Join-Path $folder 'check.elf'
    $process = Start-Process -FilePath $Emulator -WindowStyle Hidden -PassThru -WorkingDirectory $folder -ArgumentList @('-batch','-nofullscreen','-datapath',('"'+$dataRoot+'"'),'-logfile',('"'+$log+'"'),'-elf',('"'+$elf+'"'))
    $status = 'FAIL'
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        do {
            Start-Sleep -Milliseconds 500
            $text = Read-Shared $log
            if ($text -match '\[PS2 FAULT\]|TLB Miss|Address ?Error|branch in delay slot') { throw "Runtime fault in $case" }
            if ($case -eq 'valid') {
                if ($text -match '\[PS2 EXTERNAL ASSETS\] FAIL') { throw 'Valid native data were rejected.' }
                if ($text -match '\[PS2 EXTERNAL ASSETS\] PASS records=\d+ bytes=\d+' -and $text -match '\[PS2 LOGO TEX\]') { $status='PASS'; break }
            } elseif ($text -match '\[PS2 EXTERNAL ASSETS\] FAIL' -and $text -notmatch '\[PS2 LOGO TEX\]|\[PS2 EXTERNAL ASSETS\] PASS') { $status='PASS'; break }
        } while (!$process.HasExited -and (Get-Date) -lt $deadline)
    } finally {
        $owned = Get-CimInstance Win32_Process -Filter "ProcessId=$($process.Id)"
        if ($owned) {
            if ($owned.Name -ne 'pcsx2-qt.exe' -or !$owned.CommandLine.Contains($elf) -or !$owned.CommandLine.Contains($dataRoot)) { throw 'Process identity changed; refusing to stop it.' }
            Stop-Process -Id $process.Id
            Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
        }
    }
    if ($NativeUSB -and $status -eq 'PASS') {
        $arguments = @($fixture,'extract','--image',(WslPath $usbImage),'--data',(WslPath $image),'--output',(WslPath $folder))
        if ($case -ne 'missing') { $arguments += @('--native-assets',(WslPath (Join-Path $folder 'PS2DATA.DAT'))) }
        & wsl -d Ubuntu -- /home/lucia/oot-ps2-tools/usb-test-venv/bin/python @arguments
        if ($LASTEXITCODE) { throw 'Private USB fixture data changed.' }
    }
    $reports += @{case=$case;status=$status;log=$log}
    if ($status -ne 'PASS') { throw "External startup check failed: $case; log: $log" }
    Write-Output "External startup $case PASS"
}
$cardAfter = (Get-FileHash -LiteralPath $card).Hash
@{status='PASS';cases=$reports;nativeUSBEmulated=[bool]$NativeUSB;playerCardUnchanged=($cardBefore -eq $cardAfter);physicalUSBValidated=$false;realPS2Validated=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $root 'report.json') -Encoding utf8
if ($cardBefore -ne $cardAfter) { throw 'Player card changed.' }
Write-Output "Report: $root"
