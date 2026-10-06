#include "movegen.h"

int main(int argc, char** argv) {
    initBitboards(); initZobrist();
    static Position pos;
    int maxDepth = argc > 2 ? atoi(argv[2]) : 5;
    std::ifstream f(argv[1]);
    std::string line;
    int bad = 0, total = 0;
    double t0 = nowSec();
    U64 nodes = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        size_t p = line.find(';');
        std::string fen = line.substr(0, p);
        pos.setFen(fen);
        std::string rest = line.substr(p);
        for (int d = 1; d <= maxDepth; d++) {
            std::string tag = ";D" + std::to_string(d) + " ";
            size_t q = rest.find(tag);
            if (q == std::string::npos) continue;
            U64 expect = strtoull(rest.c_str() + q + tag.size(), nullptr, 10);
            U64 got = perft(pos, d);
            nodes += got; total++;
            if (got != expect) { bad++; printf("FAIL %s depth %d expected %llu got %llu\n", fen.c_str(), d, expect, got); }
        }
    }
    double t = nowSec() - t0;
    printf("checks=%d failures=%d nodes=%llu time=%.2fs (%.1f Mnps)\n", total, bad, nodes, t, nodes / t / 1e6);
    return bad != 0;
}
