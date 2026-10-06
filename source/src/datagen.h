#pragma once
#include "search.h"

// 32-byte packed training position
#pragma pack(push, 1)
struct PackedPos {
    U64 occ;
    uint8_t pcs[16];   // nibble per occupied square (ascending square order)
    int16_t score;     // white POV, centipawns (search score)
    uint8_t result;    // white POV: 0 = loss, 1 = draw, 2 = win
    uint8_t stm;       // side to move
    uint8_t pad[4];
};
#pragma pack(pop)
static_assert(sizeof(PackedPos) == 32, "PackedPos size");

static void packPosition(const Position& pos, int scoreWhite, PackedPos& pp) {
    memset(&pp, 0, sizeof(pp));
    pp.occ = pos.occ;
    U64 o = pos.occ;
    int n = 0;
    while (o) {
        int s = pop_lsb(o);
        pp.pcs[n >> 1] |= (uint8_t)(pos.board[s] << ((n & 1) * 4));
        n++;
    }
    pp.score = (int16_t)scoreWhite;
    pp.stm = (uint8_t)pos.side;
}

struct Rng {
    U64 s;
    explicit Rng(U64 seed) : s(seed * 0x9E3779B97F4A7C15ULL + 12345) { for (int i = 0; i < 8; i++) next(); }
    U64 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int below(int n) { return (int)(next() % (U64)n); }
};

// ---- SAN parsing + PGN book loading (for opening variety in datagen) ----
static Move parseSAN(Position& pos, std::string t) {
    while (!t.empty() && (t.back() == '+' || t.back() == '#' || t.back() == '!' || t.back() == '?')) t.pop_back();
    if (t.empty()) return 0;
    MoveList ml; generateLegal(pos, ml);
    if (t == "O-O" || t == "0-0" || t == "O-O-O" || t == "0-0-0") {
        int fl = (t.size() == 3) ? F_KCASTLE : F_QCASTLE;
        for (int i = 0; i < ml.n; i++) if (mFlags(ml.mv[i].m) == fl) return ml.mv[i].m;
        return 0;
    }
    int pt = PAWN, idx = 0;
    if (strchr("NBRQK", t[0])) { pt = std::string("PNBRQK").find(t[0]); idx = 1; }
    int promo = -1;
    size_t eq = t.find('=');
    if (eq != std::string::npos) { promo = std::string("PNBRQK").find(t[eq + 1]); t = t.substr(0, eq); }
    if (t.size() < (size_t)idx + 2) return 0;
    int to = (t[t.size() - 1] - '1') * 8 + (t[t.size() - 2] - 'a');
    std::string mid = t.substr(idx, t.size() - 2 - idx);
    int dFile = -1, dRank = -1;
    for (char c : mid) { if (c >= 'a' && c <= 'h') dFile = c - 'a'; else if (c >= '1' && c <= '8') dRank = c - '1'; }
    for (int i = 0; i < ml.n; i++) {
        Move m = ml.mv[i].m;
        if (mTo(m) != to) continue;
        if (pieceType(pos.board[mFrom(m)]) != pt) continue;
        if (mIsPromo(m) != (promo >= 0)) continue;
        if (promo >= 0 && (int)((mFlags(m) & 3) + 1) != promo) continue;
        if (dFile >= 0 && fileOf(mFrom(m)) != dFile) continue;
        if (dRank >= 0 && rankOf(mFrom(m)) != dRank) continue;
        return m;
    }
    return 0;
}

static std::vector<std::string> loadBook(const std::string& path) {
    std::vector<std::string> res;
    std::ifstream f(path);
    std::string line, cur;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '[') continue;
        cur += line + " ";
        if (line.find("1-0") != std::string::npos || line.find("0-1") != std::string::npos ||
            line.find("1/2-1/2") != std::string::npos || line.find('*') != std::string::npos) {
            res.push_back(cur); cur.clear();
        }
    }
    return res;
}

static bool playBookLine(Position& pos, const std::string& text) {
    std::istringstream is(text);
    std::string tok;
    while (is >> tok) {
        if (tok.find('.') != std::string::npos) { size_t p = tok.find_last_of('.'); tok = tok.substr(p + 1); if (tok.empty()) continue; }
        if (tok == "1-0" || tok == "0-1" || tok == "1/2-1/2" || tok == "*") break;
        Move m = parseSAN(pos, tok);
        if (!m) return false;
        pos.make(m);
    }
    return true;
}

static int nnCheckRun(int argc, char** argv, Search* S) {
    // nncheck <net.nnue> <out.bin> : dump random positions with the engine NNUE eval for cross-checking against the trainer
    if (argc < 4 || !nnueLoadFile(argv[2])) { fprintf(stderr, "bad args\n"); return 1; }
    FILE* fo = fopen(argv[3], "wb");
    Rng rng(777);
    int n = 0;
    for (int g = 0; g < 300; g++) {
        Position& pos = S->pos;
        pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        for (int ply = 0; ply < 90; ply++) {
            MoveList ml; generateLegal(pos, ml);
            if (ml.n == 0) break;
            pos.make(ml.mv[rng.below(ml.n)].m);
            if (ply > 6 && ply % 3 == 0) {
                // incremental accumulators vs refresh
                int e1 = evaluate(pos);
                int16_t saved[2][NN_H]; memcpy(saved, pos.acc[pos.hply], sizeof(saved));
                pos.refreshAcc();
                if (memcmp(saved, pos.acc[pos.hply], sizeof(saved)) != 0) { fprintf(stderr, "ACC MISMATCH at game %d ply %d\n", g, ply); return 2; }
                PackedPos pp; packPosition(pos, pos.side == WHITE ? e1 : -e1, pp); pp.result = 1;
                fwrite(&pp, sizeof(pp), 1, fo); n++;
            }
        }
    }
    fclose(fo);
    fprintf(stderr, "wrote %d positions, accumulator consistency OK\n", n);
    return 0;
}

