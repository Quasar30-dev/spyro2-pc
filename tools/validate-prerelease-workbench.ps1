param([uint32]$Frames = 360)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOutput = Join-Path $taskRoot 'data/analysis/prerelease-20260930/runtime'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskBuilds = @(
    @{ name='july'; source='data/builds/july-1999/source-disc/SPYRO2EXE.cue'; slots=(15..69 | Where-Object { $_ % 2 }) },
    @{ name='august'; source='data/builds/august-1999/source-disc/PSX - Spyro 2 8-17 Previewable.cue'; slots=(15..71 | Where-Object { $_ % 2 }) },
    @{ name='jampack'; source="data/builds/winter-jampack-1999/source-disc/PlayStation Underground Jampack - Winter '99 (USA).cue"; slots=@(27,35) }
)
$taskResults = @()
foreach ($taskBuild in $taskBuilds) {
    foreach ($taskSlot in $taskBuild.slots) {
        $taskCapture = Join-Path $taskOutput ($taskBuild.name + '-' + $taskSlot + '.bmp')
        $taskArguments = @('--game', ('"' + (Join-Path $taskRoot $taskBuild.source) + '"'), '--slot', $taskSlot,
            '--headless', '--frames', $Frames, '--mute', '--replay', '--no-capture', '--capture', ('"' + $taskCapture + '"'))
        $taskProcess = Start-Process -FilePath (Join-Path $taskRoot 'build/Release/spyro_workbench.exe') -WorkingDirectory $taskRoot -ArgumentList $taskArguments -WindowStyle Hidden -PassThru
        if (-not $taskProcess.WaitForExit(30000)) { $taskProcess.Kill(); $taskResult = 'Timed out after 30 seconds' }
        else {
            $taskProcess.Refresh()
            $taskResult = if ($taskProcess.ExitCode -eq 0) { 'OK' } else { Get-Content -LiteralPath (Join-Path $taskRoot 'data/validation/workbench-error.txt') -Raw }
        }
        $taskResults += [pscustomobject]@{ build=$taskBuild.name; slot=$taskSlot; result=$taskResult.Trim() }
        $taskResults | Export-Csv -Delimiter "`t" -NoTypeInformation -LiteralPath (Join-Path $taskOutput 'results.tsv')
        Write-Output ($taskBuild.name + '/' + $taskSlot + ': ' + $taskResult.Trim())
    }
}
if ($taskResults | Where-Object result -ne 'OK') { exit 1 }
