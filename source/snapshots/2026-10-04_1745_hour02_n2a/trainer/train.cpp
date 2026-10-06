// NNUE trainer: (768 -> HID) x 2 perspectives -> 1, SCReLU, NB output buckets.
// Data: 32-byte PackedPos records produced by the engine's datagen mode.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <filesystem>
#include <omp.h>

#ifndef HID
#define HID 256
#endif
#ifndef NN_KB
#define NN_KB 1
#endif
constexpr int KB = NN_KB;
constexpr int NF = 768 * KB;
constexpr int H = HID;
constexpr int NB = 8;
constexpr float SCORE_K = 400.0f;

#pragma pack(push, 1)
struct PackedPos {
    uint64_t occ;
    uint8_t pcs[16];
    int16_t score;
    uint8_t result;
    uint8_t stm;
    uint8_t pad[4];
};
#pragma pack(pop)

struct Net {
    std::vector<float> w1, b1, wo, b2;
    Net() : w1(NF * H), b1(H), wo(NB * 2 * H), b2(NB) {}
};

struct Sample {
    int n;
    uint16_t own[32], opp[32];
    int bucket;
    float target;
};

static inline float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

static float lambdaScore = 0.7f;

static inline int kingInfo(int ks) {
    if (KB == 1) return 0;
    int mirror = (ks & 7) >= 4;
    int f = mirror ? (ks & 7) ^ 7 : (ks & 7);
    int r = ks >> 3;
    int b;
    if (KB == 4) b = r <= 1 ? (f <= 1 ? 0 : 1) : (r <= 3 ? 2 : 3);
    else b = r == 0 ? (f <= 1 ? 0 : 1) : (r == 1 ? 2 : (r <= 3 ? 3 : 4));
    return (b << 1) | mirror;
}
static inline int featureIdx(int p, int pc, int sq, int ksq) {
    int c = pc >= 6, t = c ? pc - 6 : pc;
    int s = (p == 0 ? sq : (sq ^ 56));
    int base = ((c == p ? 0 : 1) * 6 + t) * 64;
    if (KB == 1) return base + s;
    int ks = (p == 0 ? ksq : (ksq ^ 56));
    int info = kingInfo(ks);
    if (info & 1) s ^= 7;
    return (info >> 1) * 768 + base + s;
}

static inline void decode(const PackedPos& pp, Sample& s) {
    uint64_t o = pp.occ;
    int n = 0;
    int stm = pp.stm;
    int pcs[32], sqs[32];
    int ksq[2] = {0, 0};
    while (o) {
        int sq = __builtin_ctzll(o); o &= o - 1;
        int pc = (pp.pcs[n >> 1] >> ((n & 1) * 4)) & 15;
        pcs[n] = pc; sqs[n] = sq;
        if (pc == 5) ksq[0] = sq; else if (pc == 11) ksq[1] = sq;
        n++;
    }
    for (int k = 0; k < n; k++) {
        int fw = featureIdx(0, pcs[k], sqs[k], ksq[0]);
        int fb = featureIdx(1, pcs[k], sqs[k], ksq[1]);
        if (stm == 0) { s.own[k] = fw; s.opp[k] = fb; } else { s.own[k] = fb; s.opp[k] = fw; }
    }
    s.n = n;
    s.bucket = std::min(NB - 1, (n - 2) / 4);
    if (s.bucket < 0) s.bucket = 0;
    float sc = pp.score; if (stm) sc = -sc;
    float r = pp.result * 0.5f; if (stm) r = 1.0f - r;
    s.target = lambdaScore * sigmoid(sc / SCORE_K) + (1.0f - lambdaScore) * r;
}

struct Grad {
    std::vector<float> w1, b1, wo, b2;
    Grad() : w1(NF * H), b1(H), wo(NB * 2 * H), b2(NB) {}
    void zero() {
        std::fill(w1.begin(), w1.end(), 0.f); std::fill(b1.begin(), b1.end(), 0.f);
        std::fill(wo.begin(), wo.end(), 0.f); std::fill(b2.begin(), b2.end(), 0.f);
    }
};

