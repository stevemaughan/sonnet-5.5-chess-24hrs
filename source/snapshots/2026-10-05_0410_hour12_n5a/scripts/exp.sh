#!/bin/bash
# usage: exp.sh <name> <data dirs (quoted)> <extra train args...>   trains H256 KB1 net then fixed-node match vs n3a (400 games)
name=$1; dirs=$2; shift 2
cd "C:/Users/steve/Dropbox/Chess/Development/AI Benchmarks/Sonnet 5.5/source"
./build/train256m.exe --data $dirs --epochs 15 --batch 16384 --threads ${THREADS:-5} --lr 0.001 --lrend 0.00002 "$@" --out nets/$name > nets/$name.log 2>&1
powershell -NoProfile -File scripts/match.ps1 -A "source\build\e5.exe" -B "source\build\e5.exe" -AOpts "option.EvalFile=source\nets\$name.nnue" -BOpts "option.EvalFile=source\nets\n3a.nnue" -Rounds ${ROUNDS:-200} -Conc ${CONC:-4} -TC "60+1" -Nodes ${NODES:-20000} -Tag fn_${name}_vs_n3a > tests/fn_${name}_vs_n3a.sum 2>&1
