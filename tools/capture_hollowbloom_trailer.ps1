param([string[]]$Shots=@('02-orchard-run','03-moonwell','04-basket','05-enemy','06-charge-jump','07-conservatory','08-spyro'))
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskExe=Join-Path $taskRoot 'build/Release/spyro_workbench.exe'
foreach($taskShot in $Shots) {
    if($taskShot -notmatch '^\d\d-[a-z-]+$'){throw 'Invalid shot name'}
    $taskArguments=@('--game','data/disc','--custom-level','data/custom-levels/hollowbloom-springs/pc/hollowbloom.s2world',
        '--cinematic-shot',"output/hollowbloom-trailers/source/$taskShot.s2shot",'--capture-music-volume','0',
        '--capture',"output/hollowbloom-trailers/review/$taskShot.bmp",'--video',"output/hollowbloom-trailers/gameplay/$taskShot.mp4")
    $taskProcess=Start-Process -FilePath $taskExe -WorkingDirectory $taskRoot -WindowStyle Hidden -ArgumentList $taskArguments -PassThru -Wait
    if($taskProcess.ExitCode -ne 0){throw "Capture $taskShot failed with $($taskProcess.ExitCode)"}
    Write-Output "Captured $taskShot"
}
