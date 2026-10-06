#pragma once
#include "bitboard.h"
#include "nnue.h"

static U64 zPiece[12][64], zCastle[16], zEp[8], zSide;
static int castleMask[64];

static const int SEE_VAL[7] = {100, 320, 330, 500, 900, 20000, 0};

static U64 splitmix(U64& x) {
    U64 z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void initZobrist() {
    U64 seed = 0x1234567890ABCDEFULL;
    for (int p = 0; p < 12; p++) for (int s = 0; s < 64; s++) zPiece[p][s] = splitmix(seed);
    for (int i = 0; i < 16; i++) zCastle[i] = splitmix(seed);
    for (int i = 0; i < 8; i++) zEp[i] = splitmix(seed);
    zSide = splitmix(seed);
    for (int s = 0; s < 64; s++) castleMask[s] = 15;
    castleMask[4] &= ~3; castleMask[0] &= ~2; castleMask[7] &= ~1;
    castleMask[60] &= ~12; castleMask[56] &= ~8; castleMask[63] &= ~4;
}

struct St {
    U64 key, checkers, pinned, pawnKey;
    int castling, ep, halfmove, captured;
    Move move;
};

constexpr int HIST_MAX = 2048;

struct Position {
    U64 byType[6], byColor[2], occ;
    int board[64];
    int side, castling, ep, halfmove, fullmove;
    U64 key, checkers, pinned, pawnKey;
    int hply;
    St hist[HIST_MAX];
    alignas(32) int16_t acc[HIST_MAX + 1][2][NN_H];

    inline void nnRefresh(int p) {
        int16_t* a = acc[hply][p];
        for (int i = 0; i < NN_H; i++) a[i] = gNet->b1[i];
        int ks = lsb(byType[KING] & byColor[p]);
        for (int s = 0; s < 64; s++) {
            if (board[s] == NO_PIECE) continue;
            const int16_t* w = gNet->w1 + nnFeature(p, board[s], s, ks) * NN_H;
            for (int i = 0; i < NN_H; i++) a[i] += w[i];
        }
    }
    void refreshAcc() { nnRefresh(0); nnRefresh(1); }
    inline void nnApply(const int* addPc, const int* addSq, int na, const int* subPc, const int* subSq, int ns,
                        int kingColor, int kFrom, int kTo) {
        for (int p = 0; p < 2; p++) {
            if (NN_KB > 1 && p == kingColor) {
                int rf = p == 0 ? kFrom : (kFrom ^ 56), rt = p == 0 ? kTo : (kTo ^ 56);
                if (nnKingInfo(rf) != nnKingInfo(rt)) { nnRefresh(p); continue; }
            }
            const int ks = lsb(byType[KING] & byColor[p]);
            const int16_t* src = acc[hply - 1][p];
            int16_t* dst = acc[hply][p];
            const int16_t* a0 = gNet->w1 + nnFeature(p, addPc[0], addSq[0], ks) * NN_H;
            const int16_t* s0 = gNet->w1 + nnFeature(p, subPc[0], subSq[0], ks) * NN_H;
            if (na == 1 && ns == 1) {
                for (int i = 0; i < NN_H; i++) dst[i] = src[i] + a0[i] - s0[i];
            } else if (na == 1) {
                const int16_t* s1 = gNet->w1 + nnFeature(p, subPc[1], subSq[1], ks) * NN_H;
                for (int i = 0; i < NN_H; i++) dst[i] = src[i] + a0[i] - s0[i] - s1[i];
            } else {
                const int16_t* a1 = gNet->w1 + nnFeature(p, addPc[1], addSq[1], ks) * NN_H;
                const int16_t* s1 = gNet->w1 + nnFeature(p, subPc[1], subSq[1], ks) * NN_H;
                for (int i = 0; i < NN_H; i++) dst[i] = src[i] + a0[i] + a1[i] - s0[i] - s1[i];
            }
        }
    }

    inline U64 pieces(int c, int pt) const { return byType[pt] & byColor[c]; }
    inline int kingSq(int c) const { return lsb(byType[KING] & byColor[c]); }

    inline void addPiece(int pc, int sq) {
        U64 b = sqbb(sq);
        byType[pieceType(pc)] |= b; byColor[pieceColor(pc)] |= b; occ |= b; board[sq] = pc;
    }
    inline void delPiece(int pc, int sq) {
        U64 b = ~sqbb(sq);
        byType[pieceType(pc)] &= b; byColor[pieceColor(pc)] &= b; occ &= b; board[sq] = NO_PIECE;
    }

    inline U64 attackersTo(int sq, U64 o) const {
        return (pawnAtt[BLACK][sq] & pieces(WHITE, PAWN)) | (pawnAtt[WHITE][sq] & pieces(BLACK, PAWN))
             | (knightAtt[sq] & byType[KNIGHT]) | (kingAtt[sq] & byType[KING])
             | (bishopAttacks(sq, o) & (byType[BISHOP] | byType[QUEEN]))
             | (rookAttacks(sq, o) & (byType[ROOK] | byType[QUEEN]));
    }
    // is sq attacked by color 'by' given occupancy o
    inline bool attackedBy(int sq, int by, U64 o) const {
        return (pawnAtt[by ^ 1][sq] & pieces(by, PAWN)) || (knightAtt[sq] & pieces(by, KNIGHT))
            || (kingAtt[sq] & pieces(by, KING))
            || (bishopAttacks(sq, o) & (pieces(by, BISHOP) | pieces(by, QUEEN)))
            || (rookAttacks(sq, o) & (pieces(by, ROOK) | pieces(by, QUEEN)));
    }

    void updateCheckInfo() {
        int us = side, them = us ^ 1;
        int ksq = kingSq(us);
        checkers = attackersTo(ksq, occ) & byColor[them];
        pinned = 0;
        U64 snipers = ((rookAttacks(ksq, 0) & (byType[ROOK] | byType[QUEEN])) |
                       (bishopAttacks(ksq, 0) & (byType[BISHOP] | byType[QUEEN]))) & byColor[them];
        while (snipers) {
            int s = pop_lsb(snipers);
            U64 b = betweenBB[ksq][s] & occ;
            if (b && !moreThanOne(b)) pinned |= b & byColor[us];
        }
    }

    void computeKey() {
        key = 0; pawnKey = 0;
        for (int s = 0; s < 64; s++) if (board[s] != NO_PIECE) { key ^= zPiece[board[s]][s]; if (pieceType(board[s]) == PAWN) pawnKey ^= zPiece[board[s]][s]; }
        key ^= zCastle[castling];
        if (ep != NO_SQ) key ^= zEp[fileOf(ep)];
        if (side == BLACK) key ^= zSide;
    }

    bool setFen(const std::string& fen) {
        std::istringstream ss(fen);
        std::string placement, stm, cast, eps;
        int hm = 0, fm = 1;
        if (!(ss >> placement >> stm)) return false;
        if (!(ss >> cast)) cast = "-";
        if (!(ss >> eps)) eps = "-";
        if (!(ss >> hm)) hm = 0;
        if (!(ss >> fm)) fm = 1;
        memset(byType, 0, sizeof(byType)); memset(byColor, 0, sizeof(byColor)); occ = 0;
        for (int i = 0; i < 64; i++) board[i] = NO_PIECE;
        int f = 0, r = 7;
        for (char c : placement) {
            if (c == '/') { r--; f = 0; }
            else if (c >= '1' && c <= '8') f += c - '0';
            else {
                const char* pcs = "PNBRQKpnbrqk";
                const char* p = strchr(pcs, c);
                if (!p || f > 7 || r < 0) return false;
                addPiece((int)(p - pcs), r * 8 + f);
                f++;
            }
        }
        if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return false;
        side = (stm == "b") ? BLACK : WHITE;
        if (attackedBy(kingSq(side ^ 1), side, occ)) return false;  // side not to move is in check: illegal
        castling = 0;
        for (char c : cast) {
            if (c == 'K') castling |= 1; else if (c == 'Q') castling |= 2;
            else if (c == 'k') castling |= 4; else if (c == 'q') castling |= 8;
        }
        if (board[4] != mkPiece(WHITE, KING)) castling &= ~3;
        if (board[60] != mkPiece(BLACK, KING)) castling &= ~12;
        if (board[7] != mkPiece(WHITE, ROOK)) castling &= ~1;
        if (board[0] != mkPiece(WHITE, ROOK)) castling &= ~2;
        if (board[63] != mkPiece(BLACK, ROOK)) castling &= ~4;
        if (board[56] != mkPiece(BLACK, ROOK)) castling &= ~8;
        ep = NO_SQ;
        if (eps.size() == 2 && eps[0] >= 'a' && eps[0] <= 'h' && eps[1] >= '1' && eps[1] <= '8') {
            int e = (eps[1] - '1') * 8 + (eps[0] - 'a');
            // only keep if a pawn of side to move can capture
            if (pawnAtt[side ^ 1][e] & pieces(side, PAWN)) ep = e;
        }
        halfmove = hm; fullmove = fm;
        hply = 0;
        computeKey();
        updateCheckInfo();
        if (useNNUE) refreshAcc();
        return true;
    }

    std::string getFen() const {
        std::string s;
        const char* pcs = "PNBRQKpnbrqk";
        for (int r = 7; r >= 0; r--) {
            int e = 0;
            for (int f = 0; f < 8; f++) {
                int p = board[r * 8 + f];
                if (p == NO_PIECE) e++;
                else { if (e) { s += char('0' + e); e = 0; } s += pcs[p]; }
            }
            if (e) s += char('0' + e);
            if (r) s += '/';
        }
        s += side == WHITE ? " w " : " b ";
        if (!castling) s += "-";
        else { if (castling & 1) s += 'K'; if (castling & 2) s += 'Q'; if (castling & 4) s += 'k'; if (castling & 8) s += 'q'; }
        s += " ";
        if (ep == NO_SQ) s += "-"; else { s += char('a' + fileOf(ep)); s += char('1' + rankOf(ep)); }
        s += " " + std::to_string(halfmove) + " " + std::to_string(fullmove);
        return s;
    }

    void make(Move m) {
        St& s = hist[hply++];
        s.key = key; s.pawnKey = pawnKey; s.checkers = checkers; s.pinned = pinned; s.castling = castling; s.ep = ep;
        s.halfmove = halfmove; s.move = m;
        int from = mFrom(m), to = mTo(m), fl = mFlags(m);
        int pc = board[from], us = side, pt = pieceType(pc);
        const bool nn = useNNUE;
        int addPc[2], addSq[2], subPc[2], subSq[2], na = 0, ns = 0;
        int cap = NO_PIECE;
        halfmove++;
        key ^= zSide;
        if (ep != NO_SQ) { key ^= zEp[fileOf(ep)]; ep = NO_SQ; }
        if (fl == F_EP) {
            int capSq = us == WHITE ? to - 8 : to + 8;
            cap = board[capSq];
            if (pieceType(cap) == PAWN) pawnKey ^= zPiece[cap][capSq];
            delPiece(cap, capSq); key ^= zPiece[cap][capSq]; halfmove = 0;
            subPc[ns] = cap; subSq[ns++] = capSq;
        } else if (fl & 4) {
            cap = board[to];
            if (pieceType(cap) == PAWN) pawnKey ^= zPiece[cap][to];
            delPiece(cap, to); key ^= zPiece[cap][to]; halfmove = 0;
            subPc[ns] = cap; subSq[ns++] = to;
        }
        s.captured = cap;
        delPiece(pc, from); key ^= zPiece[pc][from];
        if (pt == PAWN) pawnKey ^= zPiece[pc][from];
        subPc[ns] = pc; subSq[ns++] = from;
        int npc = pc;
        if (fl & 8) npc = mkPiece(us, (fl & 3) + 1);
        addPiece(npc, to); key ^= zPiece[npc][to];
        if (pt == PAWN && !(fl & 8)) pawnKey ^= zPiece[npc][to];
        addPc[na] = npc; addSq[na++] = to;
        if (pt == PAWN) {
            halfmove = 0;
            if (fl == F_DOUBLE) {
                int e = (from + to) / 2;
                if (pawnAtt[us][e] & pieces(us ^ 1, PAWN)) { ep = e; key ^= zEp[fileOf(e)]; }
            }
        }
        if (fl == F_KCASTLE || fl == F_QCASTLE) {
            int rf = fl == F_KCASTLE ? to + 1 : to - 2;
            int rt = fl == F_KCASTLE ? to - 1 : to + 1;
            int rk = mkPiece(us, ROOK);
            delPiece(rk, rf); addPiece(rk, rt);
            subPc[ns] = rk; subSq[ns++] = rf; addPc[na] = rk; addSq[na++] = rt;
            key ^= zPiece[rk][rf] ^ zPiece[rk][rt];
        }
        int nc = castling & castleMask[from] & castleMask[to];
        if (nc != castling) { key ^= zCastle[castling] ^ zCastle[nc]; castling = nc; }
        if (nn) nnApply(addPc, addSq, na, subPc, subSq, ns, pt == KING ? us : -1, from, to);
        side ^= 1;
        updateCheckInfo();
    }

    void unmake() {
        St& s = hist[--hply];
        Move m = s.move;
        side ^= 1;
        int us = side;
        int from = mFrom(m), to = mTo(m), fl = mFlags(m);
        int pc = board[to];
        if (fl & 8) {
            delPiece(pc, to);
            addPiece(mkPiece(us, PAWN), from);
            if (fl & 4) addPiece(s.captured, to);
        } else {
            delPiece(pc, to);
            addPiece(pc, from);
            if (fl == F_EP) {
                int capSq = us == WHITE ? to - 8 : to + 8;
                addPiece(s.captured, capSq);
            } else if (fl & 4) addPiece(s.captured, to);
            else if (fl == F_KCASTLE || fl == F_QCASTLE) {
                int rf = fl == F_KCASTLE ? to + 1 : to - 2;
                int rt = fl == F_KCASTLE ? to - 1 : to + 1;
                int rk = mkPiece(us, ROOK);
                delPiece(rk, rt); addPiece(rk, rf);
            }
        }
        key = s.key; pawnKey = s.pawnKey; castling = s.castling; ep = s.ep; halfmove = s.halfmove;
        checkers = s.checkers; pinned = s.pinned;
    }

    void makeNull() {
        St& s = hist[hply++];
        s.key = key; s.pawnKey = pawnKey; s.checkers = checkers; s.pinned = pinned; s.castling = castling; s.ep = ep;
        s.halfmove = halfmove; s.move = 0; s.captured = NO_PIECE;
        if (useNNUE) memcpy(acc[hply], acc[hply - 1], sizeof(acc[0]));
        key ^= zSide;
        if (ep != NO_SQ) { key ^= zEp[fileOf(ep)]; ep = NO_SQ; }
        halfmove++;
        side ^= 1;
        updateCheckInfo();
    }
    void unmakeNull() {
        St& s = hist[--hply];
        side ^= 1;
        key = s.key; pawnKey = s.pawnKey; castling = s.castling; ep = s.ep; halfmove = s.halfmove;
        checkers = s.checkers; pinned = s.pinned;
    }

    // repetition / fifty-move draw. ply = distance from search root.
    bool isDraw(int ply) const {
        if (halfmove >= 100) return true;
        int end = std::min(halfmove, hply);
        int cnt = 0;
        for (int i = 4; i <= end; i += 2) {
            if (hist[hply - i].key == key) {
                if (i <= ply) return true;
                if (++cnt >= 2) return true;
            }
        }
        // i == 2 can't repeat (needs 4 plies minimum), handled by loop start
        return false;
    }

    bool isLegal(Move m) const {
        int from = mFrom(m), to = mTo(m), fl = mFlags(m);
        int us = side, them = us ^ 1;
        int ksq = kingSq(us);
        if (fl == F_EP) {
            int capSq = us == WHITE ? to - 8 : to + 8;
            U64 o = (occ ^ sqbb(from) ^ sqbb(capSq)) | sqbb(to);
            return !(rookAttacks(ksq, o) & (pieces(them, ROOK) | pieces(them, QUEEN)))
                && !(bishopAttacks(ksq, o) & (pieces(them, BISHOP) | pieces(them, QUEEN)));
        }
        if (board[from] == mkPiece(us, KING)) {
            if (fl == F_KCASTLE || fl == F_QCASTLE) {
                if (checkers) return false;
                int mid = fl == F_KCASTLE ? from + 1 : from - 1;
                return !attackedBy(mid, them, occ) && !attackedBy(to, them, occ);
            }
            return !attackedBy(to, them, occ ^ sqbb(from));
        }
        if (checkers) {
            if (moreThanOne(checkers)) return false;
            int csq = lsb(checkers);
            if (!((betweenBB[ksq][csq] | sqbb(csq)) & sqbb(to))) return false;
        }
        if (pinned & sqbb(from)) return (lineBB[ksq][from] & sqbb(to)) != 0;
        return true;
    }

    bool isPseudoLegal(Move m) const {
        if (!m) return false;
        int from = mFrom(m), to = mTo(m), fl = mFlags(m);
        int us = side;
        int pc = board[from];
        if (pc == NO_PIECE || pieceColor(pc) != us) return false;
        int pt = pieceType(pc);
        int tgt = board[to];
        if (fl == F_KCASTLE || fl == F_QCASTLE) {
            if (pt != KING) return false;
            if (us == WHITE) {
                if (from != 4) return false;
                if (fl == F_KCASTLE) return (castling & 1) && to == 6 && !(occ & (sqbb(5) | sqbb(6)));
                return (castling & 2) && to == 2 && !(occ & (sqbb(1) | sqbb(2) | sqbb(3)));
            } else {
                if (from != 60) return false;
                if (fl == F_KCASTLE) return (castling & 4) && to == 62 && !(occ & (sqbb(61) | sqbb(62)));
                return (castling & 8) && to == 58 && !(occ & (sqbb(57) | sqbb(58) | sqbb(59)));
            }
        }
        if (tgt != NO_PIECE && pieceColor(tgt) == us) return false;
        bool isCap = fl & 4;
        if (fl == F_EP) {
            return pt == PAWN && to == ep;
        }
        if (isCap != (tgt != NO_PIECE)) return false;
        if (pt == PAWN) {
            int up = us == WHITE ? 8 : -8;
            bool lastRank = us == WHITE ? rankOf(to) == 7 : rankOf(to) == 0;
            if (((fl & 8) != 0) != lastRank) return false;
            if (isCap) return (pawnAtt[us][from] & sqbb(to)) != 0;
            if (fl == F_DOUBLE) {
                int startRank = us == WHITE ? 1 : 6;
                return rankOf(from) == startRank && to == from + 2 * up && board[from + up] == NO_PIECE && tgt == NO_PIECE;
            }
            return to == from + up && tgt == NO_PIECE;
        }
        if (fl != F_QUIET && fl != F_CAPTURE) return false;
        U64 att;
        switch (pt) {
            case KNIGHT: att = knightAtt[from]; break;
            case BISHOP: att = bishopAttacks(from, occ); break;
            case ROOK: att = rookAttacks(from, occ); break;
            case QUEEN: att = queenAttacks(from, occ); break;
            default: att = kingAtt[from]; break;
        }
        return (att & sqbb(to)) != 0;
    }

    bool see(Move m, int threshold) const {
        int fl = mFlags(m);
        if (fl == F_EP || fl == F_KCASTLE || fl == F_QCASTLE) return 0 >= threshold;
        int from = mFrom(m), to = mTo(m);
        int cp = board[to];
        int swap = (cp == NO_PIECE ? 0 : SEE_VAL[pieceType(cp)]) - threshold;
        if (swap < 0) return false;
        swap = SEE_VAL[pieceType(board[from])] - swap;
        if (swap <= 0) return true;
        U64 o = occ ^ sqbb(from) ^ sqbb(to);
        int stm = side;
        U64 attackers = attackersTo(to, o);
        int res = 1;
        while (true) {
            stm ^= 1;
            attackers &= o;
            U64 stmAtt = attackers & byColor[stm];
            if (!stmAtt) break;
            res ^= 1;
            U64 bb;
            if ((bb = stmAtt & byType[PAWN])) {
                if ((swap = SEE_VAL[PAWN] - swap) < res) break;
                o ^= bb & -bb;
                attackers |= bishopAttacks(to, o) & (byType[BISHOP] | byType[QUEEN]);
            } else if ((bb = stmAtt & byType[KNIGHT])) {
                if ((swap = SEE_VAL[KNIGHT] - swap) < res) break;
                o ^= bb & -bb;
            } else if ((bb = stmAtt & byType[BISHOP])) {
                if ((swap = SEE_VAL[BISHOP] - swap) < res) break;
                o ^= bb & -bb;
                attackers |= bishopAttacks(to, o) & (byType[BISHOP] | byType[QUEEN]);
            } else if ((bb = stmAtt & byType[ROOK])) {
                if ((swap = SEE_VAL[ROOK] - swap) < res) break;
                o ^= bb & -bb;
                attackers |= rookAttacks(to, o) & (byType[ROOK] | byType[QUEEN]);
            } else if ((bb = stmAtt & byType[QUEEN])) {
                if ((swap = SEE_VAL[QUEEN] - swap) < res) break;
                o ^= bb & -bb;
                attackers |= (bishopAttacks(to, o) & (byType[BISHOP] | byType[QUEEN])) | (rookAttacks(to, o) & (byType[ROOK] | byType[QUEEN]));
            } else {
                return (attackers & ~byColor[stm]) ? res ^ 1 : res;
            }
        }
        return res;
    }

    bool hasNonPawnMaterial(int c) const {
        return (byColor[c] & ~byType[PAWN] & ~byType[KING]) != 0;
    }
};

static std::string moveToStr(Move m) {
    if (!m) return "0000";
    std::string s;
    int from = mFrom(m), to = mTo(m), fl = mFlags(m);
    s += char('a' + fileOf(from)); s += char('1' + rankOf(from));
    s += char('a' + fileOf(to)); s += char('1' + rankOf(to));
    if (fl & 8) s += "nbrq"[fl & 3];
    return s;
}
