#!/bin/sh
# Headless Ghidra import + analysis of the SoI ROM (read-only input). Project lives on D: (not in the tree).
# Usage: run_ghidra.sh import|export [export-args...]
GH="C:/Users/Shadow/Documents/Ghidra/ghidra/build/dist/ghidra_12.2_DEV"
export JAVA_HOME="C:/Users/Shadow/Documents/Ghidra/_build/jdk-25.0.4+7"
ROM="D:/Theia-staging/spyro2-mod/soi-ghidra/rom/ASYE.gba"  # hard link to the read-only ROM (the original path has parentheses, which break analyzeHeadless.bat)
PROJ="D:/Theia-staging/spyro2-mod/soi-ghidra"
HERE="C:/Users/Shadow/Documents/Spyro 2_PC/tools/soi/ghidra"
OUT="C:/Users/Shadow/Documents/Spyro 2_PC/data/soi/analysis/ghidra"
mode=$1; shift
if [ "$mode" = import ]; then
  "$GH/support/analyzeHeadless.bat" "$PROJ" SoI -import "$ROM" -overwrite -loader BinaryLoader \
    -loader-baseAddr 0x08000000 -processor ARM:LE:32:v4t -cspec default -scriptPath "$HERE" \
    -preScript PrepareSoI.java -analysisTimeoutPerFile 1500 -postScript ExportSoI.java "$OUT" "$@" \
    -log "$OUT/headless.log" -scriptlog "$OUT/scripts.log"
else
  "$GH/support/analyzeHeadless.bat" "$PROJ" SoI -process ASYE.gba -noanalysis \
    -scriptPath "$HERE" -postScript ExportSoI.java "$OUT" "$@" -log "$OUT/headless-export.log" -scriptlog "$OUT/scripts-export.log"
fi
