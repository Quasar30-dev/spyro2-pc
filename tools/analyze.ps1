param([string]$GhidraHome = $env:GHIDRA_HOME, [string]$JavaHome = $env:JAVA_HOME, [switch]$AllFunctions, [switch]$ExportOnly)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskConfigPath = Join-Path $PSScriptRoot 'local.json'
if (Test-Path -LiteralPath $taskConfigPath) {
    $taskConfig = Get-Content -LiteralPath $taskConfigPath -Raw | ConvertFrom-Json
    if (-not $GhidraHome) { $GhidraHome = $taskConfig.ghidra }
    if (-not $JavaHome) { $JavaHome = $taskConfig.java }
}
if (-not (Test-Path -LiteralPath "$GhidraHome/support/analyzeHeadless.bat")) { throw 'Pass -GhidraHome or set GHIDRA_HOME to a Ghidra installation.' }
if ($JavaHome) { $env:JAVA_HOME = $JavaHome }
$taskProject = Join-Path $taskRoot 'data/ghidra'
$taskReport = Join-Path $taskRoot 'data/analysis/main'
New-Item -ItemType Directory -Force -Path $taskProject,$taskReport | Out-Null
$taskArguments = @($taskProject, 'Spyro2_US', '-import', "$taskRoot/data/exe/main.bin", '-overwrite',
    '-loader', 'BinaryLoader', '-loader-baseAddr', '0x80010000', '-processor', 'MIPS:LE:32:default',
    '-cspec', 'default', '-scriptPath', "$taskRoot/tools/ghidra", '-preScript', 'PrepareSpyro2.java',
    "$taskRoot/research/symbols-us.tsv", '-analysisTimeoutPerFile', '600', '-postScript', 'ExportSpyro2.java', $taskReport)
if($ExportOnly) {
    $taskArguments = @($taskProject, 'Spyro2_US', '-process', 'main.bin', '-noanalysis', '-readOnly',
        '-scriptPath', "$taskRoot/tools/ghidra", '-postScript', 'ExportSpyro2.java', $taskReport)
}
if ($AllFunctions) { $taskArguments += 'all' }
$taskArguments += @('-log', "$taskRoot/data/analysis/headless.log", '-scriptlog', "$taskRoot/data/analysis/scripts.log")
& "$GhidraHome/support/analyzeHeadless.bat" @taskArguments
if ($LASTEXITCODE -ne 0) { throw "Ghidra failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath "$taskReport/summary.json")) { throw 'Ghidra did not produce an analysis summary. Inspect data/analysis/headless.log.' }
