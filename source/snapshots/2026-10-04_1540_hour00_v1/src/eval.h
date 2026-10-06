#pragma once
#include "movegen.h"

// ---------------------------------------------------------------------------
// Linear, tunable evaluation. Every term is  param[idx] * count  (mg/eg pair).
// ---------------------------------------------------------------------------
struct Param { int mg, eg; };

enum : int {
    P_PST = 0,                          // 6 * 64 (white-relative square, includes material)
    P_MOB_N = P_PST + 384,              // 9
    P_MOB_B = P_MOB_N + 9,              // 14
    P_MOB_R = P_MOB_B + 14,             // 15
    P_MOB_Q = P_MOB_R + 15,             // 28
    P_BISHOP_PAIR = P_MOB_Q + 28,
    P_PASSED = P_BISHOP_PAIR + 1,       // 8 by relative rank
    P_PASSED_BLOCKED = P_PASSED + 8,    // 8
    P_PASSED_KOWN = P_PASSED_BLOCKED + 8,
    P_PASSED_KENEMY = P_PASSED_KOWN + 1,
    P_ISOLATED = P_PASSED_KENEMY + 1,
    P_DOUBLED = P_ISOLATED + 1,
    P_BACKWARD = P_DOUBLED + 1,
    P_SUPPORTED = P_BACKWARD + 1,       // 8
    P_PHALANX = P_SUPPORTED + 8,        // 8
    P_ROOK_OPEN = P_PHALANX + 8,
    P_ROOK_SEMI = P_ROOK_OPEN + 1,
    P_ROOK_7TH = P_ROOK_SEMI + 1,
    P_KNIGHT_OUTPOST = P_ROOK_7TH + 1,
    P_BISHOP_OUTPOST = P_KNIGHT_OUTPOST + 1,
    P_SHIELD = P_BISHOP_OUTPOST + 1,    // 8
    P_KDANGER = P_SHIELD + 8,           // 32
    P_THREAT_PAWN = P_KDANGER + 32,     // 4 (N,B,R,Q attacked by pawn)
    P_THREAT_MINOR_R = P_THREAT_PAWN + 4,
    P_THREAT_MINOR_Q = P_THREAT_MINOR_R + 1,
    P_THREAT_ROOK_Q = P_THREAT_MINOR_Q + 1,
    P_HANGING = P_THREAT_ROOK_Q + 1,
    P_TEMPO = P_HANGING + 1,
    P_COUNT = P_TEMPO + 1
};

static Param P[P_COUNT];

