#!/bin/bash
# usage: build.sh <out.exe> [extra flags]
out=$1; shift
g++ -O3 -march=x86-64-v3 -std=c++17 -DNDEBUG -flto -static -Wl,--stack,16777216 "$@" -o "$out" src/main.cpp
