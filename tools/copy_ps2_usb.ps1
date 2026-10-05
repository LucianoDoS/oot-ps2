param([ValidatePattern('^[A-Za-z]$')][string]$DriveLetter = 'D')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$source = Join-Path $project 'build\PS2\usb-ready\OOTPS2'
$volume = Get-Volume -DriveLetter $DriveLetter
if ($volume.DriveType -ne 'Removable' -or $volume.FileSystem -ne 'FAT32') {
    throw 'Expected the selected removable FAT32 USB drive. No format is performed.'
}
$driveRoot = $DriveLetter.ToUpperInvariant()+':\'
$destination = Join-Path $driveRoot 'OOTPS2'
$package = Get-Content -LiteralPath (Join-Path $source 'package.json') -Raw | ConvertFrom-Json
foreach ($entry in $package.files.PSObject.Properties) {
    if ($entry.Name -notin 'OOTPS2.ELF','BOOTTEST.ELF','OOT.Z64') { throw 'Unexpected package payload.' }
    if ((Get-FileHash -LiteralPath (Join-Path $source $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value) { throw 'Package payload hash mismatch.' }
}
$files = @(Get-ChildItem -LiteralPath $source -File)
if (($files | Measure-Object Length -Sum).Sum -gt $volume.SizeRemaining) { throw 'USB does not have enough free space.' }
[IO.Directory]::CreateDirectory($destination) | Out-Null
$backup = Join-Path $driveRoot ('OOTPS2-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$verified = @()
foreach ($file in $files) {
    $target = Join-Path $destination $file.Name
    $expected = (Get-FileHash -LiteralPath $file.FullName).Hash
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -ne $expected) {
        [IO.Directory]::CreateDirectory($backup) | Out-Null
        Copy-Item -LiteralPath $target -Destination (Join-Path $backup $file.Name)
    }
    Copy-Item -LiteralPath $file.FullName -Destination $target
    if ((Get-FileHash -LiteralPath $target).Hash -ne $expected) { throw "USB verification failed: $($file.Name)" }
    $verified += @{name=$file.Name;sha256=$expected;bytes=$file.Length}
}
@{status='PASS';drive=$driveRoot;label=$volume.FileSystemLabel;filesystem=$volume.FileSystem;
  destination=$destination;realPS2Validated=$false;files=$verified;
  backup=if(Test-Path -LiteralPath $backup){$backup}else{$null}} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $project 'build\PS2\usb-copy-report.json') -Encoding utf8
Write-Output "USB copied and verified: $destination ($($files.Count) files)"
