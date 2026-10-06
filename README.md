# Sonnet 5.5 chess 24hrs — a chess engine written from scratch in 24 hours by an AI model

**Official rating:** **3420 Elo ±16** (95% CI, CCRL Blitz scale), second of seven AI engines in the series;
see [Official results](#official-results).

## What this is

This repository holds the result of **"The 24-Hour Chess Engine Benchmark"**: one AI model (**Sonnet 5.5**, model id
`claude-sonnet-5-5`, running as an autonomous agent in Claude Code) was given 24 hours of wall-clock time to design,
build, test and deliver a UCI chess engine **from scratch**, with nobody to ask for help.
The only thing measured is playing strength (Elo) of the single executable left in `final/`, played at
**10 s + 0.1 s increment, single thread, Hash = 256 MB**, with fastchess against a range of other engines (including the
Stash versions in `resources/engines/`) from an opening book.

Rules in short (full text in `CLAUDE.md`): C or C++ with GCC/MinGW-w64, standard library only, no third-party chess code,
no copying of source from existing engines, no pre-existing networks/books/tablebases; documentation, ideas and published
constants may be used; the executable must be fully standalone (static), built for `x86-64-v3` (no `-march=native`,
no AVX-512), and must never crash, hang, play an illegal move or lose on time. The run started at
2026-10-04 15:23:55 (local) and ended at 2026-10-05 15:23:55. Time management of the 24 hours is itself part of what is
being benchmarked.

## The engine

* Name (`id name`): **Sonnet 5.5 chess 24hrs**, author: **Sonnet 5.5**, executable: `final/Sonnet55chess24hrs.exe`
* Language: **C++17** (GCC 15.2.0, MinGW-w64, via scoop). C++ was chosen for speed and for intrinsics (`_pext_u64`, AVX2).
* Single translation unit: `source/src/main.cpp` (includes the other headers). The final network is embedded in the
  executable with `.incbin`.

### Build

```
# plain build (no embedded net; load one at runtime with  setoption name EvalFile value <file.nnue>)
g++ -O3 -march=x86-64-v3 -std=c++17 -DNDEBUG -flto -static -Wl,--stack,16777216 -o engine.exe source/src/main.cpp

# the release build that produced final/ (PGO + embedded net)
bash source/make_final.sh source/nets/n8a.nnue
#   = g++ ... -DNET_FILE="<abs path>/n8a.nnue" -fprofile-generate, run `engine bench 13`, then rebuild with -fprofile-use
```
The result links only against `KERNEL32.dll` and `msvcrt.dll`; it was checked to start and play from a `cmd.exe` whose PATH
contained nothing but System32.

### Running / UCI

Standard UCI (`uci`, `isready`, `ucinewgame`, `setoption`, `position`, `go wtime/btime/winc/binc/movestogo/movetime/depth/nodes/infinite`,
`stop`, `quit`; unknown commands are ignored). Searching runs in a worker thread so `stop`/`isready`/`quit` are answered at once.
`info` lines carry depth, seldepth, score (cp/mate), nodes, nps, hashfull, time and pv.

Options: `Hash` (default 256 MB), `Threads` (fixed at 1), `Move Overhead` (default 25 ms), `EvalFile` (load an alternative net),
plus ~50 numeric search/time parameters (`RfpMargin`, `LmrBase`, `TimeDiv`, ...) that exist for tuning; the defaults are the tuned values.
Extra command-line modes of the same binary: `bench [depth]`, `datagen ...` (self-play data generation) and `nncheck`.

## Architecture and features

**Board / move generation**: bitboards, PEXT-indexed sliding-attack tables (BMI2), make/unmake with an undo stack, incremental
Zobrist and pawn keys, pins/checkers computed on make, staged pseudo-legal generation (captures / quiets / evasions) with a legality filter.
Passes all 126 positions of `perft.epd` at depths 1-6 (first verified 15:30, 7 minutes into the run).

**Evaluation**: a hand-crafted tapered evaluation (PeSTO-style tables, mobility, pawn structure, king safety, threats; written so it could be
tuned linearly) was used for the first ~2 hours, then **replaced by an NNUE** trained entirely on the engine's own self-play:
`768 inputs (piece x square, own/opponent perspective) -> 256 (x2 perspectives) -> 1`, SCReLU activation, 8 material-count output
buckets, int16 quantisation (QA 255 / QB 128), AVX2 inference with incrementally updated accumulators (fused add/sub updates),
50-move-rule damping. No king buckets (tried, see below).

**Search**: iterative deepening with aspiration windows, PVS, transposition table (5 x 12-byte entries per 64-byte bucket, aging),
quiescence search with SEE pruning, null-move pruning, reverse futility pruning, futility pruning, late-move pruning, SEE pruning,
late-move reductions (fixed-point, with ~10 tunable adjustments), ProbCut, singular extensions, check extensions, internal iterative reduction,
mate-distance pruning, killer / counter-move / butterfly / 1-2-4-ply continuation / capture histories, pawn-structure **correction history**
for the static evaluation. (Razoring was implemented and then removed: it cost >100 Elo with the NNUE.)

**Time management**: soft limit `T/18 + 0.9*inc`, hard limit ~3.5x soft (never more than 75% of the clock minus 25 ms overhead),
iteration-level stopping at 70% of the soft limit scaled by best-move stability and score drops, single-legal-move shortcut.

**Deliberately left out**: pondering, multi-threading, tablebases, opening book, Chess960, MultiPV, king-bucketed or two-layer networks
(tested/considered, did not pay off at this time control).

### Training pipeline (all inside this repository)
* `datagen` mode: self-play from random 8-9-ply openings or from the supplied UHO book lines (50/50), fixed soft node budget per move
  (1500 -> 5000 nodes), win/draw adjudication, quiet positions only, 32-byte packed records (position, search score, game result).
* `source/trainer/train.cpp`: OpenMP trainer (Adam, cosine learning-rate decay, sigmoid loss on `0.7*score + 0.3*result`,
  ~0.6-1 M samples/s on 10 threads). Training data (~62 M positions, 8 self-play generations) lives outside the project folder
  (`C:\Users\steve\chess_nnue_data`) because of its size; every net is regenerable with the scripts in `source/scripts/`.
* Each generation was labelled by the previous generation's net and the (by then tuned) search: HCE -> net1 -> n2a -> n3a -> n4a -> n5a -> n6a -> n7a -> n8a (= final).
* Fast testing: fixed-node fastchess matches (20k-100k nodes) gave 400-800 games in a few minutes, independent of machine load; used for net,
  ablation and SPSA experiments, then confirmed at real time controls. `source/scripts/spsa.ps1` is a small SPSA tuner driving fastchess.

## How the time was spent (timeline from `docs/progress.md`)

| Clock (Oct 4/5) | What happened |
|---|---|
| 15:24-15:40 | Setup, bitboard/PEXT movegen, perft suite passes (15:30), UCI, PVS search + hand-crafted eval, first games (15:37), v1 installed in `final/` |
| 15:40-17:45 | Self-play data generator + NNUE trainer + engine inference; net1 ~ parity with HCE; n2a (+86) installed |
| 17:45-20:50 | Generations 3-4; n3a (+92 over n2a, ~3200). King-bucket and H512 nets lost to H256 despite lower validation loss (validation set is correlated with training games) |
| 20:50-22:45 | Fast fixed-node ablation harness; found razoring cost ~110 Elo; removed |
| 22:00-00:45 | SPSA run 2 (16 params, 1000 iterations): +50 at 100k nodes |
| 01:10-05:30 | SPSA run 3 (27 params) in parallel with generation 5 data; n5a (+96 over n3a) installed 04:07 |
| 05:40-11:00 | Generations 6-8 (n6a +35, n7a +15, n8a +8: saturating); SPSA run 4 gave no further gain |
| 11:10-12:50 | Time-management tests at 3+0.03 (more thinking time per move: +30); final params |
| 12:50-14:30 | Final verification matches (below), H512 net rejected at real TC, stress test, documentation |

Measured vs accepted on judgement: net/size/lambda/ablation decisions and all search-parameter changes were measured (400-1600 games each);
the choice of 15 epochs, learning rate, adjudication thresholds, data mix and the time-management formula shape were judgement.
What went wrong / was cut: an early "crash" turned out to be an illegal test FEN (engine now rejects positions where the side not to move is in check);
razoring and RFP were mis-tuned for the first NNUE versions and cost ~100 Elo until they were ablated at ~21:00 on day 1; scripting mistakes (shell escaping, PowerShell
path/variable quirks) cost perhaps an hour in total; KB4/H512/mirror-augmentation/lambda variations did not help.

## Elo by hour

Estimates on the Stash/CCRL-blitz scale (anchors: stash-33 = 3286, stash-37 = 3424). Hours 0-4 and the in-between hours are derived from
matches against v1/previous nets plus the Stash anchors measured at hour 0.6 and hour 3.6; hours after 22.5 use the final measured ladder.
Values marked "~" are interpolations (fixed-node gains discounted for real time control), so treat them as +/- 50.

| Hour | Clock | Estimated Elo | Evidence / note |
|---|---|---|---|
| 0 | 15:23 | not yet measurable | start |
| 1 | 16:23 | ~3030 | v1 HCE: 5W-43L-12D vs stash-33 |
| 2 | 17:23 | ~3050 | net1 ~ parity with v1 (51%) |
| 3 | 18:23 | ~3115 | n2a installed (+86 vs v1) |
| 4 | 19:23 | ~3200 | n3a: 28W-57L-35D vs stash-33 (-86) |
| 5 | 20:23 | ~3200 | n4a parity with n3a |
| 6 | 21:23 | ~3200 | ablation phase, razoring bug found |
| 7 | 22:23 | ~3290 | razoring off (+105..115 at fixed nodes) installed 22:45 |
| 8 | 23:23 | ~3290 | SPSA 2 running |
| 9 | 00:23 | ~3300 | SPSA 2 validating (+50 at 100k nodes) |
| 10 | 01:23 | ~3320 | SPSA2 params installed 00:48 |
| 11 | 02:23 | ~3320 | gen5/SPSA3 running |
| 12 | 03:23 | ~3320 | same |
| 13 | 04:23 | ~3380 | n5a installed 04:07 (+96 vs n3a at fixed nodes) |
| 14 | 05:23 | ~3390 | SPSA3 params (+23) |
| 15 | 06:23 | ~3390 | gen6 running |
| 16 | 07:23 | ~3410 | n6a installed 06:58 (+35) |
| 17 | 08:23 | ~3410 | gen7 |
| 18 | 09:23 | ~3420 | n7a installed 08:44 (+15) |
| 19 | 10:23 | ~3420 | gen8 / SPSA4 |
| 20 | 11:23 | ~3420 | time-management tests |
| 21 | 12:23 | ~3420 | same |
| 22 | 13:23 | ~3430 | final installed 12:49; ladder: stash-33 +175 (200 g), stash-37 +2 (200 g) |
| 23 | 14:23 | ~3430 | H512 rejected, stress test clean |
| 24 | 15:23 | ~3430 | final |

## Assumptions

* Target hardware: modern Windows 11 laptop with POPCNT/BMI1/BMI2/AVX2 and fast PEXT (not Zen 1/2); compiled for `x86-64-v3` only.
* `timemargin` of the rating match unknown: the engine keeps a 25 ms move-overhead reserve and hard limits of <= 75% of the remaining clock; it
  never lost a game on time in thousands of test games, including a 32-processes-on-12-cores stress run.
* Only `Hash` is set by the harness; all other defaults are the intended ones. 256 MB hash is allocated at start-up (memset), so `ucinewgame` clears 256 MB (tens of ms, outside the clock).
* Testing used 10 fastchess workers on a 12-core hybrid (4P+8E) i7-1260P; timing noise on E-cores was accepted. Fixed-node tests were used when timing noise would swamp the effect.
* Stash/CCRL anchors were used as given in `resources/engines/StashStrength.md`; CCRL ratings are for longer time controls, so the Elo mapping is approximate.
* Self-play data generation used the UHO book lines from `resources/` as openings for half of the games; no other external data/nets/books were used and no external websites were consulted (see `docs/resources.md`).
* Training data and intermediate nets are not part of the 24-hour deliverable except the final net, which is embedded in the executable and kept in `source/nets/n8a.nnue`.

## Estimated strength

**About 3425 Elo on the CCRL-blitz/Stash scale (roughly Stash-37 strength), +/- 40.** Evidence (final executable, 10+0.1, 1 thread, 256 MB, UHO openings with `-repeat`, 10 parallel games):
* vs stash-37 (3424): two independent runs of 200 games each, 64W-63L-73D and 70W-69L-61D, i.e. 134W-132L-134D over 400 games = +2 +/- 27 (50.25%);
* vs stash-33 (3286): 122W-29L-49D, 200 games (+175 +/- 41);
* vs the previous release (n7a net, older time settings): +32 +/- 27 (300 games);
* vs stash-25 (2940) under 16-way oversubscription: 107W-2L-11D (120 games), no time forfeits, crashes or illegal moves in any match.
Uncertainty: the Stash anchors are CCRL values at other time controls and the two ladder points differ by ~170 Elo from each other
(stash-33 would suggest ~3460, stash-37 ~3425); 200-400 games per point gives about +/-27-40 Elo of statistical error.

## Official results

Rated by the benchmark operator on 5 October 2026 in 2,180 games, added to the series'
existing rating dataset (9,320 games in total across all seven engines).

**Sonnet 5.5 chess 24hrs: 3420 Elo ±16 (95% CI), CCRL Blitz scale**, from 2,180 games
(+732 =756 −692, 50.9% score). That is second in the series, about 45 Elo below Opus 5.5
and about 160 Elo above Fable 5.1.

The in-session estimate of ≈3425 ±40 was almost exactly right. Measured only against the
anchor engines, Sonnet 5.5 rates **3416 ±18**, and the two anchor gauntlets agree
(3410 ±24 and 3423 ±25). The games against the other AI engines are consistent with that.
The Stash anchors imply a little more than the non-Stash anchors (3459–3504 vs 3361–3445),
because the Stash ladder is stretched at this time control and on this hardware.

### How the rating was measured

- Tool: fastchess 1.8.2, time control **10 s + 0.1 s**, one thread, **64 MB hash**,
  `timemargin 200`. (The development runs used Hash 256.)
- Openings: UHO unbalanced 8-ply book, random order, every opening played with
  both colours.
- Adjudication: resign after 3 moves at ±600 cp (two-sided); draw after move 40
  when 8 consecutive moves stay within ±20 cp; 250-move cap. `-recover` on.
- Format: a 72-game pilot (54% vs Stash 35, Stash 37 and Marvin) placed it near 3440.
  Sonnet 5.5 then played two gauntlets against nine anchors within about 150 Elo of it
  (1,600 games), plus 580 games against Opus 5.5, Fable 5.1, Opus 5 and Astra 6.
  Fable 5 and Sonnet 5 were left out because they are ~400+ Elo weaker.
  Ratings come from an anchored maximum-likelihood Elo fit over all games, with the
  anchors fixed at their CCRL Blitz ratings.
- Hardware: Ryzen AI 9 HX 375 (12 cores), 12 games at a time.
- Clean run: no time losses, crashes, disconnects or illegal moves in 2,180 games.

### Series standings

| Rank | Engine | Elo | 95% CI | Games |
|---:|---|---:|:---:|---:|
| 1 | Opus 5.5 chess 24hrs | 3463 | ±16 | 2,180 |
| **2** | **Sonnet 5.5 chess 24hrs** | **3420** | **±16** | **2,180** |
| 3 | Fable 5.1 chess 24hrs | 3260 | ±19 | 1,580 |
| 4 | Opus 5 chess 24hrs | 3229 | ±19 | 1,580 |
| 5 | Astra 6 chess 24hrs | 3144 | ±20 | 1,520 |
| 6 | Fable 5 chess 24hrs | 3044 | ±21 | 1,200 |
| 7 | Sonnet 5 chess 24hrs | 2702 | ±25 | 1,200 |

### Sonnet 5.5's individual results

| Opponent | Rating | Games | +W | =D | −L | Score | Implied Elo |
|---|---:|---:|---:|---:|---:|---:|---:|
| Patricia 5.0 | 3539 | 200 | 25 | 65 | 110 | 28.7% | 3381 |
| Sirius 9.0 | 3528 | 200 | 24 | 77 | 99 | 31.2% | 3391 |
| Carp 3.0.1 | 3525 | 200 | 38 | 69 | 93 | 36.2% | 3427 |
| Tucano 12.00 | 3491 | 160 | 18 | 76 | 66 | 35.0% | 3383 |
| Leorik 3.2 | 3490 | 160 | 19 | 65 | 76 | 32.2% | 3361 |
| Opus 5.5 (AI) | 3463 | 160 | 34 | 70 | 56 | 43.1% | 3415 |
| Marvin 6.3.0 | 3458 | 160 | 46 | 62 | 52 | 48.1% | 3445 |
| Stash 37 | 3424 | 160 | 41 | 66 | 53 | 46.2% | 3398 |
| Stash 35 | 3347 | 160 | 83 | 44 | 33 | 65.6% | 3459 |
| Stash 33 | 3274 | 200 | 134 | 48 | 18 | 79.0% | 3504 |
| Fable 5.1 (AI) | 3260 | 160 | 100 | 45 | 15 | 76.6% | 3466 |
| Opus 5 (AI) | 3229 | 160 | 105 | 39 | 16 | 77.8% | 3447 |
| Astra 6 (AI) | 3144 | 100 | 65 | 30 | 5 | 80.0% | 3385 |

Anchor ratings are CCRL Blitz (2'+1") values: the Stash ladder from 2 Sep 2026 and the other
anchors from 23 Sep 2026. AI-engine ratings are their fitted values from the same run.
The ±16 interval is statistical only. It does not include the uncertainty in transferring
CCRL ratings to 10+0.1 on this machine, so the result is a rating "on the CCRL Blitz scale
under 10+0.1 conditions", not a CCRL rating.
