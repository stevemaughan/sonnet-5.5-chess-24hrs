#pragma once
#include "eval.h"

constexpr int MAXPLY = 128;
constexpr int INF = 32001;
constexpr int MATE = 32000;
constexpr int MATE_BOUND = MATE - MAXPLY;
constexpr int NO_EVAL = 32002;

// ---------------------------------------------------------------------------
// Transposition table
// ---------------------------------------------------------------------------
enum { B_NONE = 0, B_UPPER = 1, B_LOWER = 2, B_EXACT = 3 };

struct TTEntry {
    uint32_t key32;
    Move move;
    int16_t score;
    int16_t eval;
    uint8_t depth;
    uint8_t flagAge;  // bound (2 bits) | age << 2
    int bound() const { return flagAge & 3; }
    int age() const { return flagAge >> 2; }
};
struct alignas(64) TTCluster {
    TTEntry e[5];
    char pad[4];
};

struct TransTable {
    TTCluster* table = nullptr;
    U64 nClusters = 0;
    int age = 0;

    void resize(size_t mb) {
        if (table) _mm_free(table);
        nClusters = (mb * 1024 * 1024) / sizeof(TTCluster);
        if (nClusters < 1) nClusters = 1;
        table = (TTCluster*)_mm_malloc(nClusters * sizeof(TTCluster), 64);
        clear();
    }
    void clear() {
        memset(table, 0, nClusters * sizeof(TTCluster));
        age = 0;
    }
    inline TTCluster* cluster(U64 key) const {
        return &table[(unsigned __int128)key * nClusters >> 64];
    }
    inline void prefetch(U64 key) const { _mm_prefetch((const char*)cluster(key), _MM_HINT_T0); }
    TTEntry* probe(U64 key, bool& found) {
        TTCluster* c = cluster(key);
        uint32_t k = (uint32_t)key;
        for (int i = 0; i < 5; i++)
            if (c->e[i].key32 == k && c->e[i].bound()) { found = true; return &c->e[i]; }
        found = false;
        TTEntry* r = &c->e[0];
        int rv = 1 << 30;
        for (int i = 0; i < 5; i++) {
            if (!c->e[i].bound()) return &c->e[i];
            int v = c->e[i].depth - 8 * ((age - c->e[i].age()) & 63);
            if (v < rv) { rv = v; r = &c->e[i]; }
        }
        return r;
    }
    void store(TTEntry* e, U64 key, int score, int eval, int depth, int bound, Move m, int ply) {
        uint32_t k = (uint32_t)key;
        if (score >= MATE_BOUND) score += ply; else if (score <= -MATE_BOUND) score -= ply;
        if (m || e->key32 != k) e->move = m;
        if (bound == B_EXACT || e->key32 != k || depth + 3 >= e->depth || e->age() != age) {
            e->key32 = k;
            e->score = (int16_t)score;
            e->eval = (int16_t)eval;
            e->depth = (uint8_t)std::max(depth, 0);
            e->flagAge = (uint8_t)(bound | (age << 2));
        }
    }
    int hashfull() const {
        int cnt = 0;
        U64 n = std::min<U64>(nClusters, 250);
        for (U64 i = 0; i < n; i++)
            for (int j = 0; j < 5; j++)
                if (table[i].e[j].bound() && table[i].e[j].age() == age) cnt++;
        return (int)(cnt * 1000 / (n * 5));
    }
};
static TransTable TT;

static inline int scoreFromTT(int s, int ply) {
    if (s >= MATE_BOUND) return s - ply;
    if (s <= -MATE_BOUND) return s + ply;
    return s;
}

// ---------------------------------------------------------------------------
// History tables
// ---------------------------------------------------------------------------
static int16_t mainHist[2][4096];
static int16_t contHist[768][768];
static int16_t contNone[768];
static int16_t capHist[768][6];
static Move counterMoves[768];
static int16_t corrHist[2][16384];
static int lmrTable[64][64];
static int lmrTable16[64][64];

static inline void updHist(int16_t& h, int bonus) {
    h += bonus - (int)h * std::abs(bonus) / 16384;
}

struct SSEntry {
    Move move;
    int staticEval;
    Move killers[2];
    int ply;
    Move excluded;
    int16_t* cont;
    int pcto;
};

struct Limits {
    int64_t wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int movestogo = 0, depth = 0;
    U64 nodes = 0, softNodes = 0;
    bool infinite = false;
};

// tunable options (exposed via setoption for testing)
struct Tun {
    int moveOverhead = 25;
};
static Tun tun;
#ifdef STATS
static U64 stCut[8], stCutAll;
#endif

