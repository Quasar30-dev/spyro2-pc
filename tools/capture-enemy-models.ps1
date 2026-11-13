# Studio stills of every unmapped enemy class (coverage.json) for identification.
# Writes data/analysis/moby-cases/models/<slot>-<class>.bmp
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$coverage = Get-Content (Join-Path $root 'data/analysis/level-systems/coverage.json') -Raw | ConvertFrom-Json
$out = 'data/analysis/moby-cases/models'
New-Item -ItemType Directory -Force (Join-Path $root $out) | Out-Null
foreach ($level in $coverage) {
    foreach ($class in @($level.unimplemented_enemy_classes)) {
        if (-not $class) { continue }
        $arguments = @('--game', 'data/disc', '--slot', "$($level.slot)", '--smoke', '20', '--object-model', "0x$class", '--object-clip', '0',
            '--capture', "$out/$($level.slot)-$class.bmp")
        $p = Start-Process -FilePath (Join-Path $root 'build/Release/spyro_workbench.exe') -ArgumentList $arguments -WorkingDirectory $root -NoNewWindow -Wait -PassThru
        Write-Output "$($level.slot) $class exit $($p.ExitCode)"
    }
}
