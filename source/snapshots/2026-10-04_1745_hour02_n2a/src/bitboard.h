#pragma once
#include "types.h"

static U64 knightAtt[64], kingAtt[64], pawnAtt[2][64];
static U64 betweenBB[64][64], lineBB[64][64];

struct MagicEntry { U64 mask; U64* tab; };
static MagicEntry rookM[64], bishM[64];
static U64 attTable[107648 + 64];

inline U64 rookAttacks(int s, U64 occ) { const MagicEntry& m = rookM[s]; return m.tab[_pext_u64(occ, m.mask)]; }
inline U64 bishopAttacks(int s, U64 occ) { const MagicEntry& m = bishM[s]; return m.tab[_pext_u64(occ, m.mask)]; }
inline U64 queenAttacks(int s, U64 occ) { return rookAttacks(s, occ) | bishopAttacks(s, occ); }

static U64 slideSlow(int sq, U64 occ, const int dirs[4][2]) {
    U64 res = 0;
    for (int d = 0; d < 4; d++) {
        int f = fileOf(sq) + dirs[d][0], r = rankOf(sq) + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            int s = r * 8 + f;
            res |= sqbb(s);
            if (occ & sqbb(s)) break;
            f += dirs[d][0]; r += dirs[d][1];
        }
    }
    return res;
}
static U64 maskSlow(int sq, const int dirs[4][2]) {
    U64 res = 0;
    for (int d = 0; d < 4; d++) {
        int f = fileOf(sq) + dirs[d][0], r = rankOf(sq) + dirs[d][1];
        while (f + dirs[d][0] >= 0 && f + dirs[d][0] < 8 && r + dirs[d][1] >= 0 && r + dirs[d][1] < 8) {
            res |= sqbb(r * 8 + f);
            f += dirs[d][0]; r += dirs[d][1];
        }
    }
    return res;
}

static void initBitboards() {
    static const int rd[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    static const int bd[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    static const int kn[8][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};
    static const int kg[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int s = 0; s < 64; s++) {
        int f = fileOf(s), r = rankOf(s);
        knightAtt[s] = kingAtt[s] = pawnAtt[0][s] = pawnAtt[1][s] = 0;
        for (int i = 0; i < 8; i++) {
            int nf = f + kn[i][0], nr = r + kn[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) knightAtt[s] |= sqbb(nr * 8 + nf);
            nf = f + kg[i][0]; nr = r + kg[i][1];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) kingAtt[s] |= sqbb(nr * 8 + nf);
        }
        if (r < 7) {
            if (f > 0) pawnAtt[WHITE][s] |= sqbb(s + 7);
            if (f < 7) pawnAtt[WHITE][s] |= sqbb(s + 9);
        }
        if (r > 0) {
            if (f > 0) pawnAtt[BLACK][s] |= sqbb(s - 9);
            if (f < 7) pawnAtt[BLACK][s] |= sqbb(s - 7);
        }
    }
    U64* p = attTable;
    for (int s = 0; s < 64; s++) {
        MagicEntry& m = rookM[s];
        m.mask = maskSlow(s, rd); m.tab = p;
        U64 sub = 0;
        do { m.tab[_pext_u64(sub, m.mask)] = slideSlow(s, sub, rd); sub = (sub - m.mask) & m.mask; } while (sub);
        p += 1ULL << popcount(m.mask);
    }
    for (int s = 0; s < 64; s++) {
        MagicEntry& m = bishM[s];
        m.mask = maskSlow(s, bd); m.tab = p;
        U64 sub = 0;
        do { m.tab[_pext_u64(sub, m.mask)] = slideSlow(s, sub, bd); sub = (sub - m.mask) & m.mask; } while (sub);
        p += 1ULL << popcount(m.mask);
    }
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            betweenBB[a][b] = lineBB[a][b] = 0;
            if (a == b) continue;
            if (rookAttacks(a, 0) & sqbb(b)) {
                betweenBB[a][b] = rookAttacks(a, sqbb(b)) & rookAttacks(b, sqbb(a));
                lineBB[a][b] = (rookAttacks(a, 0) & rookAttacks(b, 0)) | sqbb(a) | sqbb(b);
            } else if (bishopAttacks(a, 0) & sqbb(b)) {
                betweenBB[a][b] = bishopAttacks(a, sqbb(b)) & bishopAttacks(b, sqbb(a));
                lineBB[a][b] = (bishopAttacks(a, 0) & bishopAttacks(b, 0)) | sqbb(a) | sqbb(b);
            }
        }
}