static const int PST_VIS[2][6][64] = {
  { // mg
    { 0,0,0,0,0,0,0,0, 98,134,61,95,68,126,34,-11, -6,7,26,31,65,56,25,-20, -14,13,6,21,23,12,17,-23,
      -27,-2,-5,12,17,6,10,-25, -26,-4,-4,-10,3,3,33,-12, -35,-1,-20,-23,-15,24,38,-22, 0,0,0,0,0,0,0,0 },
    { -167,-89,-34,-49,61,-97,-15,-107, -73,-41,72,36,23,62,7,-17, -47,60,37,65,84,129,73,44, -9,17,19,53,37,69,18,22,
      -13,4,16,13,28,19,21,-8, -23,-9,12,10,19,17,25,-16, -29,-53,-12,-3,-1,18,-14,-19, -105,-21,-58,-33,-17,-28,-19,-23 },
    { -29,4,-82,-37,-25,-42,7,-8, -26,16,-18,-13,30,59,18,-47, -16,37,43,40,35,50,37,-2, -4,5,19,50,37,37,7,-2,
      -6,13,13,26,34,12,10,4, 0,15,15,15,14,27,18,10, 4,15,16,0,7,21,33,1, -33,-3,-14,-21,-13,-12,-39,-21 },
    { 32,42,32,51,63,9,31,43, 27,32,58,62,80,67,26,44, -5,19,26,36,17,45,61,16, -24,-11,7,26,24,35,-8,-20,
      -36,-26,-12,-1,9,-7,6,-23, -45,-25,-16,-17,3,0,-5,-33, -44,-16,-20,-9,-1,11,-6,-71, -19,-13,1,17,16,7,-37,-26 },
    { -28,0,29,12,59,44,43,45, -24,-39,-5,1,-16,57,28,54, -13,-17,7,8,29,56,47,57, -27,-27,-16,-16,-1,17,-2,1,
      -9,-26,-9,-10,-2,-4,3,-3, -14,2,-11,-2,-5,2,14,5, -35,-8,11,2,8,15,-3,1, -1,-18,-9,10,-15,-25,-31,-50 },
    { -65,23,16,-15,-56,-34,2,13, 29,-1,-20,-7,-8,-4,-38,-29, -9,24,2,-16,-20,6,22,-22, -17,-20,-12,-27,-30,-25,-14,-36,
      -49,-1,-27,-39,-46,-44,-33,-51, -14,-14,-22,-46,-44,-30,-15,-27, 1,7,-8,-64,-43,-16,9,8, -15,36,12,-54,8,-28,24,14 }
  },
  { // eg
    { 0,0,0,0,0,0,0,0, 178,173,158,134,147,132,165,187, 94,100,85,67,56,53,82,84, 32,24,13,5,-2,4,17,17,
      13,9,-3,-7,-7,-8,3,-1, 4,7,-6,1,0,-5,-1,-8, 13,8,8,10,13,0,2,-7, 0,0,0,0,0,0,0,0 },
    { -58,-38,-13,-28,-31,-27,-63,-99, -25,-8,-25,-2,-9,-25,-24,-52, -24,-20,10,9,-1,-9,-19,-41, -17,3,22,22,22,11,8,-18,
      -18,-6,16,25,16,17,4,-18, -23,-3,-1,15,10,-3,-20,-22, -42,-20,-10,-5,-2,-20,-23,-44, -29,-51,-23,-15,-22,-18,-50,-64 },
    { -14,-21,-11,-8,-7,-9,-17,-24, -8,-4,7,-12,-3,-13,-4,-14, 2,-8,0,-1,-2,6,0,4, -3,9,12,9,14,10,3,2,
      -6,3,13,19,7,10,-3,-9, -12,-3,8,10,13,3,-7,-15, -14,-18,-7,-1,4,-9,-15,-27, -23,-9,-23,-5,-9,-16,-5,-17 },
    { 13,10,18,15,12,12,8,5, 11,13,13,11,-3,3,8,3, 7,7,7,5,4,-3,-5,-3, 4,3,13,1,2,1,-1,2,
      3,5,8,4,-5,-6,-8,-11, -4,0,-5,-1,-7,-12,-8,-16, -6,-6,0,2,-9,-9,-11,-3, -9,2,3,-1,-5,-13,4,-20 },
    { -9,22,22,27,27,19,10,20, -17,20,32,41,58,25,30,0, -20,6,9,49,47,35,19,9, 3,22,24,45,57,40,57,36,
      -18,28,19,47,31,34,39,23, -16,-27,15,6,9,17,10,5, -22,-23,-30,-16,-16,-23,-36,-32, -33,-28,-22,-43,-5,-32,-20,-41 },
    { -74,-35,-18,-18,-11,15,4,-17, -12,17,14,17,17,38,23,11, 10,17,23,15,20,45,44,13, -8,22,24,27,26,33,26,3,
      -18,-4,21,24,27,23,9,-11, -19,-3,11,21,23,16,7,-9, -27,-11,4,13,14,4,-5,-17, -53,-34,-21,-11,-28,-14,-24,-43 }
  }
};

