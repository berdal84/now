#!/usr/bin/env bash
printf "Bootstrapping ..."
$CC --std=c++20 -g -O0 -Wno-braced-scalar-init task.cpp -o task.exe
if [ $? -ne 0 ]; then
    echo "ERR: Unable to compile bootstrap!"
    exit 1
fi
printf "\rBootstrapping is done. Run ./task.exe to start.\n"
