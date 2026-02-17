/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Oct. 01, 2025
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "rand.h"
#include "core/context/context.h"
#include "core/operator.h"
#include "core/type/composite/array.h"
#include "utils/type.h"

#include <algorithm>
#include <random>

// 全局随机数生成器
static std::mt19937_64 g_rng;

slot_t __seed__(ArgsView &with, ArgsView &norm, Context &ctx) {
    auto seed_val = norm.get<Int64>(0);
    g_rng.seed(seed_val);
    return NullSlot;
}

slot_t __rand__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return toSlot(dist(g_rng));
}

slot_t __randn__(ArgsView &with, ArgsView &norm, Context &ctx) {
    std::normal_distribution<double> dist(0.0, 1.0);
    return toSlot(dist(g_rng));
}

slot_t __randint__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int low  = norm.get<Int>(0);
    Int high = norm.get<Int>(1);

    if (low > high)
        std::swap(low, high);

    std::uniform_int_distribution<int64_t> dist(low, high);
    return toSlot(dist(g_rng));
}

slot_t __choice__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);

    if (arr->size() == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<choice> array is empty");
        return NullSlot;
    }

    std::uniform_int_distribution<size_t> dist(0, arr->size() - 1);
    return arr->get<Int64>(dist(g_rng));
}

// Fisher–Yates 洗牌（原地随机置换）
static inline void shuffleArray(Array *arr) {
    size_t sz = arr->size();
    if (sz <= 1)
        return;
    slot_t *data = arr->data();
    for (size_t i = sz - 1; i > 0; --i) {
        size_t j   = g_rng() % (i + 1);
        slot_t tmp = data[i];
        data[i]    = data[j];
        data[j]    = tmp;
    }
}

static inline Array *sampleArray(const Array *arr, int32_t n) {
    if (n <= 0)
        return Array::create(mm::autoSpace(), 0);

    size_t sz = arr->size();
    if (sz == 0)
        return Array::create(mm::autoSpace(), 0);

    if ((size_t)n > sz)
        n = static_cast<int32_t>(sz);

    Array *res        = Array::create(mm::autoSpace(), static_cast<size_t>(n));
    slot_t *dst       = reinterpret_cast<slot_t *>(res->data());
    const slot_t *src = reinterpret_cast<const slot_t *>(arr->data());

    // ---- Fisher–Yates 采样算法 (Knuth shuffle 样本法) ----
    // 只需要前 n 个随机样本，无需完整洗牌
    // 时间复杂度 O(sz)，但最多做 sz 次交换，适合较大样本采样
    for (size_t i = 0; i < (size_t)n; ++i)
        dst[i] = src[i];

    for (size_t i = n; i < sz; ++i) {
        size_t j = g_rng() % (i + 1);
        if (j < (size_t)n)
            dst[j] = src[i];
    }

    return res;
}

slot_t __sample__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);
    Int n      = norm.get<Int>(1);

    // 样本数量检查
    if (n < 0 || (size_t)n > arr->size()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<sample> size out of range");
        return NullSlot;
    }

    Array *res = sampleArray(arr, n);
    return toSlot(res);
}

slot_t __shuffle__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr = norm.get<Array *>(0);

    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    Array *res               = static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));

    // 原地洗牌（Fisher–Yates）
    shuffleArray(res);

    return toSlot(res);
}