static float forwardBackward(const Net& net, const Sample& s, Grad* g) {
    alignas(32) float accA[H], accB[H], sA[H], sB[H];
    for (int i = 0; i < H; i++) { accA[i] = net.b1[i]; accB[i] = net.b1[i]; }
    for (int k = 0; k < s.n; k++) {
        const float* wa = &net.w1[(size_t)s.own[k] * H];
        const float* wb = &net.w1[(size_t)s.opp[k] * H];
        for (int i = 0; i < H; i++) { accA[i] += wa[i]; accB[i] += wb[i]; }
    }
    const float* wo = &net.wo[(size_t)s.bucket * 2 * H];
    float o = net.b2[s.bucket];
    for (int i = 0; i < H; i++) {
        float a = std::min(1.0f, std::max(0.0f, accA[i])); sA[i] = a * a;
        float b = std::min(1.0f, std::max(0.0f, accB[i])); sB[i] = b * b;
        o += sA[i] * wo[i] + sB[i] * wo[H + i];
    }
    float p = sigmoid(o);
    float diff = p - s.target;
    float loss = diff * diff;
    if (g) {
        float d = 2.0f * diff * p * (1.0f - p);
        float* gwo = &g->wo[(size_t)s.bucket * 2 * H];
        g->b2[s.bucket] += d;
        alignas(32) float dA[H], dB[H];
        for (int i = 0; i < H; i++) {
            gwo[i] += d * sA[i];
            gwo[H + i] += d * sB[i];
            float a = accA[i], b = accB[i];
            dA[i] = (a > 0.0f && a < 1.0f) ? d * wo[i] * 2.0f * a : 0.0f;
            dB[i] = (b > 0.0f && b < 1.0f) ? d * wo[H + i] * 2.0f * b : 0.0f;
            g->b1[i] += dA[i] + dB[i];
        }
        for (int k = 0; k < s.n; k++) {
            float* ga = &g->w1[(size_t)s.own[k] * H];
            float* gb = &g->w1[(size_t)s.opp[k] * H];
            for (int i = 0; i < H; i++) { ga[i] += dA[i]; gb[i] += dB[i]; }
        }
    }
    return loss;
}

struct Adam {
    std::vector<float> m, v;
    void init(size_t n) { m.assign(n, 0.f); v.assign(n, 0.f); }
    void step(std::vector<float>& w, const std::vector<float>& g, float lr, float invBatch, int t, float lo, float hi) {
        const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
        float c1 = 1.0f - std::pow(b1, (float)t), c2 = 1.0f - std::pow(b2, (float)t);
        size_t n = w.size();
        #pragma omp parallel for
        for (long long i = 0; i < (long long)n; i++) {
            float gr = g[i] * invBatch;
            m[i] = b1 * m[i] + (1 - b1) * gr;
            v[i] = b2 * v[i] + (1 - b2) * gr * gr;
            float upd = lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps);
            w[i] -= upd;
            if (w[i] < lo) w[i] = lo; else if (w[i] > hi) w[i] = hi;
        }
    }
};

static void saveQuantized(const Net& net, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    const char magic[4] = {'S', 'N', 'N', 'U'};
    int32_t hdr[3] = {KB, H, NB};
    fwrite(magic, 1, 4, f); fwrite(hdr, 4, 3, f);
    std::vector<int16_t> q;
    q.resize(NF * H);
    for (int i = 0; i < NF * H; i++) q[i] = (int16_t)std::lround(std::max(-127.0f, std::min(127.0f, net.w1[i])) * 255.0f);
    fwrite(q.data(), 2, q.size(), f);
    q.resize(H);
    for (int i = 0; i < H; i++) q[i] = (int16_t)std::lround(std::max(-127.0f, std::min(127.0f, net.b1[i])) * 255.0f);
    fwrite(q.data(), 2, q.size(), f);
    q.resize(NB * 2 * H);
    for (int i = 0; i < NB * 2 * H; i++) q[i] = (int16_t)std::lround(std::max(-1.98f, std::min(1.98f, net.wo[i])) * 64.0f);
    fwrite(q.data(), 2, q.size(), f);
    std::vector<int32_t> qb(NB);
    for (int i = 0; i < NB; i++) qb[i] = (int32_t)std::lround(net.b2[i] * 255.0f * 64.0f);
    fwrite(qb.data(), 4, qb.size(), f);
    fclose(f);
}
static void saveFloat(const Net& net, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    fwrite(net.w1.data(), 4, net.w1.size(), f); fwrite(net.b1.data(), 4, net.b1.size(), f);
    fwrite(net.wo.data(), 4, net.wo.size(), f); fwrite(net.b2.data(), 4, net.b2.size(), f);
    fclose(f);
}
static bool loadFloat(Net& net, const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    size_t r = 0;
    r += fread(net.w1.data(), 4, net.w1.size(), f); r += fread(net.b1.data(), 4, net.b1.size(), f);
    r += fread(net.wo.data(), 4, net.wo.size(), f); r += fread(net.b2.data(), 4, net.b2.size(), f);
    fclose(f);
    return r == net.w1.size() + net.b1.size() + net.wo.size() + net.b2.size();
}

