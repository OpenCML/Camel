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

#include <algorithm>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
#include <vector>

enum class SortAlgo { Insertion, Quick, Merge };

template <typename T, typename Compare>
static void insertionSortSlots(slot_t *vals, size_t n, Compare comp) {
    for (size_t i = 1; i < n; ++i) {
        T key    = fromSlot<T>(vals[i]);
        size_t j = i;
        while (j > 0 && comp(key, fromSlot<T>(vals[j - 1]))) {
            vals[j] = vals[j - 1];
            --j;
        }
        vals[j] = toSlot(key);
    }
}

/// Quicksort over [left, right]: median-of-three pivot, Hoare partition, insertion sort for short
/// ranges. Recursion only into the smaller part, so the depth stays logarithmic on sorted or
/// constant input.
template <typename T, typename Compare>
static void quickSortSlots(slot_t *vals, ptrdiff_t left, ptrdiff_t right, Compare comp) {
    while (right - left >= 16) {
        const ptrdiff_t mid = left + (right - left) / 2;
        if (comp(fromSlot<T>(vals[mid]), fromSlot<T>(vals[left])))
            std::swap(vals[mid], vals[left]);
        if (comp(fromSlot<T>(vals[right]), fromSlot<T>(vals[left])))
            std::swap(vals[right], vals[left]);
        if (comp(fromSlot<T>(vals[right]), fromSlot<T>(vals[mid])))
            std::swap(vals[right], vals[mid]);
        const T pivot = fromSlot<T>(vals[mid]);
        ptrdiff_t i = left, j = right;
        while (i <= j) {
            while (comp(fromSlot<T>(vals[i]), pivot))
                ++i;
            while (comp(pivot, fromSlot<T>(vals[j])))
                --j;
            if (i <= j) {
                std::swap(vals[i], vals[j]);
                ++i;
                --j;
            }
        }
        if (j - left < right - i) {
            quickSortSlots<T>(vals, left, j, comp);
            left = i;
        } else {
            quickSortSlots<T>(vals, i, right, comp);
            right = j;
        }
    }
    if (right > left) {
        insertionSortSlots<T>(vals + left, static_cast<size_t>(right - left + 1), comp);
    }
}

template <typename T, typename Compare>
static void mergeSortSlots(slot_t *vals, size_t n, Compare comp, slot_t *tmp) {
    if (n <= 1)
        return;
    size_t mid = n / 2;
    mergeSortSlots<T>(vals, mid, comp, tmp);
    mergeSortSlots<T>(vals + mid, n - mid, comp, tmp);
    size_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (comp(fromSlot<T>(vals[i]), fromSlot<T>(vals[j])))
            tmp[k++] = vals[i++];
        else
            tmp[k++] = vals[j++];
    }
    while (i < mid)
        tmp[k++] = vals[i++];
    while (j < n)
        tmp[k++] = vals[j++];
    for (i = 0; i < n; ++i)
        vals[i] = tmp[i];
}

template <typename T>
static Array *
__doSortSlots__(bool inplace, Array *arr, SortAlgo algo, const ArrayType *arrType, Context &ctx) {
    size_t n = arr->size();
    if (n <= 1) {
        if (!inplace)
            return static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));
        return arr;
    }
    Array *target = inplace ? arr : static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));
    slot_t *slots = target->data();
    auto comp     = [](T a, T b) { return a < b; };
    switch (algo) {
    case SortAlgo::Insertion:
        insertionSortSlots<T>(slots, n, comp);
        break;
    case SortAlgo::Quick:
        quickSortSlots<T>(slots, 0, static_cast<ptrdiff_t>(n) - 1, comp);
        break;
    case SortAlgo::Merge: {
        std::vector<slot_t> tmp(n);
        mergeSortSlots<T>(slots, n, comp, tmp.data());
        break;
    }
    default:
        throwRuntimeFault(RuntimeDiag::RuntimeError, "Unknown sort algorithm");
    }
    return target;
}

