param([ValidatePattern('^[A-Za-z]$')][string]$DriveLetter = 'D')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Join-Path $project 'build\PS2\floor-ready'
$volume = Get-Volume -DriveLetter $DriveLetter
if ($volume.DriveType -ne 'Removable' -or $volume.FileSystem -ne 'FAT32') {
    throw 'Expected the selected removable FAT32 USB drive. No format is performed.'
}
$destination = Join-Path ($DriveLetter.ToUpperInvariant()+':\') 'OOTPS2'
$package = Get-Content -LiteralPath (Join-Path $source 'floor-package.json') -Raw | ConvertFrom-Json
if ($package.status -ne 'PACKAGED' -or $package.diagnosticVersion -ne 2 -or
    @($package.files.PSObject.Properties).Count -ne 2) { throw 'Invalid floor package.' }
foreach ($entry in $package.files.PSObject.Properties) {
    if ($entry.Name -notin 'FLOOR.ELF','SUELO.txt') { throw 'Unexpected floor payload.' }
    if ((Get-FileHash -LiteralPath (Join-Path $source $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value) { throw 'Floor package hash mismatch.' }
}
if ((Get-FileHash -LiteralPath (Join-Path $destination 'OOT.Z64')).Hash.ToLowerInvariant() -ne $package.dataSha256) {
    throw 'USB data differs from the floor diagnostic data.'
}
$names = @('FLOOR.ELF','SUELO.txt','FLOOR.sha256','floor-package.json')
$files = @($names | ForEach-Object { Get-Item -LiteralPath (Join-Path $source $_) })
if (($files | Measure-Object Length -Sum).Sum -gt $volume.SizeRemaining) { throw 'Insufficient USB space.' }
# Capture previous physical logs before a new diagnostic can overwrite them.
$capture = Join-Path $project ('build\PS2\hardware-runtime\before-floor-lab-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($capture) | Out-Null
$logs = @()
foreach ($name in 'BOOT.LOG','DIAG.LOG') {
    $oldLog = Join-Path $destination $name
    if (Test-Path -LiteralPath $oldLog) {
        $sha = (Get-FileHash -LiteralPath $oldLog).Hash
        $savedLog = Join-Path $capture $name
        Copy-Item -LiteralPath $oldLog -Destination $savedLog
        if ((Get-FileHash -LiteralPath $savedLog).Hash -ne $sha) { throw 'Physical log backup mismatch.' }
        $logs += @{name=$name;sha256=$sha;bytes=(Get-Item -LiteralPath $oldLog).Length}
    }
}
@{logs=$logs;observation='User reports waves unchanged after the fractional clipping revision; logs may belong to earlier ELF sessions.'} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $capture 'capture.json') -Encoding utf8
$verified = @()
foreach ($file in $files) {
    $target = Join-Path $destination $file.Name
    $sha = (Get-FileHash -LiteralPath $file.FullName).Hash
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target).Hash -ne $sha) {
            Copy-Item -LiteralPath $target -Destination (Join-Path $capture ('previous-'+$file.Name))
        }
    }
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target).Hash -ne $sha) { throw "USB verification failed: $($file.Name)" }
    $verified += @{name=$file.Name;sha256=$sha;bytes=$file.Length}
}
@{status='PASS';destination=$destination;realPS2Validated=$false;files=$verified;logsArchived=$capture} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $project 'build\PS2\floor-copy-report.json') -Encoding utf8
Write-Output "Optional floor diagnostic copied and verified: $destination"
Write-Output "Previous logs preserved: $capture"
