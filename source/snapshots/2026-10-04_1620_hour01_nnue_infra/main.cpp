#include "datagen.h"

static const char* ENGINE_NAME = "Sonnet 5.5 chess 24hrs";
static const char* ENGINE_AUTHOR = "Sonnet 5.5";
static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static std::mutex outMtx;
static std::thread searchThread;
static Search* S;
static int hashMB = 256;

static void initAll() {
    initBitboards();
    initZobrist();
    initEvalTables();
    setDefaultParams();
#if __has_include("tuned.h")
#include "tuned.h"
#endif
    Search::initTables();
}

static bool setTune(const std::string& name, int v) {
    for (auto& tp : tuneList()) if (name == tp.name) { *tp.ptr = std::max(tp.mn, std::min(tp.mx, v)); Search::initTables(); return true; }
    return false;
}

static void stopSearch() {
    if (searchThread.joinable()) {
        S->stopFlag = true;
        searchThread.join();
    }
}

static void setPosition(std::istringstream& is) {
    std::string tok;
    is >> tok;
    Position& pos = S->pos;
    if (tok == "startpos") {
        pos.setFen(START_FEN);
        is >> tok;
    } else if (tok == "fen") {
        std::string fen;
        while (is >> tok && tok != "moves") fen += tok + " ";
        if (!pos.setFen(fen)) pos.setFen(START_FEN);
    }
    if (tok == "moves") {
        while (is >> tok) {
            Move m = parseMove(pos, tok);
            if (!m) break;
            pos.make(m);
            if (pos.hply > HIST_MAX - 300) {
                // rebase history to avoid overflow in very long games
                std::string fen = pos.getFen();
                pos.setFen(fen);
            }
        }
    }
}

static void doGo(std::istringstream& is) {
    Limits lim;
    std::string tok;
    while (is >> tok) {
        if (tok == "wtime") is >> lim.wtime;
        else if (tok == "btime") is >> lim.btime;
        else if (tok == "winc") is >> lim.winc;
        else if (tok == "binc") is >> lim.binc;
        else if (tok == "movestogo") is >> lim.movestogo;
        else if (tok == "movetime") is >> lim.movetime;
        else if (tok == "depth") is >> lim.depth;
        else if (tok == "nodes") is >> lim.nodes;
        else if (tok == "infinite") lim.infinite = true;
    }
    stopSearch();
    S->stopFlag = false;
    searchThread = std::thread([lim]() {
        Move best = S->go(lim);
        if (lim.infinite || (lim.depth > 0 && !S->timed)) {
            // UCI: don't print bestmove before 'stop' in infinite mode
            if (lim.infinite) while (!S->stopFlag) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::string s = "bestmove " + moveToStr(best);
        if (S->rootPonder && best) s += " ponder " + moveToStr(S->rootPonder);
        s += "\n";
        std::lock_guard<std::mutex> g(outMtx);
        fputs(s.c_str(), stdout);
        fflush(stdout);
    });
}

static void bench(int depth) {
    static const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "2rq1rk1/pp2bppp/2n1pn2/3p4/3P1B2/2NBPN2/PP3PPP/2RQ1RK1 w - - 0 11",
        "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1",
        "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
        "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    };
    U64 total = 0;
    double t0 = nowSec();
    S->silent = true;
    for (const char* f : fens) {
        S->pos.setFen(f);
        Limits lim; lim.depth = depth;
        S->clearHistory(); TT.clear();
        S->stopFlag = false;
        S->go(lim);
        total += S->nodes;
    }
    double t = nowSec() - t0;
    printf("bench depth %d: nodes %llu time %.2fs nps %.0f\n", depth, total, t, total / t);
    S->silent = false;
}

int main(int argc, char** argv) {
    initAll();
    S = S_ptr();
    TT.resize(hashMB);
    nnueLoadEmbedded();
    S->clearHistory();
    S->pos.setFen(START_FEN);
    if (argc > 1) {
        std::string a = argv[1];
        if (a == "nncheck") return nnCheckRun(argc, argv, S);
        if (a == "datagen") return datagenRun(argc, argv, S);
        if (a == "bench") { bench(argc > 2 ? atoi(argv[2]) : 12); return 0; }
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string cmd;
        if (!(is >> cmd)) continue;
        if (cmd == "uci") {
            std::lock_guard<std::mutex> g(outMtx);
            printf("id name %s\nid author %s\n", ENGINE_NAME, ENGINE_AUTHOR);
            printf("option name Hash type spin default %d min 1 max 16384\n", hashMB);
            printf("option name EvalFile type string default <embedded>\n");
            for (auto& tp : tuneList()) printf("option name %s type spin default %d min %d max %d\n", tp.name, *tp.ptr, tp.mn, tp.mx);
            printf("option name Threads type spin default 1 min 1 max 1\n");
            printf("option name Move Overhead type spin default %d min 0 max 5000\n", tun.moveOverhead);
            printf("uciok\n");
            fflush(stdout);
        } else if (cmd == "isready") {
            std::lock_guard<std::mutex> g(outMtx);
            printf("readyok\n");
            fflush(stdout);
        } else if (cmd == "ucinewgame") {
            stopSearch();
            TT.clear();
            S->clearHistory();
            S->pos.setFen(START_FEN);
        } else if (cmd == "setoption") {
            std::string tok, name, value;
            is >> tok;  // name
            while (is >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
            while (is >> tok) value += (value.empty() ? "" : " ") + tok;
            if (name == "Hash") {
                stopSearch();
                int mb = std::max(1, std::min(16384, atoi(value.c_str())));
                if (mb != hashMB) { hashMB = mb; TT.resize(hashMB); }
            } else if (name == "EvalFile") {
                stopSearch();
                if (nnueLoadFile(value)) { std::string fen = S->pos.getFen(); S->pos.setFen(fen); }
                else { printf("info string failed to load net %s\n", value.c_str()); fflush(stdout); }
            } else if (setTune(name, atoi(value.c_str()))) {
            } else if (name == "Move Overhead") tun.moveOverhead = std::max(0, atoi(value.c_str()));
        } else if (cmd == "position") {
            stopSearch();
            setPosition(is);
        } else if (cmd == "go") {
            doGo(is);
        } else if (cmd == "stop") {
            if (searchThread.joinable()) {
                S->stopFlag = true;
                searchThread.join();
            }
        } else if (cmd == "quit") {
            stopSearch();
            return 0;
        } else if (cmd == "d") {
            printf("%s\n", S->pos.getFen().c_str());
        } else if (cmd == "eval") {
            printf("eval (stm) %d\n", evaluate(S->pos));
        } else if (cmd == "perft") {
            int d = 5; is >> d;
            double t0 = nowSec();
            U64 n = perft(S->pos, d);
            printf("perft %d = %llu (%.2fs)\n", d, n, nowSec() - t0);
        } else if (cmd == "bench") {
            int d = 12; is >> d;
            stopSearch();
            bench(d);
        }
    }
    stopSearch();
    return 0;
}
