// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The module random (rand:*) in the programs std::cpp writes. It draws exactly as
 * modules/random/operators.cpp does: one std::mt19937_64 for the whole program, seeded with
 * the engine's default until random.seed(n) is called, and the distributions of
 * camel/utils/random.h, so that a program seeded the same way prints the same numbers whether
 * the interpreter runs it or it is compiled.
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
/// The engine every random operator draws from (`g_rng` of the random module).
inline camel::utils::random::Engine &randomEngine() {
    static camel::utils::random::Engine engine;
    return engine;
}
} // namespace detail

/// `random.seed(n)` (rand:seed).
inline void randomSeed(int64_t seed) {
    detail::randomEngine().seed(static_cast<camel::utils::random::Engine::result_type>(seed));
}

/// `random.rand()` (rand:rand): uniform in [0, 1).
inline double randomUnit() { return camel::utils::random::unitDouble(detail::randomEngine()); }

/// `random.randn()` (rand:randn): standard normal, from a fresh source per call as the module
/// draws it (the second value of a polar pair is not kept).
inline double randomNormal() {
    return camel::utils::random::NormalSource<double>{}(detail::randomEngine());
}

/// `random.randint(low, high)` (rand:randint): uniform in [low, high], the bounds swapped when
/// given the other way round.
inline int64_t randomBetween(int64_t low, int64_t high) {
    if (low > high) {
        std::swap(low, high);
    }
    return camel::utils::random::between(detail::randomEngine(), low, high);
}

/// `random.choice(xs)` (rand:choice): one element; of the empty array InvalidArgument at `site`
/// (values: "random.choice", what is wrong).
template <typename T> T randomChoice(const Array<T> &xs, const Site &site) {
    if (xs.empty()) [[unlikely]] {
        fault(site, {"random.choice", "the array is empty, so there is nothing to choose"});
    }
    return xs[static_cast<std::size_t>(camel::utils::random::below(detail::randomEngine(), xs.size()))];
}

/// `random.sample(xs, n)` (rand:sample): n elements by reservoir sampling, in the module's
/// order of draws; a size outside [0, length] is InvalidArgument at `site`.
template <typename T> Array<T> randomSample(const Array<T> &xs, int64_t n, const Site &site) {
    if (n < 0 || static_cast<std::size_t>(n) > xs.size()) [[unlikely]] {
        fault(
            site,
            {"random.sample",
             "the sample size is " + std::to_string(n) + " and the array has " +
                 std::to_string(xs.size()) +
                 " elements; the size must be between 0 and the number of elements"});
    }
    const auto count = static_cast<std::size_t>(n);
    if (count == 0) {
        return Array<T>();
    }
    std::vector<T> picked(xs.begin(), xs.begin() + count);
    auto &engine = detail::randomEngine();
    for (std::size_t i = count; i < xs.size(); ++i) {
        const std::size_t j = engine() % (i + 1);
        if (j < count) {
            picked[j] = xs[i];
        }
    }
    Array<T> out;
    out.reserve(count);
    for (auto &item : picked) {
        out.push(std::move(item));
    }
    return out;
}

/// `random.shuffle(xs)` (rand:shuffle): a new array, xs in the order of the module's
/// Fisher-Yates shuffle (an index drawn as `engine() % (i + 1)`).
template <typename T> Array<T> randomShuffle(const Array<T> &xs) {
    std::vector<T> items(xs.begin(), xs.end());
    auto &engine = detail::randomEngine();
    for (std::size_t i = items.size() > 1 ? items.size() - 1 : 0; i > 0; --i) {
        const std::size_t j = engine() % (i + 1);
        std::swap(items[i], items[j]);
    }
    Array<T> out;
    out.reserve(items.size());
    for (auto &item : items) {
        out.push(std::move(item));
    }
    return out;
}

/// `random.key(seed)` (rand:key): the program's Key struct, whose `hi` and `lo` fields are the
/// halves of the Philox key (camel/utils/philox.h, the code the random module runs).
template <typename Key> Key randomKey(int64_t seed) {
    const auto key = camel::utils::philox::key(static_cast<uint64_t>(seed));
    return Key{static_cast<int64_t>(key.hi), static_cast<int64_t>(key.lo)};
}

/// `random.split(key)` (rand:split): the two child keys.
template <typename Tuple, typename Key> Tuple randomSplitKey(const Key &key) {
    const auto [left, right] = camel::utils::philox::split(
        camel::utils::philox::Key{static_cast<uint64_t>(key.hi), static_cast<uint64_t>(key.lo)});
    return Tuple{
        Key{static_cast<int64_t>(left.hi), static_cast<int64_t>(left.lo)},
        Key{static_cast<int64_t>(right.hi), static_cast<int64_t>(right.lo)}};
}

/// `random.permutation(key, n)` (rand:permutation): a length below zero is InvalidArgument at
/// `site` (values: "random.permutation", what is wrong).
template <typename ArrayT, typename Key>
ArrayT randomPermutationKey(const Key &key, int64_t n, const Site &site) {
    if (n < 0) [[unlikely]] {
        fault(
            site,
            {"random.permutation",
             "the permutation length is " + std::to_string(n) + "; it must be non-negative"});
    }
    const std::vector<int64_t> order = camel::utils::philox::permutation(
        camel::utils::philox::Key{static_cast<uint64_t>(key.hi), static_cast<uint64_t>(key.lo)}, n);
    ArrayT out;
    out.reserve(order.size());
    for (const int64_t value : order) {
        out.push(value);
    }
    return out;
}

} // namespace camel::standalone
