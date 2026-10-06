#pragma once
#include "position.h"

struct ScoredMove { Move m; int score; };
struct MoveList {
    ScoredMove mv[256];
    int n = 0;
    inline void add(Move m) { mv[n].m = m; mv[n].score = 0; n++; }
};

enum GenType { GEN_CAPTURES, GEN_QUIETS, GEN_EVASIONS };

template <GenType GT>
static void generate(const Position& pos, MoveList& ml) {
    const int us = pos.side, them = us ^ 1;
    const U64 occ = pos.occ, enemy = pos.byColor[them], own = pos.byColor[us];
    const int ksq = pos.kingSq(us);
    U64 target;
    if (GT == GEN_EVASIONS) {
        if (moreThanOne(pos.checkers)) {
            U64 ka = kingAtt[ksq] & ~own;
            while (ka) { int to = pop_lsb(ka); ml.add(mkMove(ksq, to, (enemy & sqbb(to)) ? F_CAPTURE : F_QUIET)); }
            return;
        }
        int csq = lsb(pos.checkers);
        target = betweenBB[ksq][csq] | sqbb(csq);
    } else if (GT == GEN_CAPTURES) target = enemy;
    else target = ~occ;

    for (int pt = KNIGHT; pt <= QUEEN; pt++) {
        U64 bb = pos.pieces(us, pt);
        while (bb) {
            int from = pop_lsb(bb);
            U64 att;
            if (pt == KNIGHT) att = knightAtt[from];
            else if (pt == BISHOP) att = bishopAttacks(from, occ);
            else if (pt == ROOK) att = rookAttacks(from, occ);
            else att = queenAttacks(from, occ);
            att &= target;
            while (att) {
                int to = pop_lsb(att);
                ml.add(mkMove(from, to, (enemy & sqbb(to)) ? F_CAPTURE : F_QUIET));
            }
        }
    }
    {
        U64 ka = kingAtt[ksq] & ~own;
        if (GT == GEN_CAPTURES) ka &= enemy;
        else if (GT == GEN_QUIETS) ka &= ~occ;
        while (ka) { int to = pop_lsb(ka); ml.add(mkMove(ksq, to, (enemy & sqbb(to)) ? F_CAPTURE : F_QUIET)); }
    }
    // pawns
    {
        const int up = us == WHITE ? 8 : -8;
        const int promoRank = us == WHITE ? 6 : 1;
        const int startRank = us == WHITE ? 1 : 6;
        U64 pawns = pos.pieces(us, PAWN);
        while (pawns) {
            int from = pop_lsb(pawns);
            bool promo = rankOf(from) == promoRank;
            if (GT != GEN_QUIETS) {
                U64 caps = pawnAtt[us][from] & enemy;
                if (GT == GEN_EVASIONS) caps &= target;
                while (caps) {
                    int to = pop_lsb(caps);
                    if (promo) {
                        ml.add(mkMove(from, to, F_PROMOCAP_Q)); ml.add(mkMove(from, to, F_PROMOCAP_R));
                        ml.add(mkMove(from, to, F_PROMOCAP_B)); ml.add(mkMove(from, to, F_PROMOCAP_N));
                    } else ml.add(mkMove(from, to, F_CAPTURE));
                }
                if (pos.ep != NO_SQ && (pawnAtt[us][from] & sqbb(pos.ep))) ml.add(mkMove(from, pos.ep, F_EP));
            }
            int one = from + up;
            if (!(occ & sqbb(one))) {
                bool oneOk = GT != GEN_EVASIONS || (target & sqbb(one));
                if (promo) {
                    if (oneOk) {
                        if (GT == GEN_CAPTURES) ml.add(mkMove(from, one, F_PROMO_Q));
                        else if (GT == GEN_QUIETS) {
                            ml.add(mkMove(from, one, F_PROMO_R)); ml.add(mkMove(from, one, F_PROMO_B)); ml.add(mkMove(from, one, F_PROMO_N));
                        } else {
                            ml.add(mkMove(from, one, F_PROMO_Q)); ml.add(mkMove(from, one, F_PROMO_R));
                            ml.add(mkMove(from, one, F_PROMO_B)); ml.add(mkMove(from, one, F_PROMO_N));
                        }
                    }
                } else if (GT != GEN_CAPTURES) {
                    if (oneOk) ml.add(mkMove(from, one, F_QUIET));
                    if (rankOf(from) == startRank) {
                        int two = one + up;
                        if (!(occ & sqbb(two)) && (GT != GEN_EVASIONS || (target & sqbb(two))))
                            ml.add(mkMove(from, two, F_DOUBLE));
                    }
                }
            }
        }
    }
    if (GT == GEN_QUIETS && !pos.checkers) {
        if (us == WHITE) {
            if ((pos.castling & 1) && !(occ & (sqbb(5) | sqbb(6)))) ml.add(mkMove(4, 6, F_KCASTLE));
            if ((pos.castling & 2) && !(occ & (sqbb(1) | sqbb(2) | sqbb(3)))) ml.add(mkMove(4, 2, F_QCASTLE));
        } else {
            if ((pos.castling & 4) && !(occ & (sqbb(61) | sqbb(62)))) ml.add(mkMove(60, 62, F_KCASTLE));
            if ((pos.castling & 8) && !(occ & (sqbb(57) | sqbb(58) | sqbb(59)))) ml.add(mkMove(60, 58, F_QCASTLE));
        }
    }
}

// all legal moves
static void generateLegal(const Position& pos, MoveList& out) {
    MoveList tmp;
    if (pos.checkers) generate<GEN_EVASIONS>(pos, tmp);
    else { generate<GEN_CAPTURES>(pos, tmp); generate<GEN_QUIETS>(pos, tmp); }
    out.n = 0;
    for (int i = 0; i < tmp.n; i++) if (pos.isLegal(tmp.mv[i].m)) out.add(tmp.mv[i].m);
}

static U64 perft(Position& pos, int depth) {
    MoveList ml;
    generateLegal(pos, ml);
    if (depth == 1) return ml.n;
    U64 n = 0;
    for (int i = 0; i < ml.n; i++) {
        pos.make(ml.mv[i].m);
        n += perft(pos, depth - 1);
        pos.unmake();
    }
    return n;
}

static Move parseMove(Position& pos, const std::string& s) {
    if (s.size() < 4) return 0;
    MoveList ml;
    generateLegal(pos, ml);
    for (int i = 0; i < ml.n; i++)
        if (moveToStr(ml.mv[i].m) == s) return ml.mv[i].m;
    return 0;
}
