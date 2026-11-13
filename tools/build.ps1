param([switch]$CoreOnly, [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($taskCmake) { $taskCmake = $taskCmake.Source }
else {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $taskVswhere)) { throw 'Install CMake and the Visual Studio C++ workload.' }
    $taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $taskCmake = Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
$taskDesktop = if ($CoreOnly) { 'OFF' } else { 'ON' }
& $taskCmake -S $taskRoot -B "$taskRoot/build" -A x64 "-DSPYRO_BUILD_DESKTOP=$taskDesktop"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $taskCmake --build "$taskRoot/build" --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
$taskCtest = Join-Path (Split-Path -Parent $taskCmake) 'ctest.exe'
& $taskCtest --test-dir "$taskRoot/build" -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