template <SortAlgo Algo>
static Array *__doSortWithTypeCode__(
    bool inplace, Array *arr, const ArrayType *arrType, Context &ctx, std::string_view fname) {
    TypeCode code = arrType->elemTypeCode();
    Array *result = nullptr;
    switch (code) {
    case TypeCode::Int32:
        result = __doSortSlots__<Int32>(inplace, arr, Algo, arrType, ctx);
        break;
    case TypeCode::Int64:
        result = __doSortSlots__<Int64>(inplace, arr, Algo, arrType, ctx);
        break;
    case TypeCode::Float32:
        result = __doSortSlots__<Float32>(inplace, arr, Algo, arrType, ctx);
        break;
    case TypeCode::Float64:
        result = __doSortSlots__<Float64>(inplace, arr, Algo, arrType, ctx);
        break;
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            std::string(fname),
            typeCodeToString(code) + "[]",
            "an array of numbers");
    }
    return result;
}

template <typename T> static Array *__merge_sorted_arrays_slots__(Array *lhs, Array *rhs) {
    size_t nL = lhs->size(), nR = rhs->size();
    Array *result = Array::create(mm::autoSpace(), nL + nR);
    size_t i = 0, j = 0, k = 0;
    while (i < nL && j < nR) {
        T a = lhs->get<T>(i), b = rhs->get<T>(j);
        if (a < b)
            result->set(k++, a), ++i;
        else
            result->set(k++, b), ++j;
    }
    while (i < nL)
        result->set(k++, lhs->get<T>(i++));
    while (j < nR)
        result->set(k++, rhs->get<T>(j++));
    return result;
}

#define ALGO_SORT_IMPL(name, inplace, algo)                                                        \
    slot_t __algo_##name##__(ArgsView &with, ArgsView &norm, Context &ctx) {                       \
        Array *arr               = norm.get<Array *>(0);                                           \
        const ArrayType *arrType = tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));         \
        Array *result = __doSortWithTypeCode__<algo>(inplace, arr, arrType, ctx, "<" #name ">");   \
        return result ? toSlot(result) : NullSlot;                                                 \
    }

ALGO_SORT_IMPL(insert_sort, false, SortAlgo::Insertion)
ALGO_SORT_IMPL(insert_sort_inplace, true, SortAlgo::Insertion)
ALGO_SORT_IMPL(quick_sort, false, SortAlgo::Quick)
ALGO_SORT_IMPL(quick_sort_inplace, true, SortAlgo::Quick)
ALGO_SORT_IMPL(merge_sort, false, SortAlgo::Merge)
ALGO_SORT_IMPL(merge_sort_inplace, true, SortAlgo::Merge)

#undef ALGO_SORT_IMPL

slot_t __algo_merge_sorted_arrays__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Array *lhs               = norm.get<Array *>(0);
    Array *rhs               = norm.get<Array *>(1);
    const ArrayType *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    TypeCode code            = arrType->elemTypeCode();
    Array *merged            = nullptr;
    switch (code) {
    case TypeCode::Int32:
        merged = __merge_sorted_arrays_slots__<Int32>(lhs, rhs);
        break;
    case TypeCode::Int64:
        merged = __merge_sorted_arrays_slots__<Int64>(lhs, rhs);
        break;
    case TypeCode::Float32:
        merged = __merge_sorted_arrays_slots__<Float32>(lhs, rhs);
        break;
    case TypeCode::Float64:
        merged = __merge_sorted_arrays_slots__<Float64>(lhs, rhs);
        break;
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "algo.merge_sorted_arrays",
            typeCodeToString(code) + "[]",
            "two arrays of numbers");
    }
    return toSlot(merged);
}

std::unordered_map<std::string, operator_t> getAlgoOpsMap() {
    return {
        {"sort", __algo_quick_sort_inplace__},
        {"sorted", __algo_quick_sort__},
        {"insert_sort", __algo_insert_sort_inplace__},
        {"insert_sorted", __algo_insert_sort__},
        {"quick_sort", __algo_quick_sort_inplace__},
        {"quick_sorted", __algo_quick_sort__},
        {"merge_sort", __algo_merge_sort_inplace__},
        {"merge_sorted", __algo_merge_sort__},
        {"merge_sorted_arrays", __algo_merge_sorted_arrays__},
    };
}
