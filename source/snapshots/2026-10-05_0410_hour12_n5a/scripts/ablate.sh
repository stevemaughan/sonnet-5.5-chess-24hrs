#!/bin/bash
# usage: ablate.sh <tag> "<A options e.g. option.CorrPct=0>" [rounds] [nodes] [exe]   fixed-node match, A(options) vs B(default), same exe+net
tag=$1; aopts=$2; rounds=${3:-400}; nodes=${4:-20000}; exe=${5:-source/build/e6.exe}; net=${NET:-source/nets/n3a.nnue}
cd "C:/Users/steve/Dropbox/Chess/Development/AI Benchmarks/Sonnet 5.5/source"
powershell -NoProfile -File scripts/match.ps1 -A "$exe" -B "$exe" -AOpts "option.EvalFile=$net $aopts" -BOpts "option.EvalFile=$net" -Rounds $rounds -Conc ${CONC:-10} -TC "60+1" -Nodes $nodes -Tag ab_$tag > tests/ab_$tag.sum 2>&1
echo "$tag [$aopts]: $(grep -E 'Elo:' tests/ab_$tag.sum | tail -1) | $(grep -E 'Games:' tests/ab_$tag.sum | tail -1)" >> tests/ablations.txt