struct TuneParam { const char* name; int* ptr; int mn, mx; };
static std::vector<TuneParam>& tuneList() { static std::vector<TuneParam> v; return v; }
#define TUNEP(var, def, mn, mx) static int var = def; static const bool reg_##var = (tuneList().push_back({#var, &var, mn, mx}), true);
TUNEP(RazorMargin, 260, 50, 600)
TUNEP(RfpMargin, 63, 20, 200)
TUNEP(RfpImp, 62, 0, 150)
TUNEP(RfpDepth, 8, 1, 14)
TUNEP(NmpSlope, 33, 0, 60)
TUNEP(NmpBase, 172, 0, 400)
TUNEP(NmpR, 4, 1, 6)
TUNEP(NmpDiv, 4, 1, 8)
TUNEP(NmpEvalDiv, 120, 50, 500)
TUNEP(LmpBase, 3, 0, 10)
TUNEP(FutBase, 123, 0, 300)
TUNEP(FutMult, 105, 20, 250)
TUNEP(HistPrune, 2382, 0, 8000)
TUNEP(SeeQuiet, 53, 10, 200)
TUNEP(SeeCap, 37, 10, 250)
TUNEP(SingMargin, 5, 1, 6)
TUNEP(LmrHistDiv, 4115, 1000, 20000)
TUNEP(HistBonusMax, 1715, 200, 4000)
TUNEP(HistBonusMul, 224, 20, 400)
TUNEP(HistBonusSub, 133, 0, 300)
TUNEP(TimeDiv, 22, 8, 60)
TUNEP(IncPct, 75, 0, 150)
TUNEP(SoftMaxPct, 45, 10, 90)
TUNEP(HardMult10, 35, 15, 80)
TUNEP(HardMaxPct, 75, 30, 95)
TUNEP(StopFracPct, 50, 20, 100)
TUNEP(AspDelta, 21, 5, 60)
TUNEP(CorrPct, 90, 0, 200)
TUNEP(LmrCapMinus, 32, 0, 40)
TUNEP(LmrPvMinus, 17, 0, 40)
TUNEP(LmrCutPlus, 8, 0, 40)
TUNEP(LmrImpMinus, 13, 0, 40)
TUNEP(LmrCheckMinus, 26, 0, 40)
TUNEP(LmrKillerMinus, 15, 0, 40)
TUNEP(LmrInCheckMinus, 20, 0, 40)
TUNEP(SingDepth, 8, 4, 99)
TUNEP(CheckExt, 1, 0, 1)
TUNEP(IirDepth, 4, 3, 99)
TUNEP(NmpMinDepth, 3, 2, 99)
TUNEP(LmpOn, 1, 0, 1)
TUNEP(FutOn, 1, 0, 1)
TUNEP(SeeOn, 1, 0, 1)
TUNEP(RazorOn, 0, 0, 1)
TUNEP(RfpOn, 1, 0, 1)
TUNEP(ProbcutDepth, 5, 3, 12)
TUNEP(ProbcutMargin, 81, 50, 400)
TUNEP(ProbcutImp, 50, 0, 150)
TUNEP(LmrBase, 123, 0, 150)
TUNEP(LmrDiv, 229, 100, 400)

// ---------------------------------------------------------------------------
// Search object
// ---------------------------------------------------------------------------
struct Search;
static Search* gSearch;

struct Search {
    Position pos;
    std::atomic<bool> stopFlag{false};
    bool stopped = false;
    bool silent = false;
    bool timed = false;
    int64_t startMs = 0, softMs = 0, hardMs = 0, optMs = 0;
    U64 nodes = 0, nodeLimit = 0;
    int selDepth = 0;
    int maxDepth = MAXPLY - 1;
    SSEntry stackArr[MAXPLY + 12];
    SSEntry* stack = stackArr + 6;
    Move pvTable[MAXPLY + 2][MAXPLY + 2];
    int pvLen[MAXPLY + 2];
    Move rootBest = 0, rootPonder = 0;
    int rootScore = 0;
    int rootDepthDone = 0;
    int rootMoveCount = 0;
    int lastInfoScore = 0;
    int finalScore = 0;

    // --- move picker ------------------------------------------------------
    struct Picker {
        Search& S;
        Position& pos;
        SSEntry* ss;
        Move ttMove, k1, k2, counter;
        int stage;
        bool qs, inCheck;
        MoveList list;
        int idx = 0, nBad = 0, badIdx = 0;
        ScoredMove bad[64];

        enum { ST_TT, ST_GEN_CAPS, ST_CAPS, ST_K1, ST_K2, ST_COUNTER, ST_GEN_QUIETS, ST_QUIETS, ST_BAD, ST_GEN_EVA, ST_EVA, ST_DONE };

