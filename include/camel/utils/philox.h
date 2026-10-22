// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Philox4x32-10, the counter-based generator of the keyed random operators
 * (docs/design/training/README.cn.md 4.1, docs/design-principles.cn.md 13.5).
 *
 * The one implementation: the random module's kernels and the programs std::cpp writes both
 * include this header, so a key, a split and a permutation are the same bits on every scheduler,
 * in translated C++, and on every platform. Standard headers only, integer arithmetic only, no
 * state: every function is a pure function of its arguments.
 *
 * Value layout. A Key is 128 bits, stored as two int64 halves in a struct `{ hi: int, lo: int }`:
 * the value is `(hi << 64) | lo` with each half read as unsigned bits. `key(seed)` is the block
 * with key = seed (a 64-bit Philox key) and counter = 0, its words packed low-first:
 * lo = o0:o1, hi = o2:o3 (`o0` is the low 32 bits and `o1` the high 32 bits of the low half).
 * `split(k)` derives two children with domains 0x9E3779B97F4A7C15 and 0xD1B54A32D192ED03, each
 * hashed with key = k.lo and counter = (k.hi, domain). `permutation(k, n)` draws with key = k.lo
 * and counter = (k.hi, 0x8000000000000000 + draw index), taking the 64-bit word o0:o1 per draw.
 */

#pragma once

#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace camel::utils::philox {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

/// 128 bits of key or counter material. The fields are the halves of the value.
struct Key {
    u64 hi;
    u64 lo;

    friend bool operator==(const Key &, const Key &) = default;
};

struct Block {
    u32 word[4];
};

namespace detail {

constexpr u32 kM0 = 0xD2511F53u;
constexpr u32 kM1 = 0xCD9E8D57u;
constexpr u32 kW0 = 0x9E3779B9u;
constexpr u32 kW1 = 0xBB67AE85u;

/// The high 64 bits of a 64x64 product, without a 128-bit integer type (the code must build with
/// MSVC, which has none).
inline u64 mulHigh(u64 a, u64 b) {
    const u64 aLo = a & 0xFFFFFFFFu, aHi = a >> 32;
    const u64 bLo = b & 0xFFFFFFFFu, bHi = b >> 32;
    const u64 low     = aLo * bLo;
    const u64 middle1 = aHi * bLo + (low >> 32);
    const u64 middle2 = aLo * bHi + (middle1 & 0xFFFFFFFFu);
    return aHi * bHi + (middle1 >> 32) + (middle2 >> 32);
}

inline Block block(u64 key, u64 counterLo, u64 counterHi) {
    u32 c0 = static_cast<u32>(counterLo);
    u32 c1 = static_cast<u32>(counterLo >> 32);
    u32 c2 = static_cast<u32>(counterHi);
    u32 c3 = static_cast<u32>(counterHi >> 32);
    u32 k0 = static_cast<u32>(key);
    u32 k1 = static_cast<u32>(key >> 32);
    for (int round = 0; round < 10; ++round) {
        const u64 p0 = static_cast<u64>(kM0) * c0;
        const u64 p1 = static_cast<u64>(kM1) * c2;
        const u32 hi0 = static_cast<u32>(p0 >> 32), lo0 = static_cast<u32>(p0);
        const u32 hi1 = static_cast<u32>(p1 >> 32), lo1 = static_cast<u32>(p1);
        c0 = hi1 ^ c1 ^ k0;
        c1 = lo1;
        c2 = hi0 ^ c3 ^ k1;
        c3 = lo0;
        k0 += kW0;
        k1 += kW1;
    }
    return Block{{c0, c1, c2, c3}};
}

constexpr u64 kSplitA = 0x9E3779B97F4A7C15ull;
constexpr u64 kSplitB = 0xD1B54A32D192ED03ull;
constexpr u64 kPermutationDomain = 0x8000000000000000ull;

inline Key derive(const Key &key, u64 domain) {
    const Block hash = block(key.lo, key.hi, domain);
    return Key{
        (static_cast<u64>(hash.word[2]) << 32) | hash.word[3],
        (static_cast<u64>(hash.word[0]) << 32) | hash.word[1]};
}

inline u64 draw(const Key &key, u64 index) {
    const Block hash = block(key.lo, key.hi, kPermutationDomain | index);
    return (static_cast<u64>(hash.word[0]) << 32) | hash.word[1];
}

/// One unbiased draw from [0, m), by Lemire's multiply-and-reject method; `index` is advanced for
/// each rejected draw, so the stream position is part of the algorithm.
inline u64 below(const Key &key, u64 m, u64 &index) {
    for (;;) {
        const u64 r  = draw(key, index++);
        const u64 hi = mulHigh(r, m), lo = r * m;
        if (lo < m) {
            const u64 threshold = (0 - m) % m; // 2^64 mod m
            if (lo < threshold) {
                continue;
            }
        }
        return hi;
    }
}

} // namespace detail

/// The key of a seed: block(key = seed, counter = 0), packed as above.
inline Key key(u64 seed) { return detail::derive(Key{0, seed}, 0); }

/// Two independent child keys.
inline std::pair<Key, Key> split(const Key &key) {
    return {detail::derive(key, detail::kSplitA), detail::derive(key, detail::kSplitB)};
}

/// A uniform permutation of [0, n), by the descending Fisher-Yates shuffle. n == 0 and n == 1
/// are the empty and the one-element permutation; n < 0 throws std::invalid_argument.
inline std::vector<std::int64_t> permutation(const Key &key, std::int64_t n) {
    if (n < 0) {
        throw std::invalid_argument("permutation length must be non-negative");
    }
    std::vector<std::int64_t> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), std::int64_t{0});
    u64 index = 0;
    for (std::int64_t i = n - 1; i > 0; --i) {
        const u64 j = detail::below(key, static_cast<u64>(i) + 1, index);
        std::swap(order[static_cast<std::size_t>(i)], order[static_cast<std::size_t>(j)]);
    }
    return order;
}

} // namespace camel::utils::philox