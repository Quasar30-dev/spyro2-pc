# Decompile explicit address ranges of a level overlay (moby class cases found
# with tools/find_class_case.py) through ExportCaseRanges.java.
# Usage: tools/export-case-ranges.ps1 -Slot 21 -Ranges 8007a2e8,8007c65c,class_10f,8007da28,8007f4d8,class_113
# Output: data/analysis/moby-cases/overlay_NNN/<label>.c (one Ghidra run per call; a large case takes minutes).
param([Parameter(Mandatory)][int]$Slot, [Parameter(Mandatory)][string[]]$Ranges, [string]$GhidraHome = $env:GHIDRA_HOME, [string]$JavaHome = $env:JAVA_HOME)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$config = Join-Path $PSScriptRoot 'local.json'
if (Test-Path -LiteralPath $config) {
    $c = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
    if (-not $GhidraHome) { $GhidraHome = $c.ghidra }
    if (-not $JavaHome) { $JavaHome = $c.java }
}
if ($JavaHome) { $env:JAVA_HOME = $JavaHome }
$dispatch = Get-Content (Join-Path $root 'data/analysis/moby-dispatch/dispatch.json') -Raw | ConvertFrom-Json
$level = $dispatch.$Slot
if (-not $level) { throw "No dispatch map for slot $Slot (run tools/moby_dispatch.py)" }
$items = @($Ranges -split ',')
if ($items.Count % 3 -ne 0) { throw 'Ranges are start,end,label triples' }
$overlay = '{0:d3}' -f ($Slot - 1)
$binary = Join-Path $root "data/overlays/overlay_$overlay.bin"
$out = Join-Path $root "data/analysis/moby-cases/overlay_$overlay"
New-Item -ItemType Directory -Force $out | Out-Null
$project = Join-Path $root 'data/ghidra-cases'
New-Item -ItemType Directory -Force $project | Out-Null
$name = "cases_${overlay}_$PID"
$log = Join-Path $out "headless-$PID.log"
$args = @($project, $name, '-import', $binary, '-overwrite', '-deleteProject', '-noanalysis',
    '-loader', 'BinaryLoader', '-loader-baseAddr', '0x8006d264', '-processor', 'MIPS:LE:32:default', '-cspec', 'default',
    '-scriptPath', (Join-Path $PSScriptRoot 'ghidra'), '-preScript', 'PrepareOverlay.java', (Join-Path $root 'data/exe/main.bin'), $level.dispatcher,
    '-postScript', 'ExportCaseRanges.java', $out) + $items + @('-log', $log)
& "$GhidraHome/support/analyzeHeadless.bat" @args | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Ghidra export failed ($LASTEXITCODE); see $log" }
Get-ChildItem $out -Filter '*.c' | Select-Object Name, Length
