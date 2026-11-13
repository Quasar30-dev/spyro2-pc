@echo off
setlocal
cd /d "%~dp0"
set "game=%~1"
if not defined game set "game=data\disc"
start "Boss Training Ground" "build\Release\spyro_workbench.exe" --game "%game%" --custom-level "data\custom-levels\boss-training\boss-training.s2world" --play --workbench