        Picker(Search& s, SSEntry* ss_, Move tt, bool qsMode) : S(s), pos(s.pos), ss(ss_), ttMove(tt), qs(qsMode) {
            inCheck = pos.checkers != 0;
            k1 = k2 = counter = 0;
            if (!qs && !inCheck) {
                k1 = ss->killers[0]; k2 = ss->killers[1];
                int pcto = (ss - 1)->pcto;
                counter = pcto ? counterMoves[pcto] : 0;
            }
            // validate tt move
            if (ttMove) {
                bool ok = pos.isPseudoLegal(ttMove);
                if (ok && qs && !inCheck && !mIsCapture(ttMove) && !mIsPromo(ttMove)) ok = false;
                if (!ok) ttMove = 0;
            }
            stage = ttMove ? ST_TT : (inCheck ? ST_GEN_EVA : ST_GEN_CAPS);
        }

        inline int captureScore(Move m) {
            int to = mTo(m), fl = mFlags(m);
            int victim = fl == F_EP ? PAWN : (pos.board[to] == NO_PIECE ? PAWN : pieceType(pos.board[to]));
            int sc = SEE_VAL[victim] * 8;
            if (fl & 8) sc += ((fl & 3) == 3 ? 8000 : 0);
            int pc = pos.board[mFrom(m)];
            sc += capHist[pc * 64 + to][victim] / 16;
            sc -= pieceType(pc);
            return sc;
        }
        inline int quietScore(Move m) {
            int from = mFrom(m), to = mTo(m);
            int pc = pos.board[from];
            int idx2 = pc * 64 + to;
            return mainHist[pos.side][m & 4095] + (ss - 1)->cont[idx2] + (ss - 2)->cont[idx2] + (ss - 4)->cont[idx2] / 2;
        }

        Move pickBest(int from, int n) {
            int bi = from;
            for (int i = from + 1; i < n; i++) if (list.mv[i].score > list.mv[bi].score) bi = i;
            std::swap(list.mv[from], list.mv[bi]);
            return list.mv[from].m;
        }

        Move next(bool skipQuiets) {
            while (true) {
                switch (stage) {
                case ST_TT:
                    stage = inCheck ? ST_GEN_EVA : ST_GEN_CAPS;
                    return ttMove;
                case ST_GEN_CAPS:
                    list.n = 0;
                    generate<GEN_CAPTURES>(pos, list);
                    for (int i = 0; i < list.n; i++) list.mv[i].score = captureScore(list.mv[i].m);
                    idx = 0; stage = ST_CAPS;
                    break;
                case ST_CAPS:
                    while (idx < list.n) {
                        Move m = pickBest(idx, list.n);
                        int sc = list.mv[idx].score;
                        idx++;
                        if (m == ttMove) continue;
                        if (!(mFlags(m) & 8) && !pos.see(m, 0)) {
                            if (!qs && nBad < 64) { bad[nBad].m = m; bad[nBad].score = sc; nBad++; }
                            continue;
                        }
                        return m;
                    }
                    if (qs) { stage = ST_DONE; break; }
                    stage = ST_K1;
                    break;
                case ST_K1:
                    stage = ST_K2;
                    if (!skipQuiets && k1 && k1 != ttMove && !mIsCapture(k1) && pos.isPseudoLegal(k1)) return k1;
                    break;
                case ST_K2:
                    stage = ST_COUNTER;
                    if (!skipQuiets && k2 && k2 != ttMove && k2 != k1 && !mIsCapture(k2) && pos.isPseudoLegal(k2)) return k2;
                    break;
                case ST_COUNTER:
                    stage = ST_GEN_QUIETS;
                    if (!skipQuiets && counter && counter != ttMove && counter != k1 && counter != k2 && !mIsCapture(counter) && pos.isPseudoLegal(counter)) return counter;
                    break;
                case ST_GEN_QUIETS:
                    if (skipQuiets) { stage = ST_BAD; badIdx = 0; break; }
                    list.n = 0;
                    generate<GEN_QUIETS>(pos, list);
                    for (int i = 0; i < list.n; i++) list.mv[i].score = quietScore(list.mv[i].m);
                    idx = 0; stage = ST_QUIETS;
                    break;
                case ST_QUIETS:
                    if (!skipQuiets) {
                        while (idx < list.n) {
                            Move m = pickBest(idx, list.n);
                            idx++;
                            if (m == ttMove || m == k1 || m == k2 || m == counter) continue;
                            return m;
                        }
                    }
                    stage = ST_BAD; badIdx = 0;
                    break;
                case ST_BAD:
                    while (badIdx < nBad) {
                        Move m = bad[badIdx++].m;
                        if (m == ttMove) continue;
                        return m;
                    }
                    stage = ST_DONE;
                    break;
                case ST_GEN_EVA:
                    list.n = 0;
                    generate<GEN_EVASIONS>(pos, list);
                    for (int i = 0; i < list.n; i++) {
                        Move m = list.mv[i].m;
                        if (mIsCapture(m) || mIsPromo(m)) list.mv[i].score = 1000000 + captureScore(m);
                        else list.mv[i].score = quietScore(m);
                    }
                    idx = 0; stage = ST_EVA;
                    break;
                case ST_EVA:
                    while (idx < list.n) {
                        Move m = pickBest(idx, list.n);
                        idx++;
                        if (m == ttMove) continue;
                        return m;
                    }
                    stage = ST_DONE;
                    break;
                default:
                    return 0;
                }
            }
        }
    };

