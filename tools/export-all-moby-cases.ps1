# Decompile every unmapped enemy class case (data/analysis/level-systems/coverage.json)
# one overlay at a time. Long-running: each case may take up to 15 minutes.
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$coverage = Get-Content (Join-Path $root 'data/analysis/level-systems/coverage.json') -Raw | ConvertFrom-Json
foreach ($level in $coverage) {
    if (-not $level.unimplemented_enemy_classes -or -not $level.unimplemented_enemy_classes.Count) { continue }
    $classes = $level.unimplemented_enemy_classes -join ','
    Write-Output "slot $($level.slot) $($level.name): $classes"
    & (Join-Path $PSScriptRoot 'export-moby-cases.ps1') -Slot $level.slot -Classes $level.unimplemented_enemy_classes 2>&1 | Out-String | Write-Output
}
