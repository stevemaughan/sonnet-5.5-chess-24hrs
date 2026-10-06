#!/bin/bash
# usage: tabl.sh <tag> "<A options>" [rounds] [tc]   timed match A(options) vs B(default), same exe+net
tag=$1; aopts=$2; rounds=${3:-300}; tc=${4:-3+0.03}; exe=${EXE:-source/build/e12.exe}; net=${NET:-source/nets/n8a.nnue}
cd "C:/Users/steve/Dropbox/Chess/Development/AI Benchmarks/Sonnet 5.5/source"
powershell -NoProfile -File scripts/match.ps1 -A "$exe" -B "$exe" -AOpts "option.EvalFile=$net $aopts" -BOpts "option.EvalFile=$net" -Rounds $rounds -Conc ${CONC:-10} -TC "$tc" -Tag tm_$tag > tests/tm_$tag.sum 2>&1
echo "$tag [$aopts] ($tc): $(grep -E 'Elo:' tests/tm_$tag.sum | tail -1) | $(grep -E 'Games:' tests/tm_$tag.sum | tail -1)" >> tests/timetests.txt