    // --- helpers ----------------------------------------------------------
    static void initTables() {
        for (int d = 1; d < 64; d++)
            for (int m = 1; m < 64; m++) {
                lmrTable[d][m] = (int)(LmrBase / 100.0 + std::log((double)d) * std::log((double)m) / (LmrDiv / 100.0));
                lmrTable16[d][m] = (int)(16.0 * (LmrBase / 100.0 + std::log((double)d) * std::log((double)m) / (LmrDiv / 100.0)));
            }
    }

    void clearHistory() {
        memset(mainHist, 0, sizeof(mainHist));
        memset(contHist, 0, sizeof(contHist));
        memset(contNone, 0, sizeof(contNone));
        memset(capHist, 0, sizeof(capHist));
        memset(counterMoves, 0, sizeof(counterMoves));
        memset(corrHist, 0, sizeof(corrHist));
    }

    inline void checkTime() {
        if (stopFlag.load(std::memory_order_relaxed)) { stopped = true; return; }
        if (nodeLimit && nodes >= nodeLimit) { stopped = true; return; }
        if (timed && nowMs() - startMs >= hardMs) stopped = true;
    }

    inline int drawScore() const { return 0; }

    void updatePV(int ply, Move m) {
        pvTable[ply][ply] = m;
        for (int i = ply + 1; i < pvLen[ply + 1]; i++) pvTable[ply][i] = pvTable[ply + 1][i];
        pvLen[ply] = std::max(pvLen[ply + 1], ply + 1);
    }

    // --- quiescence ------------------------------------------------------
    template <bool PV>
    int qsearch(int alpha, int beta, SSEntry* ss) {
        const int ply = ss->ply;
        nodes++;
        if (PV) pvLen[ply] = ply;
        if ((nodes & 2047) == 0) checkTime();
        if (stopped) return 0;
        if (ply > selDepth) selDepth = ply;
        if (pos.isDraw(ply)) return 0;
        if (ply >= MAXPLY - 1) return evaluate(pos);
        const bool inCheck = pos.checkers != 0;
        bool found;
        TTEntry* tte = TT.probe(pos.key, found);
        Move ttMove = found ? tte->move : 0;
        int ttScore = 0;
        if (found) {
            ttScore = scoreFromTT(tte->score, ply);
            int b = tte->bound();
            if (!PV && ((b == B_EXACT) || (b == B_LOWER && ttScore >= beta) || (b == B_UPPER && ttScore <= alpha)))
                return ttScore;
        }
        int best, standPat = -INF;
        if (inCheck) best = -MATE + ply;
        else {
            if (found && tte->eval != NO_EVAL) standPat = tte->eval;
            else standPat = evaluate(pos);
            best = standPat;
            if (best >= beta) {
                if (!found) TT.store(tte, pos.key, best, standPat, 0, B_LOWER, 0, ply);
                return best;
            }
            if (best > alpha) alpha = best;
        }
        Picker mp(*this, ss, ttMove, true);
        Move m, bestMove = 0;
        int moveCount = 0;
        const int origAlpha = alpha;
        while ((m = mp.next(false))) {
            if (!pos.isLegal(m)) continue;
            moveCount++;
            if (!inCheck && !mIsPromo(m) && mFlags(m) != F_EP) {
                int cap = pieceType(pos.board[mTo(m)]);
                if (standPat + SEE_VAL[cap] + 200 <= alpha) continue;
            }
            pos.make(m);
            (ss + 1)->ply = ply + 1;
            int score = -qsearch<PV>(-beta, -alpha, ss + 1);
            pos.unmake();
            if (stopped) return 0;
            if (score > best) {
                best = score;
                if (score > alpha) {
                    bestMove = m;
                    if (PV) updatePV(ply, m);
                    if (score >= beta) break;
                    alpha = score;
                }
            }
        }
        if (inCheck && moveCount == 0) return -MATE + ply;
        int bound = best >= beta ? B_LOWER : (PV && best > origAlpha ? B_EXACT : B_UPPER);
        TT.store(tte, pos.key, best, standPat == -INF ? NO_EVAL : standPat, 0, bound, bestMove, ply);
        return best;
    }

