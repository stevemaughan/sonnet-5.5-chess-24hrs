#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cmath>
#include <immintrin.h>

typedef uint64_t U64;
typedef uint16_t Move;

enum { WHITE = 0, BLACK = 1 };
enum { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PT };
enum { NO_PIECE = 12, NO_SQ = 64 };

inline int mkPiece(int c, int pt) { return c * 6 + pt; }
inline int pieceColor(int p) { return p >= 6; }
inline int pieceType(int p) { return p >= 6 ? p - 6 : p; }
inline int fileOf(int s) { return s & 7; }
inline int rankOf(int s) { return s >> 3; }

// move flags
enum {
    F_QUIET = 0, F_DOUBLE = 1, F_KCASTLE = 2, F_QCASTLE = 3, F_CAPTURE = 4, F_EP = 5,
    F_PROMO_N = 8, F_PROMO_B = 9, F_PROMO_R = 10, F_PROMO_Q = 11,
    F_PROMOCAP_N = 12, F_PROMOCAP_B = 13, F_PROMOCAP_R = 14, F_PROMOCAP_Q = 15
};
inline Move mkMove(int from, int to, int fl) { return (Move)(from | (to << 6) | (fl << 12)); }
inline int mFrom(Move m) { return m & 63; }
inline int mTo(Move m) { return (m >> 6) & 63; }
inline int mFlags(Move m) { return m >> 12; }
inline bool mIsCapture(Move m) { return (m >> 12) & 4; }
inline bool mIsPromo(Move m) { return (m >> 12) & 8; }

constexpr U64 FILE_A = 0x0101010101010101ULL;
constexpr U64 FILE_H = FILE_A << 7;
constexpr U64 RANK_1 = 0xFFULL;
constexpr U64 RANK_2 = RANK_1 << 8;
constexpr U64 RANK_3 = RANK_1 << 16;
constexpr U64 RANK_4 = RANK_1 << 24;
constexpr U64 RANK_5 = RANK_1 << 32;
constexpr U64 RANK_6 = RANK_1 << 40;
constexpr U64 RANK_7 = RANK_1 << 48;
constexpr U64 RANK_8 = RANK_1 << 56;

inline int popcount(U64 b) { return __builtin_popcountll(b); }
inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int msb(U64 b) { return 63 - __builtin_clzll(b); }
inline int pop_lsb(U64& b) { int s = __builtin_ctzll(b); b &= b - 1; return s; }
inline U64 sqbb(int s) { return 1ULL << s; }
inline bool moreThanOne(U64 b) { return b & (b - 1); }

typedef std::chrono::steady_clock Clock;
inline int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
inline double nowSec() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
