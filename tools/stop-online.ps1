$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskStatePath = Join-Path $taskRoot 'data/online/processes.json'
if (-not (Test-Path -LiteralPath $taskStatePath)) { Write-Output 'No relay started by this launcher.'; return }
$taskState = Get-Content -LiteralPath $taskStatePath -Raw | ConvertFrom-Json
foreach ($taskKind in @('tunnel','relay')) {
    $taskProcess = Get-Process -Id $taskState.$taskKind -ErrorAction SilentlyContinue
    $taskStart = $taskState.($taskKind + 'Start')
    if ($taskProcess -and $taskProcess.StartTime.ToUniversalTime().ToString('o') -eq $taskStart) { Stop-Process -Id $taskProcess.Id }
}
Remove-Item -LiteralPath $taskStatePath
Write-Output 'Temporary online relay stopped.'
