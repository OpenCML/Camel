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
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include <cmath>

template <typename T> static double meanSlots(Array *arr) {
    size_t n = arr->size();
    if (n == 0)
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
        sum += static_cast<double>(arr->get<T>(i));
    return sum / static_cast<double>(n);
}

template <typename T> static double stdevSlots(Array *arr) {
    size_t n = arr->size();
    if (n <= 1)
        return 0.0;
    double m = meanSlots<T>(arr), sum_sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double d = static_cast<double>(arr->get<T>(i)) - m;
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq / static_cast<double>(n - 1));
}

static double meanWithTypeCode(Array *arr, TypeCode code, Context &ctx, std::string_view fname) {
    switch (code) {
    case TypeCode::Int32:
        return meanSlots<Int32>(arr);
    case TypeCode::Int64:
        return meanSlots<Int64>(arr);
    case TypeCode::Float32:
        return meanSlots<Float32>(arr);
    case TypeCode::Float64:
        return meanSlots<Float64>(arr);
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string(fname) + " not supported for type " + typeCodeToString(code));
        return 0.0;
    }
}

static double stdevWithTypeCode(Array *arr, TypeCode code, Context &ctx, std::string_view fname) {
    switch (code) {
    case TypeCode::Int32:
        return stdevSlots<Int32>(arr);
    case TypeCode::Int64:
        return stdevSlots<Int64>(arr);
    case TypeCode::Float32:
        return stdevSlots<Float32>(arr);
    case TypeCode::Float64:
        return stdevSlots<Float64>(arr);
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string(fname) + " not supported for type " + typeCodeToString(code));
        return 0.0;
    }
}

slot_t __statistics_mean__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr               = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    return toSlot(meanWithTypeCode(arr, arrType->elemTypeCode(), ctx, "<mean>"));
}

slot_t __statistics_stdev__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr               = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    return toSlot(stdevWithTypeCode(arr, arrType->elemTypeCode(), ctx, "<stdev>"));
}

std::unordered_map<std::string, operator_t> getStatisticsOpsMap() {
    return {{"mean", __statistics_mean__}, {"stdev", __statistics_stdev__}};
}
