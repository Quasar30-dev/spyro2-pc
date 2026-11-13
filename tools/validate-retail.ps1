param([string]$Game, [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $NoBuild) { & "$PSScriptRoot/build.ps1" }
if (-not $Game) {
    $taskCue = Get-ChildItem -LiteralPath $taskRoot -Filter '*.cue' -File -Recurse | Select-Object -First 1
    if (-not $taskCue) { throw 'Pass -Game with your ISO, CUE, BIN or extracted retail directory.' }
    $Game = $taskCue.FullName
}
$taskGame = (Resolve-Path -LiteralPath $Game).Path
$taskReport = Join-Path $taskRoot 'data/validation'
New-Item -ItemType Directory -Force -Path $taskReport | Out-Null
Push-Location -LiteralPath $taskRoot
try {
    & "$taskRoot/build/Release/spyro_inspect.exe" $taskGame --all-levels --powerups --verify-pickups --verify-breakables > "$taskReport/retail-inspection.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Retail archive validation failed. See data/validation/retail-inspection.txt.' }
    python "$taskRoot/tools/probe_terrain_materials.py" $taskGame --out "$taskReport/terrain-materials.json"
    if ($LASTEXITCODE -ne 0) { throw 'Terrain material / palette audit failed.' }
    python "$taskRoot/tools/verify_secondary.py" $taskGame --all-levels > "$taskReport/secondary-oracle-all.log"
    if ($LASTEXITCODE -ne 0) { throw 'Secondary animation diverged from retail MIPS instructions.' }
    $taskCases = @(
        @{Name='retail-camera-idle'; Slot=15; Frames=120; Replay=$false},
        @{Name='retail-camera-motion'; Slot=15; Frames=360; Replay=$true},
        @{Name='retail-camera-glimmer'; Slot=17; Frames=90; Replay=$false}
    )
    foreach ($taskCase in $taskCases) {
        $taskCapture = Join-Path $taskReport ($taskCase.Name+'.bmp')
        $taskArguments = @('--game', ('"'+$taskGame+'"'), '--slot', $taskCase.Slot,
            '--play', '--smoke', $taskCase.Frames, '--paced', '--capture', ('"'+$taskCapture+'"'))
        if ($taskCase.Replay) { $taskArguments += '--replay' }
        $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" `
            -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw "Native run failed: $($taskCase.Name). See data/validation/workbench-error.txt." }
        $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
        if ($taskResult.frames -ne $taskCase.Frames -or $taskResult.simulation_ticks -ne $taskCase.Frames) {
            throw "Frame / simulation count mismatch in $($taskCase.Name)."
        }
        if ($taskCase.Replay -and ($taskResult.observed_motion_mask -band 127) -ne 127) { throw 'Replay did not exercise all seven movement animation states, including glide braking.' }
        if (-not $taskResult.sparx_spawned -or $taskResult.sparx_script_steps -ne [Math]::Ceiling($taskCase.Frames/2)) {
            throw 'Sparx did not spawn or his script cadence changed.'
        }
        if ($taskCase.Replay -and $taskResult.sparx_delivered_gems -lt 1) { throw 'Movement replay did not exercise Sparx gem delivery.' }
        $taskExpectedObjects = if ($taskCase.Slot -eq 15) { 273 } else { 201 }
        $taskExpectedGems = if ($taskCase.Slot -eq 15) { 105 } else { 91 }
        if ($taskResult.retail_objects -ne $taskExpectedObjects -or $taskResult.loose_gems -ne $taskExpectedGems -or
            $taskResult.rendered_static_objects -le 0 -or $taskResult.rendered_animated_objects -le 0) { throw 'Retail object import / rendering regression.' }
        if ($taskResult.retail_units_per_world_unit -ne 1024 -or $taskResult.spyro_origin_height_retail -ne 356) {
            throw 'World-scale regression in the native renderer.'
        }
        Write-Output ('{0}: {1} frames, {2:N3}s, CPU mean {3:N3}ms, max {4:N3}ms, hardware {5}' -f
            $taskCase.Name,$taskResult.frames,$taskResult.elapsed_seconds,$taskResult.mean_cpu_work_ms,
            $taskResult.max_cpu_work_ms,$taskResult.hardware_renderer)
        if ($taskResult.cpu_frames_over_budget -gt 0) {
            Write-Warning ("{0} had {1} CPU frame(s) over 16.67ms; slowest was frame {2}." -f
                $taskCase.Name,$taskResult.cpu_frames_over_budget,$taskResult.max_cpu_work_frame)
        }
    }
    $taskTraversalCases = @(
        @{Name='retail-run'; Frames=110; Clip=2},
        @{Name='retail-free-glide'; Frames=238; Clip=16},
        @{Name='retail-glide-brake'; Frames=253; Clip=24}
    )
    foreach ($taskCase in $taskTraversalCases) {
        $taskCapture = Join-Path $taskReport ($taskCase.Name+'.bmp')
        $taskArguments = @('--game', ('"'+$taskGame+'"'), '--slot', 15, '--play', '--replay',
            '--smoke', $taskCase.Frames, '--capture', ('"'+$taskCapture+'"'))
        $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" `
            -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw "Traversal rendering failed: $($taskCase.Name)." }
        $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
        if ($taskResult.spyro_animation -ne $taskCase.Clip -or $taskResult.simulation_ticks -ne $taskCase.Frames) {
            throw "Wrong retail traversal clip: $($taskCase.Name)."
        }
    }
    $taskSwimCases = @(
        @{Name='swim-surface-idle'; Frames=58; Clip=44; Water=1},
        @{Name='swim-surface-move'; Frames=108; Clip=43; Water=1},
        @{Name='swim-dive'; Frames=152; Clip=40; Water=2},
        @{Name='swim-underwater-idle'; Frames=298; Clip=41; Water=2},
        @{Name='swim-underwater-move'; Frames=340; Clip=40; Water=2},
        @{Name='swim-underwater-charge'; Frames=388; Clip=42; Water=2},
        @{Name='swim-resurface'; Frames=800; Clip=44; Water=1}
    )
    foreach ($taskCase in $taskSwimCases) {
        $taskCapture = Join-Path $taskReport ($taskCase.Name+'.bmp')
        $taskArguments = @('--game', ('"'+$taskGame+'"'), '--slot', 15, '--swim-replay',
            '--smoke', $taskCase.Frames, '--capture', ('"'+$taskCapture+'"'))
        if ($taskCase.Frames -eq 800) { $taskArguments += '--paced' }
        $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" `
            -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw "Swimming rendering failed: $($taskCase.Name)." }
        $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
        if ($taskResult.spyro_animation -ne $taskCase.Clip -or $taskResult.water_mode -ne $taskCase.Water -or
            $taskResult.simulation_ticks -ne $taskCase.Frames) { throw "Swimming state / clip mismatch: $($taskCase.Name)." }
        if ($taskResult.rendered_transparent_terrain_triangles -ne 296) { throw 'Summer Forest water is missing from the transparent terrain pass.' }
        if ($taskCase.Frames -eq 800 -and ($taskResult.observed_motion_mask -band 15872) -ne 15872) {
            throw 'Swimming replay failed to visit all five swimming states.'
        }
        Write-Output ('{0}: clip {1}, water mode {2}, {3} frames, CPU p95 {4:N3}ms, max {5:N3}ms' -f
            $taskCase.Name,$taskResult.spyro_animation,$taskResult.water_mode,$taskResult.frames,$taskResult.p95_cpu_work_ms,$taskResult.max_cpu_work_ms)
    }
    foreach ($taskAppearance in @('retail','main')) {
        foreach ($taskPose in @(0,3,6)) {
            $taskCapture = Join-Path $taskReport ('supercharge-'+$taskAppearance+'-pose'+$taskPose+'.bmp')
            $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru `
                -ArgumentList @('--game',('"'+$taskGame+'"'),'--supercharge',$taskAppearance,'--animation',61,'--pose',$taskPose,'--smoke',3,'--capture',('"'+$taskCapture+'"'))
            if ($taskProcess.ExitCode -ne 0) { throw 'Supercharge appearance capture failed.' }
            $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
            $taskExpectedVertices = if ($taskAppearance -eq 'main') { 353 } else { 255 }
            if ($taskResult.spyro_vertices -ne $taskExpectedVertices -or $taskResult.spyro_animation -ne 61) {
                throw 'Supercharge appearance / leg-blur mesh mismatch.'
            }
        }
    }
    $taskObjectCases = @(
        @{Name='sparx-frame0'; Class=120; Clip=0; Pose=0},
        @{Name='sparx-frame1'; Class=120; Clip=0; Pose=1},
        @{Name='sparx-clip1'; Class=120; Clip=1; Pose=7},
        @{Name='object-444'; Class=444; Clip=0; Pose=0}
    )
    foreach ($taskCase in $taskObjectCases) {
        $taskCapture = Join-Path $taskReport ($taskCase.Name+'.bmp')
        $taskArguments = @('--game', ('"'+$taskGame+'"'), '--slot', 15, '--object-model', $taskCase.Class,
            '--object-clip', $taskCase.Clip, '--object-pose', $taskCase.Pose, '--smoke', 3, '--capture', ('"'+$taskCapture+'"'))
        $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" `
            -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw "Object model rendering failed: $($taskCase.Name)." }
        $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
        if ($taskResult.rendered_animated_objects -ne 1 -or $taskResult.object_preview_class -ne $taskCase.Class -or
            $taskResult.object_preview_clip -ne $taskCase.Clip -or $taskResult.object_preview_frame -ne $taskCase.Pose) {
            throw "Incorrect object model / animation selection: $($taskCase.Name)."
        }
    }
    $taskPoseA = (Get-FileHash -LiteralPath "$taskReport/sparx-frame0.bmp" -Algorithm SHA256).Hash
    $taskPoseB = (Get-FileHash -LiteralPath "$taskReport/sparx-frame1.bmp" -Algorithm SHA256).Hash
    if ($taskPoseA -eq $taskPoseB) { throw 'Sparx rendered identical images for different retail frames.' }
    $taskContainerCases = @(
        @{Name='basket-charge'; Record=122; Attack='charge'; Frames=90; Broken=1; Bouncing=0; Gems=0},
        @{Name='basket-flame'; Record=122; Attack='flame'; Frames=90; Broken=1; Bouncing=1; Gems=0},
        @{Name='basket-delivery'; Record=122; Attack='flame'; Frames=180; Broken=1; Bouncing=0; Gems=10},
        @{Name='vase-resists-flame'; Record=0; Attack='flame'; Frames=90; Broken=0; Bouncing=0; Gems=0},
        @{Name='vase-charge'; Record=0; Attack='charge'; Frames=90; Broken=1; Bouncing=0; Gems=0},
        @{Name='bottle-charge'; Record=137; Attack='charge'; Frames=90; Broken=1; Bouncing=0; Gems=0}
    )
    foreach ($taskCase in $taskContainerCases) {
        $taskCapture = Join-Path $taskReport ($taskCase.Name+'.bmp')
        $taskArguments = @('--game', ('"'+$taskGame+'"'), '--slot', 15, '--interaction-record', $taskCase.Record,
            '--attack', $taskCase.Attack, '--smoke', $taskCase.Frames, '--paced', '--capture', ('"'+$taskCapture+'"'))
        $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" `
            -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw "Container replay failed: $($taskCase.Name)." }
        $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
        if ($taskResult.simulation_ticks -ne $taskCase.Frames -or $taskResult.broken_containers -ne $taskCase.Broken -or
            $taskResult.released_container_gems -ne $taskCase.Broken -or $taskResult.bouncing_gems -ne $taskCase.Bouncing -or
            $taskResult.gem_total -ne $taskCase.Gems) { throw "Container / gem state mismatch: $($taskCase.Name)." }
        if ($taskCase.Broken -and $taskResult.active_fragments -le 0) { throw 'Retail container fragments are missing.' }
        if ($taskCase.Name -eq 'vase-resists-flame' -and $taskResult.heated_vases -ne 1) { throw 'Flame did not actually hit and heat the resistant vase.' }
        Write-Output ('{0}: {1} broken, {2} gem value collected, CPU p95 {3:N3}ms, max {4:N3}ms' -f
            $taskCase.Name,$taskResult.broken_containers,$taskResult.gem_total,$taskResult.p95_cpu_work_ms,$taskResult.max_cpu_work_ms)
        if ($taskResult.cpu_frames_over_budget -gt 0) { Write-Warning "$($taskCase.Name) exceeded the CPU frame budget in $($taskResult.cpu_frames_over_budget) frame(s)." }
    }
    $taskFlameCases = @(
        @{Name='flame-before'; Frames=80; Visible=$false; Orbit=0},
        @{Name='flame-emission'; Frames=90; Visible=$true; Orbit=0},
        @{Name='flame-trails'; Frames=100; Visible=$true; Orbit=0},
        @{Name='flame-side'; Frames=100; Visible=$true; Orbit=90},
        @{Name='flame-fading'; Frames=110; Visible=$true; Orbit=90},
        @{Name='flame-expired'; Frames=130; Visible=$false; Orbit=0}
    )
    foreach ($taskCase in $taskFlameCases) {
        $taskCapture = Join-Path $taskReport ($taskCase.Name+'.bmp')
        $taskArguments = @('--game', ('"'+$taskGame+'"'), '--slot', 15, '--play', '--replay', '--camera-orbit', $taskCase.Orbit,
            '--smoke', $taskCase.Frames, '--capture', ('"'+$taskCapture+'"'))
        $taskProcess = Start-Process -FilePath "$taskRoot/build/Release/spyro_workbench.exe" `
            -ArgumentList $taskArguments -WorkingDirectory $taskRoot -WindowStyle Hidden -Wait -PassThru
        if ($taskProcess.ExitCode -ne 0) { throw "Flame rendering failed: $($taskCase.Name)." }
        $taskResult = Get-Content -LiteralPath ([IO.Path]::ChangeExtension($taskCapture,'.json')) -Raw | ConvertFrom-Json
        if ($taskResult.simulation_ticks -ne $taskCase.Frames -or
            ($taskResult.rendered_flame_streams -gt 0) -ne $taskCase.Visible -or
            ($taskResult.rendered_flame_triangles -gt 0) -ne $taskCase.Visible) { throw "Flame lifetime / rendering mismatch: $($taskCase.Name)." }
        Write-Output ("{0}: {1} streams, {2} submitted triangles" -f $taskCase.Name,$taskResult.rendered_flame_streams,$taskResult.rendered_flame_triangles)
    }
    Write-Output "All retail validation runs passed. Reports and captures: $taskReport"
} finally { Pop-Location }
