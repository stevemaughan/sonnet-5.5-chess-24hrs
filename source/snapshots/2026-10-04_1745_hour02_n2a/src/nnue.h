#pragma once
#include "bitboard.h"

// (768 -> NN_H) x 2 perspectives -> 1, SCReLU, NN_B output buckets.
// Quantisation: QA = 255 (accumulator), QB = 64 (output weights), SCALE = 400.
#ifndef NN_H
#define NN_H 256
#endif
#ifndef NN_KB
#define NN_KB 1
#endif
constexpr int NN_KB_N = NN_KB;
constexpr int NN_NF = 768 * NN_KB, NN_B = 8;
constexpr int NN_QA = 255, NN_QB = 64, NN_SCALE = 400;

struct NNUENet {
    alignas(32) int16_t w1[NN_NF * NN_H];
    alignas(32) int16_t b1[NN_H];
    alignas(32) int16_t wo[NN_B][2][NN_H];
    int32_t b2[NN_B];
};
static NNUENet* gNet = nullptr;
static bool useNNUE = false;

static bool nnueLoadBuffer(const unsigned char* p, size_t n) {
    const size_t hdr = 16;
    size_t need = hdr + sizeof(int16_t) * (NN_NF * NN_H + NN_H + NN_B * 2 * NN_H) + sizeof(int32_t) * NN_B;
    if (n < need) return false;
    if (memcmp(p, "SNNU", 4) != 0) return false;
    int32_t h[3];
    memcpy(h, p + 4, 12);
    if (h[0] != NN_KB || h[1] != NN_H || h[2] != NN_B) return false;
    if (!gNet) gNet = (NNUENet*)_mm_malloc(sizeof(NNUENet), 64);
    const unsigned char* q = p + hdr;
    memcpy(gNet->w1, q, sizeof(int16_t) * NN_NF * NN_H); q += sizeof(int16_t) * NN_NF * NN_H;
    memcpy(gNet->b1, q, sizeof(int16_t) * NN_H); q += sizeof(int16_t) * NN_H;
    memcpy(gNet->wo, q, sizeof(int16_t) * NN_B * 2 * NN_H); q += sizeof(int16_t) * NN_B * 2 * NN_H;
    memcpy(gNet->b2, q, sizeof(int32_t) * NN_B);
    useNNUE = true;
    return true;
}
static bool nnueLoadFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf(sz);
    size_t got = fread(buf.data(), 1, sz, f);
    fclose(f);
    return got == (size_t)sz && nnueLoadBuffer(buf.data(), buf.size());
}

#ifdef NET_FILE
__asm__(".section .rodata\n.global gNetData\n.balign 32\ngNetData:\n.incbin \"" NET_FILE "\"\n.global gNetEnd\ngNetEnd:\n.byte 0\n.text\n");
extern "C" const unsigned char gNetData[];
extern "C" const unsigned char gNetEnd[];
static void nnueLoadEmbedded() { nnueLoadBuffer(gNetData, (size_t)(gNetEnd - gNetData)); }
#else
static void nnueLoadEmbedded() {}
#endif

// King-bucket info for a perspective-relative king square ks (already flipped for black).
// Returns bucket (0..NN_KB-1) in bits 1.., mirror flag in bit 0. With NN_KB == 1 there is no mirroring.
static inline int nnKingInfo(int ks) {
    if (NN_KB == 1) return 0;
    int mirror = (ks & 7) >= 4;
    int f = mirror ? (ks & 7) ^ 7 : (ks & 7);
    int r = ks >> 3;
    int b;
    if (NN_KB == 4) b = r <= 1 ? (f <= 1 ? 0 : 1) : (r <= 3 ? 2 : 3);
    else b = r == 0 ? (f <= 1 ? 0 : 1) : (r == 1 ? 2 : (r <= 3 ? 3 : 4));  // NN_KB == 5
    return (b << 1) | mirror;
}
// feature index of piece pc on square sq from perspective p, with perspective king on ksq (absolute square)
static inline int nnFeature(int p, int pc, int sq, int ksq) {
    int c = pc >= 6, t = c ? pc - 6 : pc;
    int s = (p == 0 ? sq : (sq ^ 56));
    int base = ((c == p ? 0 : 1) * 6 + t) * 64;
    if (NN_KB == 1) return base + s;
    int ks = (p == 0 ? ksq : (ksq ^ 56));
    int info = nnKingInfo(ks);
    if (info & 1) s ^= 7;
    return (info >> 1) * 768 + base + s;
}
