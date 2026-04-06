@echo off
echo Bootstrapping ...
%CC% -O0 -g --std=c++20 -Wno-braced-scalar-init now.cpp -o now.exe
if errorlevel 1 (
    echo ERR: Unable to compile bootstrap!
) else (
    echo Bootstrapping is done. Run now.exe to start.
)