    // --- main search -----------------------------------------------------
    template <bool PV>
    int search(int alpha, int beta, int depth, SSEntry* ss, bool cutNode) {
        const int ply = ss->ply;
        const bool root = ply == 0;
        if (depth <= 0) return qsearch<PV>(alpha, beta, ss);
        nodes++;
        if (PV) pvLen[ply] = ply;
        if ((nodes & 1023) == 0) checkTime();
        if (stopped) return 0;
        if (ply > selDepth) selDepth = ply;
        const Move excluded = ss->excluded;
        const bool inCheck = pos.checkers != 0;
        if (!root) {
            if (pos.isDraw(ply)) return drawScore();
            if (ply >= MAXPLY - 1) return inCheck ? 0 : evaluate(pos);
            alpha = std::max(alpha, -MATE + ply);
            beta = std::min(beta, MATE - ply - 1);
            if (alpha >= beta) return alpha;
        }
        const int us = pos.side;
        // TT probe
        bool found = false;
        TTEntry* tte = TT.probe(pos.key ^ (excluded ? (U64)excluded * 0x9E3779B97F4A7C15ULL : 0), found);
        Move ttMove = found ? tte->move : 0;
        int ttScore = 0, ttDepth = 0, ttBound = 0;
        if (found) {
            ttScore = scoreFromTT(tte->score, ply);
            ttDepth = tte->depth; ttBound = tte->bound();
            if (!PV && ttDepth >= depth && pos.halfmove < 90 &&
                ((ttBound == B_EXACT) || (ttBound == B_LOWER && ttScore >= beta) || (ttBound == B_UPPER && ttScore <= alpha)))
                return ttScore;
        }
        if (root) ttMove = rootBest;

        // static eval
        int staticEval = NO_EVAL;
        if (!inCheck) {
            if (found && tte->eval != NO_EVAL) staticEval = tte->eval;
            else staticEval = evaluate(pos);
        }
        const int rawEval = staticEval;
        if (!inCheck) {
            int corr = corrHist[us][pos.pawnKey & 16383] * CorrPct / 1600;
            staticEval = std::max(-MATE_BOUND + 1, std::min(MATE_BOUND - 1, staticEval + corr));
        }
        ss->staticEval = staticEval;
        bool improving = false;
        if (!inCheck) {
            if ((ss - 2)->staticEval != NO_EVAL) improving = staticEval > (ss - 2)->staticEval;
            else if ((ss - 4)->staticEval != NO_EVAL) improving = staticEval > (ss - 4)->staticEval;
        }
        int eval = staticEval;
        if (found && !inCheck && ttBound != B_NONE && std::abs(ttScore) < MATE_BOUND) {
            if ((ttBound == B_LOWER && ttScore > eval) || (ttBound == B_UPPER && ttScore < eval) || ttBound == B_EXACT) eval = ttScore;
        }
        (ss + 1)->killers[0] = (ss + 1)->killers[1] = 0;
        (ss + 2)->killers[0] = (ss + 2)->killers[1] = 0;

        if (!PV && !inCheck && !excluded) {
            // razoring
            if (RazorOn && depth <= 3 && eval + RazorMargin * depth < alpha) {
                int v = qsearch<false>(alpha - 1, alpha, ss);
                if (v < alpha) return v;
                if (stopped) return 0;
            }
            // reverse futility
            if (RfpOn && depth <= RfpDepth && eval - RfpMargin * depth + RfpImp * improving >= beta && eval < MATE_BOUND && beta > -MATE_BOUND)
                return (eval + beta) / 2;
            // null move
            if (depth >= NmpMinDepth && eval >= beta && (ss - 1)->move != 0 && staticEval >= beta - NmpSlope * depth + NmpBase &&
                pos.hasNonPawnMaterial(us) && beta > -MATE_BOUND) {
                int R = NmpR + depth / NmpDiv + std::min((eval - beta) / NmpEvalDiv, 3);
                ss->move = 0; ss->pcto = 0; ss->cont = contNone;
                pos.makeNull();
                (ss + 1)->ply = ply + 1;
                (ss + 1)->excluded = 0;
                int score = -search<false>(-beta, -beta + 1, depth - R, ss + 1, !cutNode);
                pos.unmakeNull();
                if (stopped) return 0;
                if (score >= beta) {
                    if (score >= MATE_BOUND) score = beta;
                    return score;
                }
            }
        }
        // probcut
        if (!PV && !inCheck && !excluded && depth >= ProbcutDepth && std::abs(beta) < MATE_BOUND) {
            const int pcBeta = beta + ProbcutMargin - ProbcutImp * improving;
            if (!(found && ttDepth >= depth - 3 && ttScore < pcBeta)) {
                Picker pmp(*this, ss, ttMove, true);
                Move pm;
                while ((pm = pmp.next(false))) {
                    if (!pos.isLegal(pm)) continue;
                    if (!pos.see(pm, pcBeta - staticEval)) continue;
                    ss->move = pm; ss->pcto = pos.board[mFrom(pm)] * 64 + mTo(pm); ss->cont = contHist[ss->pcto];
                    pos.make(pm);
                    (ss + 1)->ply = ply + 1; (ss + 1)->excluded = 0;
                    int pcScore = -qsearch<false>(-pcBeta, -pcBeta + 1, ss + 1);
                    if (pcScore >= pcBeta) pcScore = -search<false>(-pcBeta, -pcBeta + 1, depth - 4, ss + 1, !cutNode);
                    pos.unmake();
                    if (stopped) return 0;
                    if (pcScore >= pcBeta) {
                        TT.store(tte, pos.key, pcScore, staticEval, depth - 3, B_LOWER, pm, ply);
                        return pcScore;
                    }
                }
            }
        }
        // internal iterative reduction
        if (!ttMove && depth >= IirDepth && (PV || cutNode) && !inCheck) depth--;

        Picker mp(*this, ss, ttMove, false);
        Move m, bestMove = 0;
        int best = -INF;
        const int origAlpha = alpha;
        int moveCount = 0, quietCount = 0, capCount = 0;
        Move quietsTried[64], capsTried[32];
        bool skipQuiets = false;

        while ((m = mp.next(skipQuiets))) {
            if (m == excluded) continue;
            if (!pos.isLegal(m)) continue;
            moveCount++;
            const bool isCap = mIsCapture(m);
            const bool isPromo = mIsPromo(m);
            const bool isQuiet = !isCap && !isPromo;
            const int from = mFrom(m), to = mTo(m);
            const int pc = pos.board[from];
            const int histScore = isQuiet ? mp.quietScore(m) : 0;

            if (!root && best > -MATE_BOUND && pos.hasNonPawnMaterial(us)) {
                if (isQuiet) {
                    if (LmpOn && moveCount >= (LmpBase + depth * depth) / (improving ? 1 : 2) + 1 && depth <= 7) { skipQuiets = true; continue; }
                    int lmrD = std::max(1, depth - lmrTable[std::min(depth, 63)][std::min(moveCount, 63)]);
                    if (FutOn && !inCheck && lmrD <= 8 && staticEval + FutBase + FutMult * lmrD <= alpha) { skipQuiets = true; continue; }
                    if (depth <= 5 && histScore < -HistPrune * depth) continue;
                    if (SeeOn && !pos.see(m, -SeeQuiet * lmrD)) continue;
                } else if (isCap && depth <= 6 && !pos.see(m, -SeeCap * depth)) continue;
            }

            int ext = 0;
            if (!root && depth >= SingDepth && m == ttMove && !excluded && ttDepth >= depth - 3 && (ttBound & B_LOWER) && std::abs(ttScore) < MATE_BOUND) {
                int sBeta = ttScore - SingMargin * depth;
                ss->excluded = m;
                int sScore = search<false>(sBeta - 1, sBeta, (depth - 1) / 2, ss, cutNode);
                ss->excluded = 0;
                if (stopped) return 0;
                if (sScore < sBeta) { ext = 1; if (!PV && sScore < sBeta - 25) ext = 2; }
                else if (sBeta >= beta) return sBeta;
                else if (ttScore >= beta) ext = -1;
            }

            ss->move = m; ss->pcto = pc * 64 + to; ss->cont = contHist[pc * 64 + to];
            TT.prefetch(pos.key);  // (cheap, key will be updated by make)
            pos.make(m);
            TT.prefetch(pos.key);
            (ss + 1)->ply = ply + 1;
            (ss + 1)->excluded = 0;
            const bool givesCheck = pos.checkers != 0;
            int newDepth = depth - 1 + ext;
            int score;
            if (CheckExt && givesCheck && ext == 0 && !root) newDepth++;  // check extension

            if (depth >= 3 && moveCount > 1 + (root ? 1 : 0)) {
                int R16 = lmrTable16[std::min(depth, 63)][std::min(moveCount, 63)];
                if (!isQuiet) R16 = std::max(0, R16 - LmrCapMinus);
                if (PV) R16 -= LmrPvMinus;
                if (cutNode) R16 += LmrCutPlus;
                if (improving) R16 -= LmrImpMinus;
                if (givesCheck) R16 -= LmrCheckMinus;
                if (isQuiet) {
                    if (m == ss->killers[0] || m == ss->killers[1]) R16 -= LmrKillerMinus;
                    R16 -= histScore * 16 / LmrHistDiv;
                }
                if (inCheck) R16 -= LmrInCheckMinus;
                int R = R16 >= 0 ? R16 / 16 : -((-R16) / 16);
                int d = std::max(1, std::min(newDepth - R, newDepth));
                score = -search<false>(-alpha - 1, -alpha, d, ss + 1, true);
                if (score > alpha && d < newDepth)
                    score = -search<false>(-alpha - 1, -alpha, newDepth, ss + 1, !cutNode);
            } else if (!PV || moveCount > 1) {
                score = -search<false>(-alpha - 1, -alpha, newDepth, ss + 1, !cutNode);
            } else score = alpha + 1;  // force full search below
            if (PV && (moveCount == 1 || score > alpha))
                score = -search<true>(-beta, -alpha, newDepth, ss + 1, false);
            pos.unmake();
            if (stopped) return 0;

            if (score > best) {
                best = score;
                if (score > alpha) {
                    bestMove = m;
                    if (PV) updatePV(ply, m);
                    if (root) { rootBest = m; rootScore = score; }
                    if (score >= beta) {
#ifdef STATS
                        stCutAll++; stCut[std::min(moveCount, 7)]++;
#endif
                        break;
                    }
                    alpha = score;
                }
            }
            if (isQuiet) { if (quietCount < 64) quietsTried[quietCount++] = m; }
            else if (isCap) { if (capCount < 32) capsTried[capCount++] = m; }
        }

        if (moveCount == 0) {
            if (excluded) return alpha;
            return inCheck ? -MATE + ply : drawScore();
        }
        // update histories on beta cutoff
        if (best >= beta && bestMove) {
            int bonus = std::min(HistBonusMax, HistBonusMul * depth - HistBonusSub);
            if (!mIsCapture(bestMove) && !mIsPromo(bestMove)) {
                if (ss->killers[0] != bestMove) { ss->killers[1] = ss->killers[0]; ss->killers[0] = bestMove; }
                int pcto = (ss - 1)->pcto;
                if (pcto) counterMoves[pcto] = bestMove;
                auto upd = [&](Move mv, int b) {
                    int pc = pos.board[mFrom(mv)], to = mTo(mv), i2 = pc * 64 + to;
                    updHist(mainHist[us][mv & 4095], b);
                    updHist((ss - 1)->cont[i2], b);
                    updHist((ss - 2)->cont[i2], b);
                    updHist((ss - 4)->cont[i2], b / 2);
                };
                upd(bestMove, bonus);
                for (int i = 0; i < quietCount; i++) if (quietsTried[i] != bestMove) upd(quietsTried[i], -bonus);
            } else {
                int pc = pos.board[mFrom(bestMove)], to = mTo(bestMove);
                int victim = mFlags(bestMove) == F_EP ? PAWN : (pos.board[to] == NO_PIECE ? PAWN : pieceType(pos.board[to]));
                updHist(capHist[pc * 64 + to][victim], bonus);
            }
            for (int i = 0; i < capCount; i++) {
                Move c = capsTried[i];
                if (c == bestMove) continue;
                int pc = pos.board[mFrom(c)], to = mTo(c);
                int victim = mFlags(c) == F_EP ? PAWN : (pos.board[to] == NO_PIECE ? PAWN : pieceType(pos.board[to]));
                updHist(capHist[pc * 64 + to][victim], -bonus);
            }
        }
        if (!excluded) {
            int bound = best >= beta ? B_LOWER : (best > origAlpha ? B_EXACT : B_UPPER);
            if (!inCheck && (!bestMove || !mIsCapture(bestMove)) && !(bound == B_LOWER && best <= staticEval) &&
                !(bound == B_UPPER && best >= staticEval) && std::abs(best) < MATE_BOUND) {
                int diff = std::max(-200, std::min(200, best - rawEval)) * 16;
                int w = std::min(depth + 1, 16);
                int16_t& e = corrHist[us][pos.pawnKey & 16383];
                e = (int16_t)std::max(-3200, std::min(3200, (int)e + (diff - (int)e) * w / 256));
            }
            TT.store(tte, pos.key, best, rawEval, depth, bound, bestMove, ply);
        }
        return best;
    }

