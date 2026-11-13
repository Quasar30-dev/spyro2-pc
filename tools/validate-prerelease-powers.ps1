param([uint32]$Frames = 240, [switch]$AllLevels)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOutput = Join-Path $taskRoot 'data/analysis/prerelease-20260930/power-import-fix/runtime'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskBuilds = @(
    @{ name='july'; source='data/builds/july-1999/source-disc/SPYRO2EXE.cue'; slots=(15..69 | Where-Object { $_ % 2 }); sample=15 },
    @{ name='august'; source='data/builds/august-1999/source-disc/PSX - Spyro 2 8-17 Previewable.cue'; slots=(15..71 | Where-Object { $_ % 2 }); sample=15 },
    @{ name='jampack'; source="data/builds/winter-jampack-1999/source-disc/PlayStation Underground Jampack - Winter '99 (USA).cue"; slots=@(27,35); sample=35 }
)
$taskResults = @()
foreach ($taskBuild in $taskBuilds) {
    $taskSlots = if ($AllLevels) { $taskBuild.slots } else { @($taskBuild.sample) }
    foreach ($taskSlot in $taskSlots) {
        $taskCapture = Join-Path $taskOutput ($taskBuild.name + '-' + $taskSlot + '.bmp')
        $taskReport = [IO.Path]::ChangeExtension($taskCapture, '.json')
        $taskArgs = @('--game', ('"' + (Join-Path $taskRoot $taskBuild.source) + '"'), '--slot', $taskSlot,
            '--headless', '--frames', $Frames, '--mute', '--replay', '--supercharge', 'retail', '--green-firebomb',
            '--retail-powerups', ('"' + (Join-Path $taskRoot 'data/disc') + '"'), '--no-capture', '--capture', ('"' + $taskCapture + '"'))
        $taskProcess = Start-Process -FilePath (Join-Path $taskRoot 'build/Release/spyro_workbench.exe') -WorkingDirectory $taskRoot -ArgumentList $taskArgs -WindowStyle Hidden -PassThru
        $taskResult = 'OK'
        if (-not $taskProcess.WaitForExit(60000)) { $taskProcess.Kill(); $taskResult = 'Timeout' }
        else {
            $taskProcess.Refresh()
            if ($taskProcess.ExitCode -ne 0) { $taskResult = (Get-Content -LiteralPath (Join-Path $taskRoot 'data/validation/workbench-error.txt') -Raw).Trim() }
            else {
                $taskData = Get-Content -LiteralPath $taskReport -Raw | ConvertFrom-Json
                if (-not $taskData.retail_powerups_imported -or $taskData.unbound_return_portals_visible) { $taskResult = 'Missing retail powers or visible orphan portal' }
            }
        }
        $taskResults += [pscustomobject]@{ build=$taskBuild.name; slot=$taskSlot; result=$taskResult }
        $taskResults | Export-Csv -Delimiter "`t" -NoTypeInformation -LiteralPath (Join-Path $taskOutput 'results.tsv')
        Write-Output ($taskBuild.name + '/' + $taskSlot + ': ' + $taskResult)
    }
}
if ($taskResults | Where-Object result -ne 'OK') { exit 1 }
