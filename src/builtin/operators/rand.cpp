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
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "rand.h"
#include "core/context/context.h"

#include <algorithm>
#include <random>

// 全局随机数生成器
static std::mt19937_64 g_rng;

void __seed__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &seed_val = frame.get(nargs[0]);

    auto v = seed_val->as<IntData>(Type::Int());
    g_rng.seed(static_cast<uint64_t>(v->data()));

    frame.set(self, Data::null());
}

void __rand__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double v = dist(g_rng);
    frame.set(self, std::make_shared<DoubleData>(v));
}

void __randn__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    std::normal_distribution<double> dist(0.0, 1.0);
    double v = dist(g_rng);
    frame.set(self, std::make_shared<DoubleData>(v));
}

void __randint__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &low_val  = frame.get(nargs[0]);
    const data_ptr_t &high_val = frame.get(nargs[1]);

    int32_t low  = low_val->as<IntData>(Type::Int())->data();
    int32_t high = high_val->as<IntData>(Type::Int())->data();

    if (low > high)
        std::swap(low, high);

    std::uniform_int_distribution<int32_t> dist(low, high);
    frame.set(self, std::make_shared<IntData>(dist(g_rng)));
}

void __choice__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arr_val = frame.get(nargs[0]);

    auto arr = arr_val->as<ArrayData>(arr_val->type());
    if (arr->raw().size() == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<choice> array is empty");
        frame.set(self, Data::null());
        return;
    }

    std::uniform_int_distribution<size_t> dist(0, arr->raw().size() - 1);
    frame.set(self, arr->raw()[dist(g_rng)]);
}

void __sample__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arr_val = frame.get(nargs[0]);
    const data_ptr_t &n_val   = frame.get(nargs[1]);

    auto arr      = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elemType();
    int32_t n     = n_val->as<IntData>(Type::Int())->data();

    if (n < 0 || static_cast<size_t>(n) > arr->raw().size()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<sample> size out of range");
        frame.set(self, Data::null());
        return;
    }

    std::vector<data_ptr_t> copy = arr->raw();
    std::shuffle(copy.begin(), copy.end(), g_rng);
    copy.resize(static_cast<size_t>(n));

    frame.set(self, std::make_shared<ArrayData>(ArrayType::create(elemType), std::move(copy)));
}

void __shuffle__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arr_val = frame.get(nargs[0]);

    auto arr                     = arr_val->as<ArrayData>(arr_val->type());
    auto elemType                = tt::as_shared<ArrayType>(arr_val->type())->elemType();
    std::vector<data_ptr_t> copy = arr->raw();
    std::shuffle(copy.begin(), copy.end(), g_rng);

    frame.set(self, std::make_shared<ArrayData>(ArrayType::create(elemType), std::move(copy)));
}
