# Decompile selected moby classes of a level overlay case by case.
# Ranges come from data/analysis/moby-dispatch/dispatch.json (tools/moby_dispatch.py).
# Usage: tools/export-moby-cases.ps1 -Slot 21 -Classes 111,118,11a
param([Parameter(Mandatory)][int]$Slot, [Parameter(Mandatory)][string[]]$Classes, [string]$GhidraHome = $env:GHIDRA_HOME, [string]$JavaHome = $env:JAVA_HOME)
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
$overlay = '{0:d3}' -f ($Slot - 1)
$binary = Join-Path $root "data/overlays/overlay_$overlay.bin"
$out = Join-Path $root "data/analysis/moby-cases/overlay_$overlay"
New-Item -ItemType Directory -Force $out | Out-Null
$ranges = @()
foreach ($class in ($Classes -split ',')) {
    $entry = $level.classes.$class
    if (-not $entry) { Write-Output "class $class has no dispatch case"; continue }
    $ranges += @($entry.start, $entry.end, "class_$class")
}
if (-not $ranges.Count) { throw 'Nothing to export' }
$project = Join-Path $root 'data/ghidra-cases'
New-Item -ItemType Directory -Force $project | Out-Null
$args = @($project, "cases_$overlay", '-import', $binary, '-overwrite', '-deleteProject', '-noanalysis',
    '-loader', 'BinaryLoader', '-loader-baseAddr', '0x8006d264', '-processor', 'MIPS:LE:32:default', '-cspec', 'default',
    '-scriptPath', (Join-Path $PSScriptRoot 'ghidra'), '-preScript', 'PrepareOverlay.java', (Join-Path $root 'data/exe/main.bin'), $level.dispatcher,
    '-postScript', 'ExportCaseRanges.java', $out) + $ranges + @('-log', (Join-Path $out 'headless.log'))
& "$GhidraHome/support/analyzeHeadless.bat" @args | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Ghidra export failed ($LASTEXITCODE); see $out/headless.log" }
Get-ChildItem $out -Filter 'class_*.c' | Select-Object Name, Length
