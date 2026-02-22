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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <random>

static std::mt19937_64 g_rng;

std::unordered_map<std::string, operator_t> getRandomOpsMap() {
    return {
        {"seed", __rand_seed__},
        {"rand", __rand_rand__},
        {"randn", __rand_randn__},
        {"randint", __rand_randint__},
        {"choice", __rand_choice__},
        {"sample", __rand_sample__},
        {"shuffle", __rand_shuffle__},
    };
}

slot_t __rand_seed__(ArgsView &with, ArgsView &norm, Context &ctx) {
    g_rng.seed(norm.get<Int64>(0));
    return NullSlot;
}

slot_t __rand_rand__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(std::uniform_real_distribution<double>(0.0, 1.0)(g_rng));
}

slot_t __rand_randn__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(std::normal_distribution<double>(0.0, 1.0)(g_rng));
}

slot_t __rand_randint__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 low = norm.get<Int64>(0), high = norm.get<Int64>(1);
    if (low > high)
        std::swap(low, high);
    return toSlot(std::uniform_int_distribution<int64_t>(low, high)(g_rng));
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
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<choice> array is empty");
        return NullSlot;
    }
    size_t idx = std::uniform_int_distribution<size_t>(0, arr->size() - 1)(g_rng);
    return arr->get<slot_t>(idx);
}

slot_t __rand_sample__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    Int64 n    = norm.get<Int64>(1);
    if (n < 0 || (size_t)n > arr->size()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<sample> size out of range");
        return NullSlot;
    }
    return toSlot(sampleArray(arr, static_cast<int32_t>(n)));
}

slot_t __rand_shuffle__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr               = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    Array *res               = static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));
    shuffleArray(res);
    return toSlot(res);
}