static double evalLoss(const Net& net, const std::vector<PackedPos>& data, size_t from, size_t to) {
    double tot = 0;
    #pragma omp parallel for reduction(+ : tot)
    for (long long i = (long long)from; i < (long long)to; i++) {
        Sample s; decode(data[i], s);
        tot += forwardBackward(net, s, nullptr);
    }
    return tot / (double)(to - from);
}

int main(int argc, char** argv) {
    std::vector<std::string> dirs;
    std::string outPrefix = "net", initPath;
    int epochs = 4, batch = 16384, threads = 10;
    float lr0 = 1e-3f, lrEnd = 1e-5f;
    long long maxPos = 0;
    bool checkMode = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--data") { while (i + 1 < argc && argv[i + 1][0] != '-') dirs.push_back(argv[++i]); }
        else if (a == "--out") outPrefix = argv[++i];
        else if (a == "--init") initPath = argv[++i];
        else if (a == "--epochs") epochs = atoi(argv[++i]);
        else if (a == "--batch") batch = atoi(argv[++i]);
        else if (a == "--lr") lr0 = (float)atof(argv[++i]);
        else if (a == "--lrend") lrEnd = (float)atof(argv[++i]);
        else if (a == "--threads") threads = atoi(argv[++i]);
        else if (a == "--lambda") lambdaScore = (float)atof(argv[++i]);
        else if (a == "--maxpos") maxPos = atoll(argv[++i]);
        else if (a == "--check") checkMode = true;
    }
    omp_set_num_threads(threads);
    std::vector<PackedPos> data;
    for (auto& d : dirs) {
        for (auto& e : std::filesystem::directory_iterator(d)) {
            if (e.path().extension() != ".bin") continue;
            FILE* f = fopen(e.path().string().c_str(), "rb");
            if (!f) continue;
            fseek(f, 0, SEEK_END); long long sz = _ftelli64(f); fseek(f, 0, SEEK_SET);
            size_t n = (size_t)(sz / sizeof(PackedPos));
            size_t old = data.size();
            data.resize(old + n);
            size_t got = fread(&data[old], sizeof(PackedPos), n, f);
            data.resize(old + got);
            fclose(f);
        }
    }
    if (maxPos > 0 && (long long)data.size() > maxPos) data.resize(maxPos);
    if (checkMode) {
        Net cn;
        if (!loadFloat(cn, initPath)) { printf("cannot load ckpt\n"); return 1; }
        double sumAbs = 0, maxAbs = 0; 
        for (size_t i = 0; i < data.size(); i++) {
            Sample s; decode(data[i], s);
            // recompute raw output o (cp = o * 400) via forwardBackward-like pass
            float accA[H], accB[H];
            for (int k = 0; k < H; k++) { accA[k] = cn.b1[k]; accB[k] = cn.b1[k]; }
            for (int f = 0; f < s.n; f++) for (int k = 0; k < H; k++) { accA[k] += cn.w1[(size_t)s.own[f] * H + k]; accB[k] += cn.w1[(size_t)s.opp[f] * H + k]; }
            const float* wo = &cn.wo[(size_t)s.bucket * 2 * H];
            float o = cn.b2[s.bucket];
            for (int k = 0; k < H; k++) { float a = std::min(1.f, std::max(0.f, accA[k])), b = std::min(1.f, std::max(0.f, accB[k])); o += a * a * wo[k] + b * b * wo[H + k]; }
            float cp = o * SCORE_K;
            float eng = data[i].stm ? -data[i].score : data[i].score;
            double d = std::fabs(cp - eng);
            sumAbs += d; maxAbs = std::max(maxAbs, d);
            if (i < 8) printf("float %.1f engine %.1f\n", cp, eng);
        }
        printf("check: mean abs diff %.3f cp, max %.3f cp over %zu positions\n", sumAbs / data.size(), maxAbs, data.size());
        return 0;
    }
    printf("loaded %zu positions, HID=%d, threads=%d, lambda=%.2f\n", data.size(), H, threads, lambdaScore);
    fflush(stdout);
    if (data.size() < 1000) return 1;
    std::mt19937_64 rng(12345);
    std::shuffle(data.begin(), data.end(), rng);
    size_t nVal = std::min<size_t>(data.size() / 50, 500000);
    size_t nTrain = data.size() - nVal;

    Net net;
    if (initPath.empty() || !loadFloat(net, initPath)) {
        std::normal_distribution<float> nd(0.f, 1.f);
        for (auto& w : net.w1) w = nd(rng) * 0.08f;
        for (auto& w : net.b1) w = 0.f;
        for (auto& w : net.wo) w = nd(rng) * 0.05f;
        for (auto& w : net.b2) w = 0.f;
    } else printf("loaded init %s\n", initPath.c_str());

    Adam aw1, ab1, awo, ab2;
    aw1.init(net.w1.size()); ab1.init(net.b1.size()); awo.init(net.wo.size()); ab2.init(net.b2.size());
    std::vector<Grad> grads(threads);
    Grad total;
    std::vector<uint32_t> order(nTrain);
    for (size_t i = 0; i < nTrain; i++) order[i] = (uint32_t)(i + nVal);  // train portion indices (data shuffled)
    long long batchesPerEpoch = (long long)(nTrain / batch);
    long long totalBatches = batchesPerEpoch * epochs, step = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int ep = 0; ep < epochs; ep++) {
        std::shuffle(order.begin(), order.end(), rng);
        double runLoss = 0; long long runCnt = 0;
        for (long long b = 0; b < batchesPerEpoch; b++) {
            float prog = (float)step / (float)std::max(1LL, totalBatches);
            float lr = lrEnd + (lr0 - lrEnd) * 0.5f * (1.0f + std::cos(3.14159265f * prog));
            double bl = 0;
            #pragma omp parallel reduction(+ : bl)
            {
                int tid = omp_get_thread_num();
                Grad& g = grads[tid];
                g.zero();
                #pragma omp for schedule(static)
                for (int i = 0; i < batch; i++) {
                    Sample s; decode(data[order[b * batch + i]], s);
                    bl += forwardBackward(net, s, &g);
                }
            }
            // reduce grads
            for (int t = 1; t < threads; t++) {
                Grad& g = grads[t];
                #pragma omp parallel for
                for (long long i = 0; i < (long long)g.w1.size(); i++) grads[0].w1[i] += g.w1[i];
                for (size_t i = 0; i < g.b1.size(); i++) grads[0].b1[i] += g.b1[i];
                for (size_t i = 0; i < g.wo.size(); i++) grads[0].wo[i] += g.wo[i];
                for (size_t i = 0; i < g.b2.size(); i++) grads[0].b2[i] += g.b2[i];
            }
            step++;
            float inv = 1.0f / batch;
            aw1.step(net.w1, grads[0].w1, lr, inv, (int)step, -4.f, 4.f);
            ab1.step(net.b1, grads[0].b1, lr, inv, (int)step, -4.f, 4.f);
            awo.step(net.wo, grads[0].wo, lr, inv, (int)step, -1.98f, 1.98f);
            ab2.step(net.b2, grads[0].b2, lr, inv, (int)step, -8.f, 8.f);
            runLoss += bl; runCnt += batch;
            if ((b + 1) % 200 == 0) {
                double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                printf("ep %d batch %lld/%lld lr %.2e train-loss %.6f  (%.0f samples/s)\n", ep, b + 1, batchesPerEpoch, lr, runLoss / runCnt,
                       (double)step * batch / el);
                fflush(stdout);
                runLoss = 0; runCnt = 0;
            }
        }
        double vl = evalLoss(net, data, 0, nVal);
        printf("=== epoch %d done: val-loss %.6f\n", ep, vl);
        fflush(stdout);
        saveQuantized(net, outPrefix + "_e" + std::to_string(ep) + ".nnue");
        saveFloat(net, outPrefix + ".ckpt");
    }
    saveQuantized(net, outPrefix + ".nnue");
    return 0;
}