static int datagenRun(int argc, char** argv, Search* S) {
    // datagen <outfile> <minutes> <softNodes> <seed> [randPlies]
    if (argc < 6) { fprintf(stderr, "usage: datagen out.bin minutes softNodes seed [randPlies]\n"); return 1; }
    std::string outFile = argv[2];
    double minutes = atof(argv[3]);
    U64 softNodes = strtoull(argv[4], nullptr, 10);
    U64 seed = strtoull(argv[5], nullptr, 10);
    int randPlies = argc > 6 ? atoi(argv[6]) : 8;
    std::string bookPath = argc > 7 ? argv[7] : "";
    int bookPct = argc > 8 ? atoi(argv[8]) : 0;
    if (argc > 9 && std::string(argv[9]) != "none") { if (!nnueLoadFile(argv[9])) { fprintf(stderr, "cannot load net" ); return 1; } fprintf(stderr, "using net %s", argv[9]); }
    std::vector<std::string> book;
    if (!bookPath.empty() && bookPath != "none") { book = loadBook(bookPath); fprintf(stderr, "book lines: %zu\n", book.size()); }
    Rng rng(seed);
    FILE* fo = fopen(outFile.c_str(), "ab");
    if (!fo) { fprintf(stderr, "cannot open %s\n", outFile.c_str()); return 1; }
    S->silent = true;
    TT.resize(32);
    double tEnd = nowSec() + minutes * 60.0;
    U64 games = 0, positions = 0;
    std::vector<PackedPos> buf;
    static const char* START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    double lastReport = nowSec();
    while (nowSec() < tEnd) {
        Position& pos = S->pos;
        pos.setFen(START);
        TT.clear();
        S->clearHistory();
        // random opening
        const bool useBook = !book.empty() && rng.below(100) < bookPct;
        int rp = useBook ? rng.below(3) : randPlies + rng.below(2);
        bool ok = true;
        if (useBook && !playBookLine(pos, book[rng.below((int)book.size())])) continue;
        for (int i = 0; i < rp; i++) {
            MoveList ml; generateLegal(pos, ml);
            if (ml.n == 0) { ok = false; break; }
            pos.make(ml.mv[rng.below(ml.n)].m);
        }
        if (!ok) continue;
        {
            MoveList ml; generateLegal(pos, ml);
            if (ml.n == 0) continue;
            // balance check
            Limits l; l.depth = 8;
            S->stopFlag = false;
            Move bm = S->go(l);
            if (!bm || std::abs(S->finalScore) > (useBook ? 500 : 350)) continue;
            // history of this search must not influence the game: reset
            TT.clear();
        }
        buf.clear();
        int result = -1;  // white POV 0/1/2
        int winPlies = 0, drawPlies = 0;
        int ply = 0;
        while (true) {
            MoveList ml; generateLegal(pos, ml);
            if (ml.n == 0) {
                if (pos.checkers) result = pos.side == WHITE ? 0 : 2; else result = 1;
                break;
            }
            if (pos.isDraw(0) || insufficientMaterial(pos)) { result = 1; break; }
            if (ply >= 400) { result = 1; break; }
            Limits l;
            l.softNodes = softNodes; l.nodes = softNodes * 10;
            S->stopFlag = false;
            Move bm = S->go(l);
            int sc = S->finalScore;  // stm POV
            int scW = pos.side == WHITE ? sc : -sc;
            bool quiet = !pos.checkers && !mIsCapture(bm) && !mIsPromo(bm) && std::abs(sc) < 3000;
            if (quiet) {
                PackedPos pp; packPosition(pos, scW, pp);
                buf.push_back(pp);
            }
            if (std::abs(sc) >= 1500) winPlies++; else winPlies = 0;
            if (std::abs(sc) <= 8 && ply > 60) drawPlies++; else drawPlies = 0;
            if (winPlies >= 6) { result = scW > 0 ? 2 : 0; break; }
            if (drawPlies >= 12) { result = 1; break; }
            if (std::abs(sc) >= MATE_BOUND) { result = scW > 0 ? 2 : 0; break; }
            pos.make(bm);
            ply++;
        }
        for (auto& p : buf) p.result = (uint8_t)result;
        fwrite(buf.data(), sizeof(PackedPos), buf.size(), fo);
        positions += buf.size();
        games++;
        if (nowSec() - lastReport > 60) {
            fflush(fo);
            fprintf(stderr, "[seed %llu] games %llu positions %llu\n", (unsigned long long)seed, (unsigned long long)games, (unsigned long long)positions);
            lastReport = nowSec();
        }
    }
    fclose(fo);
    fprintf(stderr, "[seed %llu] done: games %llu positions %llu\n", (unsigned long long)seed, (unsigned long long)games, (unsigned long long)positions);
    return 0;
}
