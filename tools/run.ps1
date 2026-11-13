param([string]$Game, [int]$Slot=15, [switch]$Play, [switch]$Workbench, [switch]$Universe, [UInt64]$Seed=2026, [UInt64]$World=0, [switch]$Space, [switch]$Spyro3)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskExecutable = Join-Path $taskRoot 'build/Release/spyro_workbench.exe'
$taskVerified = Join-Path $taskRoot 'build/Release/spyro_workbench_ready.exe'
# A verified update can be staged while Windows holds the running binary.
# A later normal build takes precedence again when its timestamp is newer.
if ((Test-Path -LiteralPath $taskVerified) -and
    ((-not (Test-Path -LiteralPath $taskExecutable)) -or
     (Get-Item -LiteralPath $taskVerified).LastWriteTimeUtc -gt (Get-Item -LiteralPath $taskExecutable).LastWriteTimeUtc)) {
    $taskExecutable = $taskVerified
}
if (-not (Test-Path -LiteralPath $taskExecutable)) { & "$PSScriptRoot/build.ps1" }
$taskArguments = @('--slot', $Slot)
if ($Game) { $taskArguments += @('--game', $Game) }
if ($Play) { $taskArguments += '--play' }
if ($Workbench) { $taskArguments += '--workbench' }
# -Spyro3: the Spyro 3: Year of the Dragon edition (own title, menus, PC locations and saves).
if ($Spyro3) { $taskArguments += '--spyro3' }
if ($Universe) { $taskArguments += @('--universe', $Seed.ToString(), '--world', $World.ToString()) }
if ($Universe -and $Space) { $taskArguments += '--space' }
Push-Location -LiteralPath $taskRoot
try { & $taskExecutable @taskArguments } finally { Pop-Location }
