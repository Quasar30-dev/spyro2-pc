# Native 60 fps captures of the elemental powers, demons, melee trails and
# Dark Spyro. Each run writes <name>-60fps.mp4, a final <name>.bmp still and
# the workbench's <name>.json report into data/validation/elements.
# Usage: tools/capture-elements.ps1 [-Only <name-substring>] [-Paced]
param([string]$Only = '', [switch]$Paced)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build/Release/spyro_workbench.exe'
# Relative to the working directory: Start-Process splits arguments at spaces.
$out = 'data/validation/elements'
New-Item -ItemType Directory -Force (Join-Path $root $out) | Out-Null
# Glimmer record 0: a small lizard on open grass beside the gemcutters.
$spot = @('--game', 'data/disc', '--slot', '17', '--interaction-record', '0', '--headless')
$side = @('--camera-orbit', '60')
$wide = @('--camera-distance', '7', '--camera-height', '3', '--camera-orbit', '150')
$elements = 'fire', 'water', 'ice', 'electricity', 'convexity'
$runs = [System.Collections.Generic.List[object]]::new()
foreach ($e in $elements) {
    $runs.Add(@{ name = "$e-breath"; frames = 300; args = @('--element-replay', 'breath', '--element', $e) + $side })
    $runs.Add(@{ name = "$e-shot"; frames = 300; args = @('--element-replay', 'shot', '--element', $e) + $side })
    $runs.Add(@{ name = "$e-headbash"; frames = 280; args = @('--element-replay', 'headbash', '--element', $e, '--camera-height', '2.5', '--camera-orbit', '30') })
    $runs.Add(@{ name = "$e-fury"; frames = 420; args = @('--element-replay', 'fury', '--element', $e) + $wide })
}
$runs.Add(@{ name = 'melee-trails'; frames = 160; args = @('--element-replay', 'melee') + $side })
$runs.Add(@{ name = 'melee-trails-dark'; frames = 160; args = @('--element-replay', 'melee', '--dark-spyro') + $side })
$runs.Add(@{ name = 'melee-trails-ice'; frames = 160; args = @('--element-replay', 'melee', '--element', 'ice') + $side })
$runs.Add(@{ name = 'convexity-demons'; frames = 420; args = @('--element-replay', 'demons', '--camera-distance', '6', '--camera-height', '2.5', '--camera-orbit', '120') })
$runs.Add(@{ name = 'convexity-slice'; frames = 420; args = @('--element-replay', 'slice', '--camera-distance', '6', '--camera-height', '2', '--camera-orbit', '120') })
$runs.Add(@{ name = 'dark-run'; frames = 300; args = @('--element-replay', 'dark-run', '--dark-spyro') })
$runs.Add(@{ name = 'dark-run-no-smoke'; frames = 300; args = @('--element-replay', 'dark-run', '--dark-spyro', '--no-dark-smoke') })
$runs.Add(@{ name = 'element-tour'; frames = 1500; args = @('--element-replay', 'tour', '--elements') + $side })
# Dark Spyro stills from four sides (front, oblique, overhead, reverse).
$views = @{ front = @('--camera-orbit', '180'); oblique = @('--camera-orbit', '135', '--camera-height', '1.2'); overhead = @('--camera-height', '6', '--camera-distance', '2'); reverse = @('--camera-orbit', '0') }
foreach ($v in $views.Keys) { $runs.Add(@{ name = "dark-spyro-$v"; frames = 90; still = $true; args = @('--dark-spyro') + $views[$v] }) }

$failed = 0
foreach ($run in $runs) {
    if ($Only -and -not $run.name.Contains($Only)) { continue }
    $arguments = $spot + @('--frames', "$($run.frames)") + $run.args + @('--capture', "$out/$($run.name).bmp")
    if (-not $run.still) { $arguments += @('--video', "$out/$($run.name)-60fps.mp4") }
    if ($Paced) { $arguments += '--paced' }
    Remove-Item (Join-Path $root 'data/validation/workbench-error.txt') -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -NoNewWindow -Wait -PassThru
    if ($p.ExitCode -ne 0) {
        $failed++; $why = Get-Content (Join-Path $root 'data/validation/workbench-error.txt') -ErrorAction SilentlyContinue
        Write-Output "FAILED $($run.name): $why"
    } else { Write-Output "ok $($run.name)" }
}
if ($failed) { throw "$failed element captures failed" }
