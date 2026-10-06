#!/bin/bash
# usage: make_final.sh <net.nnue> [extra g++ flags e.g. -DNN_KB=4 -DNN_H=512]   (builds PGO exe with embedded net -> ../final)
set -e
cd "$(dirname "$0")"
NET="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; shift
mkdir -p build/pgo && cd build/pgo && rm -f *.gcda
FLAGS=(-O3 -march=x86-64-v3 -std=c++17 -DNDEBUG -flto -static -Wl,--stack,16777216 "-DNET_FILE=\"$NET\"" "$@")
g++ "${FLAGS[@]}" -fprofile-generate -fprofile-update=single -o pgo_gen.exe ../../src/main.cpp 2>&1 | grep -v lto || true
./pgo_gen.exe bench 13 > /dev/null
g++ "${FLAGS[@]}" -fprofile-use -fprofile-correction -Wno-missing-profile -o ../../../final/Sonnet55chess24hrs.exe ../../src/main.cpp 2>&1 | grep -v "lto\|profile count" || true
cd ../../..
objdump -p final/Sonnet55chess24hrs.exe | grep "DLL Name"
ls -la final/