    // --- iterative deepening --------------------------------------------
    void printInfo(int depth, int score, int64_t elapsed) {
        if (silent) return;
        std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(selDepth);
        if (std::abs(score) >= MATE_BOUND) {
            int mm = (MATE - std::abs(score) + 1) / 2;
            s += " score mate " + std::to_string(score > 0 ? mm : -mm);
        } else s += " score cp " + std::to_string(score);
        s += " nodes " + std::to_string(nodes);
        s += " nps " + std::to_string(elapsed > 0 ? nodes * 1000 / elapsed : nodes * 1000);
        s += " hashfull " + std::to_string(TT.hashfull());
        s += " time " + std::to_string(elapsed);
        s += " pv";
        for (int i = 0; i < pvLen[0]; i++) s += " " + moveToStr(pvTable[0][i]);
        s += "\n";
        fputs(s.c_str(), stdout);
        fflush(stdout);
    }

    void setupTime(const Limits& lim) {
        timed = false;
        softMs = hardMs = 1LL << 60;
        int us = pos.side;
        int64_t T = us == WHITE ? lim.wtime : lim.btime;
        int64_t inc = us == WHITE ? lim.winc : lim.binc;
        if (lim.movetime > 0) {
            timed = true;
            softMs = hardMs = std::max<int64_t>(1, lim.movetime - tun.moveOverhead);
        } else if (T >= 0 && !lim.infinite) {
            timed = true;
            int64_t avail = std::max<int64_t>(1, T - tun.moveOverhead);
            double base;
            if (lim.movestogo > 0) base = (double)T / (std::min(lim.movestogo, 40) + 1.5) + inc * 0.8;
            else base = (double)T / (double)TimeDiv + inc * IncPct / 100.0;
            softMs = (int64_t)std::min(base, avail * SoftMaxPct / 100.0);
            hardMs = (int64_t)std::min(softMs * HardMult10 / 10.0, avail * HardMaxPct / 100.0);
            if (hardMs < 2) hardMs = 2;
            if (softMs < 1) softMs = 1;
        }
        optMs = softMs;
    }