static void setDefaultParams() {
    static const int matMg[6] = {82, 337, 365, 477, 1025, 0};
    static const int matEg[6] = {94, 281, 297, 512, 936, 0};
    for (int pt = 0; pt < 6; pt++)
        for (int s = 0; s < 64; s++) {
            P[P_PST + pt * 64 + s].mg = PST_VIS[0][pt][s ^ 56] + matMg[pt];
            P[P_PST + pt * 64 + s].eg = PST_VIS[1][pt][s ^ 56] + matEg[pt];
        }
    for (int m = 0; m < 9; m++) P[P_MOB_N + m] = {4 * (m - 4), 4 * (m - 4)};
    for (int m = 0; m < 14; m++) P[P_MOB_B + m] = {3 * (m - 6), 4 * (m - 6)};
    for (int m = 0; m < 15; m++) P[P_MOB_R + m] = {2 * (m - 7), 4 * (m - 7)};
    for (int m = 0; m < 28; m++) P[P_MOB_Q + m] = {1 * (m - 13), 2 * (m - 13)};
    P[P_BISHOP_PAIR] = {30, 50};
    static const int pasMg[8] = {0, 0, 5, 10, 20, 35, 60, 0};
    static const int pasEg[8] = {0, 5, 10, 20, 40, 70, 110, 0};
    for (int r = 0; r < 8; r++) {
        P[P_PASSED + r] = {pasMg[r], pasEg[r]};
        P[P_PASSED_BLOCKED + r] = {-pasMg[r] / 4, -pasEg[r] / 3};
        P[P_SUPPORTED + r] = {r >= 2 && r <= 6 ? 2 + r * 2 : 0, r >= 2 && r <= 6 ? 2 + r * 2 : 0};
        P[P_PHALANX + r] = {r >= 2 && r <= 6 ? r * 2 : 0, r >= 2 && r <= 6 ? r * 3 : 0};
    }
    P[P_PASSED_KOWN] = {0, -3};
    P[P_PASSED_KENEMY] = {0, 4};
    P[P_ISOLATED] = {-10, -12};
    P[P_DOUBLED] = {-10, -20};
    P[P_BACKWARD] = {-8, -8};
    P[P_ROOK_OPEN] = {20, 10};
    P[P_ROOK_SEMI] = {10, 6};
    P[P_ROOK_7TH] = {15, 20};
    P[P_KNIGHT_OUTPOST] = {20, 15};
    P[P_BISHOP_OUTPOST] = {10, 8};
    static const int sh[8] = {-30, 15, 8, 0, -20, 15, 8, 0};
    for (int i = 0; i < 8; i++) P[P_SHIELD + i] = {sh[i], 0};
    for (int i = 0; i < 32; i++) P[P_KDANGER + i] = {-(i * i) / 3, -i / 2};
    P[P_THREAT_PAWN + 0] = {40, 30};
    P[P_THREAT_PAWN + 1] = {40, 30};
    P[P_THREAT_PAWN + 2] = {50, 30};
    P[P_THREAT_PAWN + 3] = {45, 30};
    P[P_THREAT_MINOR_R] = {25, 15};
    P[P_THREAT_MINOR_Q] = {25, 15};
    P[P_THREAT_ROOK_Q] = {20, 10};
    P[P_HANGING] = {15, 10};
    P[P_TEMPO] = {15, 5};
}

// ---- static tables --------------------------------------------------------
static U64 passMask[2][64], frontFile[2][64], backAdjMask[2][64], adjFilesBB[8], fileBB[8];
static U64 kingZone[2][64];

static void initEvalTables() {
    for (int f = 0; f < 8; f++) {
        fileBB[f] = FILE_A << f;
        adjFilesBB[f] = (f > 0 ? FILE_A << (f - 1) : 0) | (f < 7 ? FILE_A << (f + 1) : 0);
    }
    for (int s = 0; s < 64; s++) {
        int f = fileOf(s), r = rankOf(s);
        for (int c = 0; c < 2; c++) {
            U64 fwd = 0, back = 0;
            if (c == WHITE) {
                for (int rr = r + 1; rr < 8; rr++) fwd |= RANK_1 << (rr * 8);
                for (int rr = 0; rr <= r; rr++) back |= RANK_1 << (rr * 8);
            } else {
                for (int rr = r - 1; rr >= 0; rr--) fwd |= RANK_1 << (rr * 8);
                for (int rr = 7; rr >= r; rr--) back |= RANK_1 << (rr * 8);
            }
            frontFile[c][s] = fwd & fileBB[f];
            passMask[c][s] = fwd & (fileBB[f] | adjFilesBB[f]);
            backAdjMask[c][s] = back & adjFilesBB[f];
            U64 z = kingAtt[s] | sqbb(s);
            z |= (c == WHITE) ? (z << 8) : (z >> 8);
            kingZone[c][s] = z;
        }
    }
}

template <bool TR> struct Acc {
    int mg = 0, eg = 0;
    int* tr = nullptr;
    inline void add(int idx, int cnt, int sign) {
        if constexpr (TR) tr[idx] += sign * cnt;
        else { mg += sign * cnt * P[idx].mg; eg += sign * cnt * P[idx].eg; }
    }
};

