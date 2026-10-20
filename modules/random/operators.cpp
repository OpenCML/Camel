/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/philox.h"
#include "camel/utils/random.h"
#include "camel/utils/type.h"
#include "key.h"

#include <algorithm>
#include <format>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;
namespace rnd = camel::utils::random;
namespace philox = camel::utils::philox;

// The distributions are camel/utils/random.h's, not <random>'s: the same seed gives the same
// numbers on every platform.
static rnd::Engine g_rng;

std::unordered_map<std::string, operator_t> getRandomOpsMap() {
    return {
        {"seed", __rand_seed__},
        {"rand", __rand_rand__},
        {"randn", __rand_randn__},
        {"randint", __rand_randint__},
        {"choice", __rand_choice__},
        {"sample", __rand_sample__},
        {"shuffle", __rand_shuffle__},
        {"key", __rand_key__},
        {"split", __rand_split__},
        {"permutation", __rand_permutation__},
    };
}

namespace {

/// A Key struct value: `{ hi, lo }` in the canonical field order (key.h).
Struct *makeKey(const philox::Key &key) {
    Struct *value = Struct::create(2, mm::autoSpace());
    value->set<Int64>(0, static_cast<Int64>(key.hi));
    value->set<Int64>(1, static_cast<Int64>(key.lo));
    return value;
}

philox::Key readKey(const Struct *value) {
    return philox::Key{
        static_cast<philox::u64>(value->get<Int64>(0)),
        static_cast<philox::u64>(value->get<Int64>(1))};
}

} // namespace

slot_t __rand_seed__(ArgsView &with, ArgsView &norm, Context &ctx) {
    g_rng.seed(norm.get<Int64>(0));
    return NullSlot;
}

slot_t __rand_rand__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(rnd::unitDouble(g_rng));
}

slot_t __rand_randn__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // A fresh source per call: the second value of a pair is not kept, so the numbers after
    // seed() depend on nothing but the calls made since.
    return toSlot(rnd::NormalSource<double>{}(g_rng));
}

slot_t __rand_randint__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 low = norm.get<Int64>(0), high = norm.get<Int64>(1);
    if (low > high)
        std::swap(low, high);
    return toSlot(rnd::between(g_rng, low, high));
}

static void shuffleArray(Array *arr) {
    size_t sz = arr->size();
    if (sz <= 1)
        return;
    slot_t *data = arr->data();
    for (size_t i = sz - 1; i > 0; --i) {
        size_t j = g_rng() % (i + 1);
        std::swap(data[i], data[j]);
    }
}

static Array *sampleArray(const Array *arr, int32_t n) {
    if (n <= 0 || arr->size() == 0)
        return Array::create(mm::autoSpace(), 0);
    size_t sz = arr->size();
    if ((size_t)n > sz)
        n = static_cast<int32_t>(sz);
    Array *res        = Array::create(mm::autoSpace(), static_cast<size_t>(n));
    slot_t *dst       = reinterpret_cast<slot_t *>(res->data());
    const slot_t *src = reinterpret_cast<const slot_t *>(arr->data());
    for (size_t i = 0; i < (size_t)n; ++i)
        dst[i] = src[i];
    for (size_t i = n; i < sz; ++i) {
        size_t j = g_rng() % (i + 1);
        if (j < (size_t)n)
            dst[j] = src[i];
    }
    return res;
}

slot_t __rand_choice__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    if (arr->size() == 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "random.choice",
            "the array is empty, so there is nothing to choose");
    }
    const size_t idx = static_cast<size_t>(rnd::below(g_rng, arr->size()));
    return arr->get<slot_t>(idx);
}

slot_t __rand_sample__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    Int64 n    = norm.get<Int64>(1);
    if (n < 0 || (size_t)n > arr->size()) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "random.sample",
            std::format(
                "the sample size is {} and the array has {} elements; the size must be between 0 "
                "and the number of elements",
                n,
                arr->size()));
    }
    return toSlot(sampleArray(arr, static_cast<int32_t>(n)));
}

slot_t __rand_shuffle__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    const camel::core::type::ArrayType *arrType =
        tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));
    Array *res = static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));
    shuffleArray(res);
    return toSlot(res);
}

// ---------------------------------------------------------------- keyed random

slot_t __rand_key__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(makeKey(philox::key(static_cast<philox::u64>(norm.get<Int64>(0)))));
}

slot_t __rand_split__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const auto [left, right] = philox::split(readKey(norm.get<Struct *>(0)));
    Tuple *pair              = Tuple::create(2, mm::autoSpace());
    pair->set(0, makeKey(left));
    pair->set(1, makeKey(right));
    return toSlot(pair);
}

slot_t __rand_permutation__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const philox::Key key = readKey(norm.get<Struct *>(0));
    const Int64 n         = norm.get<Int64>(1);
    if (n < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "random.permutation",
            std::format("the permutation length is {}; it must be non-negative", n));
    }
    const std::vector<int64_t> order = philox::permutation(key, static_cast<int64_t>(n));
    Array *result                    = Array::create(mm::autoSpace(), order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        result->set<Int64>(i, static_cast<Int64>(order[i]));
    }
    return toSlot(result);
}
