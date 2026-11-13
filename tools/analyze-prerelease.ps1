param([ValidateSet('july-1999','august-1999','winter-jampack-1999')][string]$Build)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskConfig = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'local.json') -Raw | ConvertFrom-Json
$env:JAVA_HOME = $taskConfig.java
$taskBuildRoot = Join-Path $taskRoot "data/builds/$Build"
$taskManifest = Get-Content -LiteralPath (Join-Path $taskBuildRoot 'manifest.json') -Raw | ConvertFrom-Json
$taskExecutable = $taskManifest.executables | Where-Object { $_.name -match '(^|/)(SPYRO2|DEMO2)\.EXE$' } | Select-Object -First 1
if (-not $taskExecutable) { throw 'Spyro executable was not inventoried' }
$taskEntry = '{0:x8}' -f [uint32]$taskExecutable.entry_pc
$taskBase = '0x{0:x8}' -f [uint32]$taskExecutable.load_address
$taskPayload = if ($Build -eq 'winter-jampack-1999') { 'exe/DEMO2.EXE.bin' } else { 'exe/main.bin' }
$taskProjectName = 'Spyro2_' + $Build.Replace('-','_')
if ($Build -eq 'winter-jampack-1999') { $taskProjectName += '_native' }
$taskOut = Join-Path $taskRoot "data/analysis/prerelease-20260930/$Build"
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
& (Join-Path $taskConfig.ghidra 'support/analyzeHeadless.bat') (Join-Path $taskRoot 'data/ghidra') $taskProjectName '-import' (Join-Path $taskBuildRoot $taskPayload) '-loader' 'BinaryLoader' '-loader-baseAddr' $taskBase '-processor' 'MIPS:LE:32:default' '-cspec' 'default' '-scriptPath' (Join-Path $PSScriptRoot 'ghidra') '-preScript' 'PrepareBuild.java' $taskEntry '-analysisTimeoutPerFile' '300' '-postScript' 'AuditMapping.java' $taskOut '-log' (Join-Path $taskOut 'headless.log') *> (Join-Path $taskOut 'console.log')
if ($LASTEXITCODE -ne 0) { throw "Ghidra prerelease analysis failed: $Build" }