static inline int cheb(int a, int b) { return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b))); }

// Fills accumulator; returns phase (0..24) and eg scale (0..128). White POV.
template <bool TR>
static void evalImpl(const Position& pos, Acc<TR>& A, int& phaseOut, int& scaleOut) {
    const U64 occ = pos.occ;
    const U64 pawnsBB[2] = {pos.pieces(WHITE, PAWN), pos.pieces(BLACK, PAWN)};
    U64 pawnAttacks[2];
    pawnAttacks[WHITE] = ((pawnsBB[WHITE] & ~FILE_A) << 7) | ((pawnsBB[WHITE] & ~FILE_H) << 9);
    pawnAttacks[BLACK] = ((pawnsBB[BLACK] & ~FILE_A) >> 9) | ((pawnsBB[BLACK] & ~FILE_H) >> 7);
    U64 attacked[2][6] = {};
    U64 allAtt[2] = {};
    int units[2] = {0, 0}, nAttackers[2] = {0, 0};
    int ksq[2] = {pos.kingSq(WHITE), pos.kingSq(BLACK)};

    int phase = 4 * popcount(pos.byType[QUEEN]) + 2 * popcount(pos.byType[ROOK]) + popcount(pos.byType[BISHOP] | pos.byType[KNIGHT]);
    if (phase > 24) phase = 24;

    for (int us = 0; us < 2; us++) {
        const int them = us ^ 1;
        const int sg = us == WHITE ? 1 : -1;
        const U64 zoneE = kingZone[them][ksq[them]];
        const U64 area = ~(pawnsBB[us] | pos.pieces(us, KING)) & ~pawnAttacks[them];
        attacked[us][PAWN] = pawnAttacks[us];
        attacked[us][KING] = kingAtt[ksq[us]];
        // PST for all pieces
        for (int pt = 0; pt < 6; pt++) {
            U64 bb = pos.pieces(us, pt);
            while (bb) {
                int s = pop_lsb(bb);
                A.add(P_PST + pt * 64 + (us == WHITE ? s : s ^ 56), 1, sg);
            }
        }
        U64 bb = pos.pieces(us, KNIGHT);
        while (bb) {
            int s = pop_lsb(bb);
            U64 a = knightAtt[s];
            attacked[us][KNIGHT] |= a;
            A.add(P_MOB_N + popcount(a & area), 1, sg);
            if (a & zoneE) { units[us] += 2; nAttackers[us]++; }
            int rr = us == WHITE ? rankOf(s) : 7 - rankOf(s);
            if (rr >= 3 && rr <= 5 && (pawnAtt[them][s] & pawnsBB[us]) && !(passMask[us][s] & adjFilesBB[fileOf(s)] & pawnsBB[them]))
                A.add(P_KNIGHT_OUTPOST, 1, sg);
        }
        bb = pos.pieces(us, BISHOP);
        int nb = popcount(bb);
        while (bb) {
            int s = pop_lsb(bb);
            U64 a = bishopAttacks(s, occ);
            attacked[us][BISHOP] |= a;
            A.add(P_MOB_B + popcount(a & area), 1, sg);
            if (a & zoneE) { units[us] += 2; nAttackers[us]++; }
            int rr = us == WHITE ? rankOf(s) : 7 - rankOf(s);
            if (rr >= 3 && rr <= 5 && (pawnAtt[them][s] & pawnsBB[us]) && !(passMask[us][s] & adjFilesBB[fileOf(s)] & pawnsBB[them]))
                A.add(P_BISHOP_OUTPOST, 1, sg);
        }
        if (nb >= 2) A.add(P_BISHOP_PAIR, 1, sg);
        bb = pos.pieces(us, ROOK);
        while (bb) {
            int s = pop_lsb(bb);
            U64 a = rookAttacks(s, occ);
            attacked[us][ROOK] |= a;
            A.add(P_MOB_R + popcount(a & area), 1, sg);
            if (a & zoneE) { units[us] += 3; nAttackers[us]++; }
            U64 ff = fileBB[fileOf(s)];
            if (!(ff & pawnsBB[us])) A.add((ff & pawnsBB[them]) ? P_ROOK_SEMI : P_ROOK_OPEN, 1, sg);
            int rr = us == WHITE ? rankOf(s) : 7 - rankOf(s);
            if (rr == 6) A.add(P_ROOK_7TH, 1, sg);
        }
        bb = pos.pieces(us, QUEEN);
        while (bb) {
            int s = pop_lsb(bb);
            U64 a = queenAttacks(s, occ);
            attacked[us][QUEEN] |= a;
            A.add(P_MOB_Q + popcount(a & area), 1, sg);
            if (a & zoneE) { units[us] += 5; nAttackers[us]++; }
        }
        for (int pt = 0; pt < 6; pt++) allAtt[us] |= attacked[us][pt];

        // pawn structure
        bb = pawnsBB[us];
        while (bb) {
            int s = pop_lsb(bb);
            int f = fileOf(s);
            int rr = us == WHITE ? rankOf(s) : 7 - rankOf(s);
            int stop = us == WHITE ? s + 8 : s - 8;
            bool passed = !(passMask[us][s] & pawnsBB[them]) && !(frontFile[us][s] & pawnsBB[us]);
            bool isolated = !(adjFilesBB[f] & pawnsBB[us]);
            if (frontFile[us][s] & pawnsBB[us]) A.add(P_DOUBLED, 1, sg);
            if (isolated) A.add(P_ISOLATED, 1, sg);
            else if (!(backAdjMask[us][s] & pawnsBB[us]) && (pawnAttacks[them] & sqbb(stop))) A.add(P_BACKWARD, 1, sg);
            if (pawnAtt[them][s] & pawnsBB[us]) A.add(P_SUPPORTED + rr, 1, sg);
            if (f < 7 && (pawnsBB[us] & sqbb(s + 1))) A.add(P_PHALANX + rr, 1, sg);
            if (passed) {
                A.add(P_PASSED + rr, 1, sg);
                if (occ & sqbb(stop)) A.add(P_PASSED_BLOCKED + rr, 1, sg);
                if (rr >= 3) {
                    A.add(P_PASSED_KOWN, cheb(ksq[us], stop), sg);
                    A.add(P_PASSED_KENEMY, cheb(ksq[them], stop), sg);
                }
            }
        }
    }
    // second pass: threats and king safety
    for (int us = 0; us < 2; us++) {
        const int them = us ^ 1;
        const int sg = us == WHITE ? 1 : -1;
        // threats by us on them
        U64 eN = pos.pieces(them, KNIGHT), eB = pos.pieces(them, BISHOP), eR = pos.pieces(them, ROOK), eQ = pos.pieces(them, QUEEN);
        A.add(P_THREAT_PAWN + 0, popcount(pawnAttacks[us] & eN), sg);
        A.add(P_THREAT_PAWN + 1, popcount(pawnAttacks[us] & eB), sg);
        A.add(P_THREAT_PAWN + 2, popcount(pawnAttacks[us] & eR), sg);
        A.add(P_THREAT_PAWN + 3, popcount(pawnAttacks[us] & eQ), sg);
        U64 minorAtt = attacked[us][KNIGHT] | attacked[us][BISHOP];
        A.add(P_THREAT_MINOR_R, popcount(minorAtt & eR), sg);
        A.add(P_THREAT_MINOR_Q, popcount(minorAtt & eQ), sg);
        A.add(P_THREAT_ROOK_Q, popcount(attacked[us][ROOK] & eQ), sg);
        U64 hang = (eN | eB | eR | eQ) & allAtt[us] & ~allAtt[them];
        A.add(P_HANGING, popcount(hang), sg);

        // king safety of 'us' (attacked by them)
        int k = ksq[us];
        int u = units[them];
        const U64 safe = ~pos.byColor[them] & ~allAtt[us];
        if (pos.pieces(them, QUEEN) || nAttackers[them] >= 2) {
            if (knightAtt[k] & attacked[them][KNIGHT] & safe) u += 3;
            if (bishopAttacks(k, occ) & attacked[them][BISHOP] & safe) u += 2;
            if (rookAttacks(k, occ) & attacked[them][ROOK] & safe) u += 3;
            if (queenAttacks(k, occ) & attacked[them][QUEEN] & safe) u += 4;
        }
        int idx = nAttackers[them] >= 2 ? std::min(u, 31) : (nAttackers[them] == 1 ? std::min(u, 31) / 3 : 0);
        A.add(P_KDANGER + idx, 1, sg);
        // pawn shield
        int kf = fileOf(k), kr = us == WHITE ? rankOf(k) : 7 - rankOf(k);
        if (kr <= 1) {
            for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); f++) {
                U64 fp = pawnsBB[us] & fileBB[f] & frontFile[us][k - fileOf(k) + f];
                int d = 0;
                if (fp) {
                    int ps = us == WHITE ? lsb(fp) : msb(fp);
                    d = std::min(3, std::abs(rankOf(ps) - rankOf(k)));
                }
                A.add(P_SHIELD + (f == kf ? 0 : 4) + d, 1, sg);
            }
        }
    }
    A.add(P_TEMPO, 1, pos.side == WHITE ? 1 : -1);

    // endgame scale factor (0..128) applied to eg term
    int scale = 128;
    {
        const int wN = popcount(pos.pieces(WHITE, KNIGHT)), bN = popcount(pos.pieces(BLACK, KNIGHT));
        const int wB = popcount(pos.pieces(WHITE, BISHOP)), bB = popcount(pos.pieces(BLACK, BISHOP));
        const int wR = popcount(pos.pieces(WHITE, ROOK)), bR = popcount(pos.pieces(BLACK, ROOK));
        const int wQ = popcount(pos.pieces(WHITE, QUEEN)), bQ = popcount(pos.pieces(BLACK, QUEEN));
        const int wP = popcount(pawnsBB[WHITE]), bP = popcount(pawnsBB[BLACK]);
        if (wB == 1 && bB == 1 && !wN && !bN && !wR && !bR && !wQ && !bQ) {
            U64 lightSq = 0x55AA55AA55AA55AAULL;
            bool wl = (pos.pieces(WHITE, BISHOP) & lightSq) != 0, bl = (pos.pieces(BLACK, BISHOP) & lightSq) != 0;
            if (wl != bl) scale = 64 + 8 * std::min(wP + bP, 8) / 2;
        } else if (!wP && !bP) {
            int wm = wN * 320 + wB * 330 + wR * 500 + wQ * 900, bm = bN * 320 + bB * 330 + bR * 500 + bQ * 900;
            // no pawns: small material edge is a draw
            if (std::abs(wm - bm) <= 330 && std::max(wm, bm) <= 1000) scale = 16;
        } else {
            // side with no pawns and only a minor-piece edge cannot win
            int wm = wN * 320 + wB * 330 + wR * 500 + wQ * 900, bm = bN * 320 + bB * 330 + bR * 500 + bQ * 900;
            if (!wP && wm - bm <= 330) scale = std::min(scale, 40);
            if (!bP && bm - wm <= 330) scale = std::min(scale, 40);
        }
    }
    phaseOut = phase;
    scaleOut = scale;
}

