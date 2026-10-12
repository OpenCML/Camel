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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <vector>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::core::context;

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
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            std::string(fname),
            typeCodeToString(code),
            "a number");
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
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            std::string(fname),
            typeCodeToString(code),
            "a number");
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

// ---- variance, median, quantile ----

// The elements as doubles; `hasNaN` tells whether one of them is not a number.
static std::vector<double>
numbersOf(Array *arr, TypeCode code, std::string_view fname, bool &hasNaN) {
    const size_t n = arr->size();
    std::vector<double> values(n);
    hasNaN = false;
    for (size_t i = 0; i < n; ++i) {
        switch (code) {
        case TypeCode::Int32:
            values[i] = static_cast<double>(arr->get<Int32>(i));
            break;
        case TypeCode::Int64:
            values[i] = static_cast<double>(arr->get<Int64>(i));
            break;
        case TypeCode::Float32:
            values[i] = static_cast<double>(arr->get<Float32>(i));
            break;
        case TypeCode::Float64:
            values[i] = arr->get<Float64>(i);
            break;
        default:
            throwRuntimeFault(
                RuntimeDiag::OperandTypeUnsupported,
                std::string(fname),
                typeCodeToString(code),
                "a number");
        }
        hasNaN = hasNaN || std::isnan(values[i]);
    }
    return values;
}

// The q-quantile of sorted values by linear interpolation between the two nearest ranks
// (position q * (n - 1); the common "type 7" definition). `sorted` is not empty.
static double quantileOfSorted(const std::vector<double> &sorted, double q) {
    const double position = q * static_cast<double>(sorted.size() - 1);
    const size_t below    = static_cast<size_t>(std::floor(position));
    const size_t above    = std::min(below + 1, sorted.size() - 1);
    const double fraction = position - static_cast<double>(below);
    // Not a + f * (b - a): b - a overflows for values of opposite sign near the limits.
    return sorted[below] * (1.0 - fraction) + sorted[above] * fraction;
}

// The sample variance (divisor n - 1), the square of stdev; 0 for fewer than two elements, as
// stdev is.
slot_t __statistics_variance__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *arr               = norm.get<Array *>(0);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    bool hasNaN              = false;
    const auto values        = numbersOf(arr, arrType->elemTypeCode(), "<variance>", hasNaN);
    const size_t n           = values.size();
    if (n <= 1)
        return toSlot(0.0);
    double sum = 0.0;
    for (double v : values)
        sum += v;
    const double mean = sum / static_cast<double>(n);
    double squares    = 0.0;
    for (double v : values)
        squares += (v - mean) * (v - mean);
    return toSlot(squares / static_cast<double>(n - 1));
}

// An empty array has no quantile: the result is null. With a NaN among the elements the
// result is NaN (NaN has no place in an order).
static slot_t quantileOf(ArgsView &norm, double q, std::string_view fname) {
    static const TupleType *resultType = TupleType::nullable(Type::Float64());
    Array *arr                         = norm.get<Array *>(0);
    const ArrayType *arrType           = tt::as_ptr<ArrayType>(norm.type(0));
    bool hasNaN                        = false;
    auto values                        = numbersOf(arr, arrType->elemTypeCode(), fname, hasNaN);
    if (values.empty())
        return NullSlot;
    if (hasNaN)
        return makeSome(toSlot(std::numeric_limits<double>::quiet_NaN()), resultType);
    std::sort(values.begin(), values.end());
    return makeSome(toSlot(quantileOfSorted(values, q)), resultType);
}

slot_t __statistics_median__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return quantileOf(norm, 0.5, "<median>");
}

slot_t __statistics_quantile__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const double q = with.get<Float64>(0);
    if (!(q >= 0.0 && q <= 1.0)) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "statistics.quantile",
            std::format("the quantile {} is not between 0 and 1", q));
    }
    return quantileOf(norm, q, "<quantile>");
}

std::unordered_map<std::string, operator_t> getStatisticsOpsMap() {
    return {
        {"mean", __statistics_mean__},
        {"stdev", __statistics_stdev__},
        {"variance", __statistics_variance__},
        {"median", __statistics_median__},
        {"quantile", __statistics_quantile__},
    };
}