    Move go(const Limits& lim) {
        startMs = nowMs();
        stopped = false;
        nodes = 0; selDepth = 0;
        nodeLimit = lim.nodes;
        maxDepth = lim.depth > 0 ? std::min(lim.depth, MAXPLY - 2) : MAXPLY - 2;
        setupTime(lim);
        TT.age = (TT.age + 1) & 63;
        for (int i = 0; i < MAXPLY + 12; i++) {
            stackArr[i].move = 0; stackArr[i].staticEval = NO_EVAL; stackArr[i].excluded = 0;
            stackArr[i].killers[0] = stackArr[i].killers[1] = 0; stackArr[i].cont = contNone; stackArr[i].pcto = 0;
            stackArr[i].ply = i - 6;
        }
        rootBest = 0; rootScore = 0; rootDepthDone = 0;
        MoveList rl;
        generateLegal(pos, rl);
        rootMoveCount = rl.n;
        if (rl.n == 0) return 0;
        Move firstLegal = rl.mv[0].m;
        Move prevBest = 0;
        int prevScore = 0;
        int stableCount = 0;
        for (int depth = 1; depth <= maxDepth; depth++) {
            int delta = AspDelta;
            int alpha = -INF, beta = INF;
            if (depth >= 5) { alpha = std::max(-INF, prevScore - delta); beta = std::min(INF, prevScore + delta); }
            int score;
            while (true) {
                selDepth = 0;
                score = search<true>(alpha, beta, depth, stack, false);
                if (stopped) break;
                if (score <= alpha) {
                    beta = (alpha + beta) / 2;
                    alpha = std::max(-INF, score - delta);
                    delta += delta / 2;
                } else if (score >= beta) {
                    beta = std::min(INF, score + delta);
                    delta += delta / 2;
                } else break;
                if (delta > 600) { alpha = -INF; beta = INF; }
            }
            if (stopped) {
                if (depth == 1 && !rootBest) rootBest = firstLegal;
                break;
            }
            rootDepthDone = depth;
            finalScore = score;
            int64_t el = nowMs() - startMs;
            if (pvLen[0] > 0 && pvTable[0][0]) rootBest = pvTable[0][0];
            printInfo(depth, score, el);
            if (pvLen[0] > 1) rootPonder = pvTable[0][1]; else rootPonder = 0;
            // time management
            if (rootBest == prevBest) stableCount++; else stableCount = 0;
            double scale = 1.0;
            if (depth >= 6) {
                if (stableCount >= 4) scale *= 0.75;
                else if (stableCount == 0) scale *= 1.25;
                if (score < prevScore - 25) scale *= 1.25;
            }
            prevBest = rootBest;
            prevScore = score;
            if (timed) {
                if (rl.n == 1 && depth >= 4) break;
                if ((double)el > softMs * scale * StopFracPct / 100.0) break;
            }
            if (lim.softNodes && nodes >= lim.softNodes) break;
            if (std::abs(score) >= MATE_BOUND && depth >= (MATE - std::abs(score)) + 2 && !lim.infinite) break;
        }
        if (!rootBest) rootBest = firstLegal;
        return rootBest;
    }
};

static Search* S_ptr() {
    static Search* s = new Search();
    return s;
}
