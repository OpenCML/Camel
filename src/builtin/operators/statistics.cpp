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
 * Created: Dec. 11, 2025
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "statistics.h"
#include "core/context/context.h"

#include <cmath>

// ---------- 均值计算模板 ----------
template <typename T> static double __mean_slots__(Array *arr) {
    size_t n = arr->size();
    if (n == 0)
        return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(arr->get<T>(i));
    }
    return sum / static_cast<double>(n);
}

// ---------- 样本标准差计算模板 ----------
// stdev = sqrt( sum((x - mean)^2) / (n - 1) )
template <typename T> static double __stdev_slots__(Array *arr) {
    size_t n = arr->size();
    if (n <= 1)
        return 0.0;

    double mean   = __mean_slots__<T>(arr);
    double sum_sq = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double diff = static_cast<double>(arr->get<T>(i)) - mean;
        sum_sq += diff * diff;
    }

    return std::sqrt(sum_sq / static_cast<double>(n - 1));
}

static double
__mean_with_type_code__(Array *arr, TypeCode code, Context &ctx, std::string_view fname) {
    switch (code) {
    case TypeCode::Int:
        return __mean_slots__<Int>(arr);
    case TypeCode::Long:
        return __mean_slots__<Long>(arr);
    case TypeCode::Float:
        return __mean_slots__<Float>(arr);
    case TypeCode::Double:
        return __mean_slots__<Double>(arr);
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string(fname) + " not supported for type " + typeCodeToString(code));
        return 0.0;
    }
}

static double
__stdev_with_type_code__(Array *arr, TypeCode code, Context &ctx, std::string_view fname) {
    switch (code) {
    case TypeCode::Int:
        return __stdev_slots__<Int>(arr);
    case TypeCode::Long:
        return __stdev_slots__<Long>(arr);
    case TypeCode::Float:
        return __stdev_slots__<Float>(arr);
    case TypeCode::Double:
        return __stdev_slots__<Double>(arr);
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string(fname) + " not supported for type " + typeCodeToString(code));
        return 0.0;
    }
}

void __mean__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr    = frame.get<Array *>(nargs[0]);
    double result = __mean_with_type_code__(arr, arr->elemType(), ctx, "<mean>");
    frame.set(self, result);
}

void __stdev__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr    = frame.get<Array *>(nargs[0]);
    double result = __stdev_with_type_code__(arr, arr->elemType(), ctx, "<stdev>");
    frame.set(self, result);
}
