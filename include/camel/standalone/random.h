// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The module random (rand:*) in the programs std::cpp writes. The one keyed primitive is
 * Philox4x32-10 (camel/utils/philox.h, the code the interpreter runs), and the global stream is
 * the key `seed` set and a count of the keys taken from it, as modules/random/operators.cpp
 * keeps it, so a program seeded the same way prints the same numbers compiled or interpreted.
 */

#pragma once

#include "camel/standalone/array.h"
#include "camel/standalone/core.h"
#include "camel/utils/philox.h"
#include "camel/utils/random.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace camel::standalone {

namespace detail {
/// The global stream: the key and the number of keys taken from it.
struct RandomStream {
    camel::utils::philox::Key key{0, 0};
    uint64_t taken = 0;
};
inline RandomStream &randomStream() {
    static RandomStream stream;
    return stream;
}

/// The block of a key and a 64-bit counter (the Camel `philox` of stdlib/random.cml).
inline camel::utils::philox::Block randomBlock(const camel::utils::philox::Key &key, uint64_t counter) {
    return camel::utils::philox::detail::block(key.lo, key.hi, counter);
}
} // namespace detail

/// `seed(n)` (rand:seed): the stream starts at the key of the seed.
inline void randomSeed(int64_t seed) {
    auto &stream = detail::randomStream();
    stream.key   = camel::utils::philox::Key{0, static_cast<uint64_t>(seed)};
    stream.taken = 0;
}

/// `next_key()` (rand:next_key): the stream's key folded with the number of keys taken so far.
template <typename Key> Key randomNextKey() {
    auto &stream          = detail::randomStream();
    const uint64_t index  = stream.taken++;
    const auto block      = detail::randomBlock(stream.key, static_cast<uint64_t>(-3) - index);
    return Key{
        static_cast<int64_t>((static_cast<uint64_t>(block.word[0]) << 32) | block.word[1]),
        static_cast<int64_t>((static_cast<uint64_t>(block.word[2]) << 32) | block.word[3])};
}

/// The f64 uniform of the words of counter 0 of a key (modules/random/operators.cpp,
/// `uniformBits<double>`): 53 bits of the block, the bits the scalar `uniform` of the Camel module
/// reads, and no other draw.
inline double randomUniform53(const camel::utils::philox::Key &key) {
    const auto block    = detail::randomBlock(key, 0);
    const uint64_t high = block.word[0] >> 5, low = block.word[1] >> 6;
    return static_cast<double>(high * 67108864ull + low) * 1.1102230246251565e-16;
}

/// The key the shuffle's `index`-th draw uses: the key folded at the counter `-3 - index` (the Camel
/// `fold`), which is where the derived keys live -- one word set apart from the key's own draws.
inline camel::utils::philox::Key randomFoldKey(
    const camel::utils::philox::Key &key, uint64_t index) {
    const auto block = detail::randomBlock(key, static_cast<uint64_t>(-3) - index);
    return camel::utils::philox::Key{
        (static_cast<uint64_t>(block.word[0]) << 32) | block.word[1],
        (static_cast<uint64_t>(block.word[2]) << 32) | block.word[3]};
}

/// `xs->shuffle<k>` (rand:shuffle_i and its three siblings): `xs[permutation(k, len(xs))]`, with
/// the positions ordered by one uniform draw each -- the draw the position's own key folds to --
/// and ties in position order. That is the interpreter's kernel (modules/random/operators.cpp,
/// `shuffleOf`) to the bit: the same draws with no bias correction and no buffer, the same sort
/// over (draw, position), the same element per position. A shuffle that is merely uniform is a
/// different permutation.
template <typename T, typename Key> Array<T> randomShuffle(const Array<T> &xs, const Key &key) {
    const camel::utils::philox::Key base{
        static_cast<uint64_t>(key.hi), static_cast<uint64_t>(key.lo)};
    const std::size_t n = xs.size();
    struct Draw {
        double value;
        std::size_t position;
    };
    std::vector<Draw> draws(n);
    for (std::size_t i = 0; i < n; ++i) {
        draws[i] = {randomUniform53(randomFoldKey(base, i)), i};
    }
    std::sort(draws.begin(), draws.end(), [](const Draw &a, const Draw &b) {
        return a.value < b.value || (a.value == b.value && a.position < b.position);
    });
    Array<T> out;
    out.reserve(n);
    for (const Draw &draw : draws) {
        out.push(xs[draw.position]);
    }
    out.setExtents(std::vector<int64_t>{static_cast<int64_t>(n)});
    return out;
}

/// `philox(key, counter)` (rand:philox): the four 32-bit words of the block.
template <typename ArrayT, typename Key> ArrayT randomPhilox(const Key &key, int64_t counter) {
    const auto block = detail::randomBlock(
        camel::utils::philox::Key{static_cast<uint64_t>(key.hi), static_cast<uint64_t>(key.lo)},
        static_cast<uint64_t>(counter));
    ArrayT out;
    out.reserve(4);
    for (const uint32_t word : block.word) {
        out.push(static_cast<int64_t>(word));
    }
    return out;
}

} // namespace camel::standalone
