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
    if ($entry.Name -notin 'OOTPS2.ELF','DIAG.ELF','DIAG224.ELF','BOOTTEST.ELF','OOT.Z64','ESP.DAT') { throw 'Unexpected package payload.' }
    if ((Get-FileHash -LiteralPath (Join-Path $source $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value) { throw 'Package payload hash mismatch.' }
}
$ownedNames = @('OOTPS2.ELF','DIAG.ELF','DIAG224.ELF','BOOTTEST.ELF','OOT.Z64','package.json','HASHES.sha256','LEEME.txt','BUGS.txt')
if ($package.files.PSObject.Properties.Name -contains 'ESP.DAT') { $ownedNames += 'ESP.DAT','PS2_LANGUAGE.md' }
$files = @($ownedNames | ForEach-Object { Get-Item -LiteralPath (Join-Path $source $_) })
if (($files | Measure-Object Length -Sum).Sum -gt $volume.SizeRemaining) { throw 'USB does not have enough free space.' }
[IO.Directory]::CreateDirectory($destination) | Out-Null
$backup = Join-Path $driveRoot ('OOTPS2-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$verified = @()
foreach ($file in $files) {
    $target = Join-Path $destination $file.Name
    $expected = (Get-FileHash -LiteralPath $file.FullName).Hash
    $existingHash = if (Test-Path -LiteralPath $target) { (Get-FileHash -LiteralPath $target).Hash } else { $null }
    if ($existingHash -and $existingHash -ne $expected) {
        [IO.Directory]::CreateDirectory($backup) | Out-Null
        Copy-Item -LiteralPath $target -Destination (Join-Path $backup $file.Name)
    }
    if ($existingHash -ne $expected) { Copy-Item -LiteralPath $file.FullName -Destination $target }
    if ((Get-FileHash -LiteralPath $target).Hash -ne $expected) { throw "USB verification failed: $($file.Name)" }
    $verified += @{name=$file.Name;sha256=$expected;bytes=$file.Length;unchanged=($existingHash -eq $expected)}
}
@{status='PASS';drive=$driveRoot;label=$volume.FileSystemLabel;filesystem=$volume.FileSystem;
  destination=$destination;realPS2Validated=$false;files=$verified;
  backup=if(Test-Path -LiteralPath $backup){$backup}else{$null}} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $project 'build\PS2\usb-copy-report.json') -Encoding utf8
Write-Output "USB copied and verified: $destination ($($files.Count) files)"
