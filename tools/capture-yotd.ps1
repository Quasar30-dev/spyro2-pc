param(
    [string]$Game = 'data-yotd',
    [string]$Donor = 'data/disc',
    [string]$Output = 'captures-yotd'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$taskExe = Join-Path $taskRoot 'build/Release/spyro_workbench.exe'
if (!(Test-Path -LiteralPath $taskExe)) { throw 'Build and install the main workbench first.' }
New-Item -ItemType Directory -Path $Output -Force | Out-Null
# All takes use the actual native gameplay controller and the installed main.
# Camera overrides change presentation only; they never move the player.
$taskCases = @(
    @{ Name='sunrise'; Slot=97; Extra=@() },
    @{ Name='sunny'; Slot=99; Extra=@() },
    @{ Name='midday'; Slot=115; Extra=@() },
    @{ Name='evening'; Slot=133; Extra=@() },
    @{ Name='midnight'; Slot=151; Extra=@() },
    @{ Name='sunny-area1'; Slot=99; Extra=@('--yotd-area','1') },
    @{ Name='dino-area3'; Slot=159; Extra=@('--yotd-area','3') },
    @{ Name='mushroom-speedway'; Slot=107; Extra=@() },
    @{ Name='sheila'; Slot=109; Extra=@() },
    @{ Name='agent9'; Slot=163; Extra=@() },
    @{ Name='crawdad'; Slot=113; Extra=@() },
    @{ Name='sunny-overhead'; Slot=99; Extra=@('--camera-distance','9','--camera-height','16') },
    @{ Name='sunny-downward'; Slot=99; Extra=@('--camera-distance','4','--camera-height','6') },
    @{ Name='sunny-oblique'; Slot=99; Extra=@('--camera-orbit','65','--camera-distance','9','--camera-height','3') },
    @{ Name='sunny-reverse'; Slot=99; Extra=@('--camera-orbit','180') },
    @{ Name='sunrise-walk'; Slot=97; Frames=360; Extra=@('--replay') },
    @{ Name='sunrise-water'; Slot=97; Frames=85; Extra=@('--upgrade-tier','2','--elements','--element','water','--element-replay','breath') },
    @{ Name='sunny-electric'; Slot=99; Frames=85; Extra=@('--upgrade-tier','2','--elements','--element','electricity','--element-replay','breath') }
)
$taskCommands = @()
foreach ($taskCase in $taskCases) {
    $taskFrames = if ($taskCase.ContainsKey('Frames')) { $taskCase.Frames } else { 120 }
    $taskCapture = Join-Path $Output ($taskCase.Name + '.bmp')
    $taskArgs = @('--game',$Game,'--retail-powerups',$Donor,'--slot',[string]$taskCase.Slot,
        '--play','--headless','--frames',[string]$taskFrames,'--capture-fullscreen',
        '--capture-size','1280','720','--capture',$taskCapture) + $taskCase.Extra
    $taskCommands += @{ name=$taskCase.Name; executable=$taskExe; arguments=$taskArgs }
    & $taskExe @taskArgs *> (Join-Path $Output ($taskCase.Name + '.log'))
    if ($LASTEXITCODE -ne 0) { throw "$($taskCase.Name) exited $LASTEXITCODE; inspect its log." }
    if (!(Test-Path -LiteralPath $taskCapture)) { throw "$($taskCase.Name) did not produce a screenshot." }
    Write-Output "Captured $taskCapture"
}
$taskCommands | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Output 'commands.json') -Encoding UTF8
