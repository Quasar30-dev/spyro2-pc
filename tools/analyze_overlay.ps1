param([int]$Slot = 1, [string]$GhidraHome = $env:GHIDRA_HOME, [string]$JavaHome = $env:JAVA_HOME,
    [string[]]$EntryPoints = @(), [string[]]$ExportFunctions = @(), [string[]]$DumpRanges = @())
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskConfigPath = Join-Path $PSScriptRoot 'local.json'
if (Test-Path -LiteralPath $taskConfigPath) {
    $taskConfig = Get-Content -LiteralPath $taskConfigPath -Raw | ConvertFrom-Json
    if (-not $GhidraHome) { $GhidraHome = $taskConfig.ghidra }
    if (-not $JavaHome) { $JavaHome = $taskConfig.java }
}
if (-not $GhidraHome) { throw 'Set GHIDRA_HOME or tools/local.json' }
if ($JavaHome) { $env:JAVA_HOME = $JavaHome }
$taskName = 'overlay_{0:d3}' -f $Slot
$taskBinary = "$taskRoot/data/overlays/$taskName.bin"
python "$PSScriptRoot/spyrodisc.py" wad-extract "$taskRoot/data/disc/WAD.WAD" $Slot $taskBinary
if ($LASTEXITCODE -ne 0) { throw 'Overlay extraction failed' }
$taskReport = "$taskRoot/data/analysis/$taskName"
New-Item -ItemType Directory -Force -Path "$taskRoot/data/ghidra",$taskReport | Out-Null
if ($DumpRanges.Count % 2) { throw 'DumpRanges requires start / exclusive-end pairs.' }
foreach ($taskAddress in @($EntryPoints)+@($ExportFunctions)+@($DumpRanges)) {
    if ($taskAddress -notmatch '^[0-9a-fA-F]{8}$') { throw "Expected an eight-digit hexadecimal address: $taskAddress" }
}
$taskArguments = @("$taskRoot/data/ghidra", "Spyro2_$taskName", '-import', $taskBinary, '-overwrite',
    '-loader', 'BinaryLoader', '-loader-baseAddr', '0x8006d264', '-processor', 'MIPS:LE:32:default',
    '-cspec', 'default', '-scriptPath', "$PSScriptRoot/ghidra", '-preScript', 'PrepareOverlay.java',
    "$taskRoot/data/exe/main.bin") + $EntryPoints + @('-analysisTimeoutPerFile', '600')
if ($ExportFunctions.Count) {
    $taskArguments += @('-postScript', 'ExportSelected.java', "$taskReport/selected") + $ExportFunctions
} else { $taskArguments += @('-postScript', 'ExportSpyro2.java', $taskReport, 'all') }
if ($DumpRanges.Count) {
    $taskArguments += @('-postScript', 'DumpMipsRanges.java', "$taskReport/disassembly.txt") + $DumpRanges
}
$taskArguments += @('-log', "$taskReport/headless.log")
& "$GhidraHome/support/analyzeHeadless.bat" @taskArguments
if ($LASTEXITCODE -ne 0) { throw "Overlay analysis failed with exit code $LASTEXITCODE" }
