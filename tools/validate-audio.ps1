param([string]$Game, [switch]$NoBuild, [switch]$SkipPlayback)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $NoBuild) { & "$PSScriptRoot/build.ps1" }
if (-not $Game) {
    $taskCue = Get-ChildItem -LiteralPath $taskRoot -Filter '*.cue' -File -Recurse | Select-Object -First 1
    if (-not $taskCue) { throw 'Pass -Game with your CUE, BIN or extracted directory containing full XA sectors.' }
    $Game = $taskCue.FullName
}
$taskGame = (Resolve-Path -LiteralPath $Game).Path
$taskReport = Join-Path $taskRoot 'data/validation'
New-Item -ItemType Directory -Force -Path $taskReport | Out-Null
& "$taskRoot/build/Release/spyro_audio_probe.exe" $taskGame --all > "$taskReport/audio-retail-full.jsonl"
if ($LASTEXITCODE -ne 0) { throw 'Retail sound-bank or music decoding failed.' }
& "$taskRoot/build/Release/spyro_audio_probe.exe" $taskGame --all --npcs > "$taskReport/hunter-voices.jsonl"
if ($LASTEXITCODE -ne 0) { throw 'Retail Hunter speech decoding failed.' }
if (-not $SkipPlayback) {
    & "$taskRoot/build/Release/spyro_audio_playback_probe.exe" $taskGame > "$taskReport/audio-playback.json"
    if ($LASTEXITCODE -ne 0) { throw 'Windows playback, pause, cancellation or level-switch check failed.' }
    Get-Content -LiteralPath "$taskReport/audio-playback.json"
}
Get-Content -LiteralPath "$taskReport/audio-retail-full.jsonl" -Tail 1
