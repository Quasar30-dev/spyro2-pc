param([string]$Source = 'build/misc-candidate/Release/spyro_workbench.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskSource = if ([IO.Path]::IsPathRooted($Source)) { $Source } else { Join-Path $taskRoot $Source }
$taskSource = (Resolve-Path -LiteralPath $taskSource).Path
$taskDestination = Join-Path $taskRoot 'build/Release/spyro_workbench.exe'
if ($taskSource -eq $taskDestination) { throw 'Source is already the main executable.' }
# Install immediately after compilation, before validation. A running window
# retains its loaded image; the next launch always receives the new build.
try { Copy-Item -LiteralPath $taskSource -Destination $taskDestination -Force }
catch [System.IO.IOException] {
    $taskRetired = Join-Path $taskRoot ('build/Release/spyro_workbench-running-' + (Get-Date -Format 'yyyyMMdd-HHmmssfff') + '.exe')
    Move-Item -LiteralPath $taskDestination -Destination $taskRetired
    try { Copy-Item -LiteralPath $taskSource -Destination $taskDestination }
    catch { Move-Item -LiteralPath $taskRetired -Destination $taskDestination; throw }
}
$taskHash = (Get-FileHash -LiteralPath $taskDestination -Algorithm SHA256).Hash
if ($taskHash -ne (Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash) { throw 'Installed executable differs from the compiled source.' }
Write-Output "Installed main executable: $taskDestination"
Write-Output "SHA256: $taskHash"
