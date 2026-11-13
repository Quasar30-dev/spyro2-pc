param([string]$Output = 'data/analysis/mapping-audit-20260930')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskConfig = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'local.json') -Raw | ConvertFrom-Json
$env:JAVA_HOME = $taskConfig.java
$taskAudit = Join-Path $taskRoot $Output
New-Item -ItemType Directory -Force -Path $taskAudit | Out-Null
foreach ($taskProject in Get-ChildItem -LiteralPath (Join-Path $taskRoot 'data/ghidra') -Filter '*.gpr') {
    $taskName = $taskProject.BaseName
    $taskOut = Join-Path $taskAudit $taskName
    New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
    & (Join-Path $taskConfig.ghidra 'support/analyzeHeadless.bat') (Join-Path $taskRoot 'data/ghidra') $taskName '-process' '-noanalysis' '-readOnly' '-scriptPath' (Join-Path $PSScriptRoot 'ghidra') '-postScript' 'AuditMapping.java' $taskOut '-log' (Join-Path $taskOut 'headless.log') *> (Join-Path $taskOut 'console.log')
    if ($LASTEXITCODE -ne 0) { throw "Ghidra audit failed: $taskName" }
    Write-Output "Audited $taskName"
}
