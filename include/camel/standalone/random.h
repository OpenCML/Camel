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
