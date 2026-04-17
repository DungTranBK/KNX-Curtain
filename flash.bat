@echo off
echo Programming...
nrfjprog --program build/merged.hex && (
    echo Flash successful, resetting...
    nrfjprog --reset
) || (
    echo Flash failed.
    pause
)
