param([string]$Python='python',[int]$Seconds=75)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'run_ps2_usb_stack_check.ps1') -Elf 'build\PS2-ntsc12\oot_ps2_usb_locale_test.elf' -Seconds $Seconds
$root=Get-ChildItem -LiteralPath (Join-Path $project 'build\PS2\usb-stack-checks') -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$report=Get-Content -LiteralPath (Join-Path $root.FullName 'report.json') -Raw | ConvertFrom-Json
$expected=(Get-FileHash -LiteralPath (Join-Path $project 'build\PS2-ntsc12\oot_ps2_usb_locale_test.elf')).Hash
if ($report.elfSha256 -ne $expected) { throw 'The selected report belongs to another ELF.' }
& $Python (Join-Path $PSScriptRoot 'verify_ps2_locale_game.py') $root.FullName
if ($LASTEXITCODE) { throw 'Language game checks failed.' }