static inline bool insufficientMaterial(const Position& pos) {
    if (pos.byType[PAWN] | pos.byType[ROOK] | pos.byType[QUEEN]) return false;
    int minors = popcount(pos.byType[KNIGHT] | pos.byType[BISHOP]);
    if (minors <= 1) return true;
    return false;
}

// Static evaluation from side-to-move point of view (centipawns).
static int evaluate(const Position& pos) {
    if (insufficientMaterial(pos)) return 0;
    Acc<false> A;
    int phase, scale;
    evalImpl<false>(pos, A, phase, scale);
    int eg = A.eg * scale / 128;
    int score = (A.mg * phase + eg * (24 - phase)) / 24;
    // mop-up: winning side with a big material edge and no enemy pawns drives the king to the edge
    {
        int strong = score > 0 ? WHITE : BLACK;
        int weak = strong ^ 1;
        if (!pos.pieces(weak, PAWN) && std::abs(score) > 300 && phase <= 8 && (pos.byColor[weak] & ~pos.byType[KING]) == 0) {
            int wk = pos.kingSq(weak), sk = pos.kingSq(strong);
            int cd = std::max(3 - fileOf(wk), fileOf(wk) - 4) + std::max(3 - rankOf(wk), rankOf(wk) - 4);
            int bonus = cd * 10 + (14 - (std::abs(fileOf(wk) - fileOf(sk)) + std::abs(rankOf(wk) - rankOf(sk)))) * 4;
            score += strong == WHITE ? bonus : -bonus;
        }
    }
    return pos.side == WHITE ? score : -score;
}
