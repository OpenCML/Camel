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
 * Created: Oct. 29, 2025
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "algo.h"
#include "core/context/context.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

enum class SortAlgo { Insertion, Quick, Merge };

// ---------- 排序核心模板 ----------

template <typename T, typename Compare>
static void insertionSortSlots(slot_t *vals, size_t n, Compare comp) {
    for (size_t i = 1; i < n; ++i) {
        T key    = (T)vals[i];
        size_t j = i;
        while (j > 0 && comp(key, (T)vals[j - 1])) {
            vals[j] = vals[j - 1];
            --j;
        }
        vals[j] = key;
    }
}

template <typename T, typename Compare>
static void quickSortSlots(slot_t *vals, int left, int right, Compare comp) {
    if (left >= right)
        return;
    T pivot = (T)vals[left];
    int i = left, j = right;
    while (i < j) {
        while (i < j && !comp((T)vals[j], pivot))
            --j;
        while (i < j && comp((T)vals[i], pivot))
            ++i;
        if (i < j)
            std::swap(vals[i], vals[j]);
    }
    std::swap(vals[left], vals[i]);
    quickSortSlots<T>(vals, left, i - 1, comp);
    quickSortSlots<T>(vals, i + 1, right, comp);
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
        if (comp((T)vals[i], (T)vals[j]))
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

// ---------- 统一排序执行 ----------

template <typename T>
static Array *__doSortSlots__(
    bool inplace, Array *arr, const ArrayType *arrType, SortAlgo algo, Frame &frame, Context &ctx) {
    size_t n = arr->size();
    if (n <= 1) {
        if (!inplace)
            return Object::clone(arr, mm::autoSpace());
        return arr;
    }

    Array *target = inplace ? arr : Object::clone(arr, mm::autoSpace());
    slot_t *slots = target->data();
    auto comp     = [](T a, T b) { return a < b; };

    switch (algo) {
    case SortAlgo::Insertion:
        insertionSortSlots<T>(slots, n, comp);
        break;
    case SortAlgo::Quick:
        quickSortSlots<T>(slots, 0, (int)n - 1, comp);
        break;
    case SortAlgo::Merge: {
        std::vector<slot_t> tmp(n);
        mergeSortSlots<T>(slots, n, comp, tmp.data());
        break;
    }
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Unknown sort algorithm");
        return nullptr;
    }

    return target;
}

// =============================================================
// 通用工具函数：
// 根据元素类型 TypeCode 分派到模板
// =============================================================

template <SortAlgo Algo>
static Array *__doSortWithTypeCode__(
    bool inplace, Array *arr, const ArrayType *arrType, Frame &frame, Context &ctx,
    std::string_view fname) {
    TypeCode code = arrType->elemCode();
    Array *result = nullptr;

    switch (code) {
    case TypeCode::Int:
        result = __doSortSlots__<Int>(inplace, arr, arrType, Algo, frame, ctx);
        break;
    case TypeCode::Long:
        result = __doSortSlots__<Long>(inplace, arr, arrType, Algo, frame, ctx);
        break;
    case TypeCode::Float:
        result = __doSortSlots__<Float>(inplace, arr, arrType, Algo, frame, ctx);
        break;
    case TypeCode::Double:
        result = __doSortSlots__<Double>(inplace, arr, arrType, Algo, frame, ctx);
        break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(
                std::string(fname) + " not supported for element type " + typeCodeToString(code));
        return nullptr;
    }
    return result;
}

// ---------- 实际内建函数实现 ----------

void __insert_sort__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr               = frame.get<Array *>(nargs[0]);
    const ArrayType *arrType = frame.typePtrAt<ArrayType>(nargs[0]).get();

    Array *result = __doSortWithTypeCode__<SortAlgo::Insertion>(
        /*inplace*/ false,
        arr,
        arrType,
        frame,
        ctx,
        "<insert_sort>");

    if (!result)
        frame.set(self, NullSlot);
    else
        frame.set(self, result);
}

void __insert_sort_inplace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr               = frame.get<Array *>(nargs[0]);
    const ArrayType *arrType = frame.typePtrAt<ArrayType>(nargs[0]).get();

    Array *result = __doSortWithTypeCode__<SortAlgo::Insertion>(
        /*inplace*/ true,
        arr,
        arrType,
        frame,
        ctx,
        "<insert_sort_inplace>");

    if (!result)
        frame.set(self, NullSlot);
    else
        frame.set(self, result);
}

void __quick_sort__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr               = frame.get<Array *>(nargs[0]);
    const ArrayType *arrType = frame.typePtrAt<ArrayType>(nargs[0]).get();

    Array *result =
        __doSortWithTypeCode__<SortAlgo::Quick>(false, arr, arrType, frame, ctx, "<quick_sort>");

    if (!result)
        frame.set(self, NullSlot);
    else
        frame.set(self, result);
}

void __quick_sort_inplace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr               = frame.get<Array *>(nargs[0]);
    const ArrayType *arrType = frame.typePtrAt<ArrayType>(nargs[0]).get();

    Array *result = __doSortWithTypeCode__<SortAlgo::Quick>(
        true,
        arr,
        arrType,
        frame,
        ctx,
        "<quick_sort_inplace>");

    if (!result)
        frame.set(self, NullSlot);
    else
        frame.set(self, result);
}

void __merge_sort__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr               = frame.get<Array *>(nargs[0]);
    const ArrayType *arrType = frame.typePtrAt<ArrayType>(nargs[0]).get();

    Array *result =
        __doSortWithTypeCode__<SortAlgo::Merge>(false, arr, arrType, frame, ctx, "<merge_sort>");

    if (!result)
        frame.set(self, NullSlot);
    else
        frame.set(self, result);
}

void __merge_sort_inplace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *arr               = frame.get<Array *>(nargs[0]);
    const ArrayType *arrType = frame.typePtrAt<ArrayType>(nargs[0]).get();

    Array *result = __doSortWithTypeCode__<SortAlgo::Merge>(
        true,
        arr,
        arrType,
        frame,
        ctx,
        "<merge_sort_inplace>");

    if (!result)
        frame.set(self, NullSlot);
    else
        frame.set(self, result);
}

// ---------- 合并已排序数组 ----------

template <typename T>
static Array *__merge_sorted_arrays_slots__(Array *lhs, Array *rhs, const ArrayTypeLayout &layout) {
    size_t nL     = lhs->size();
    size_t nR     = rhs->size();
    Array *result = Array::create(layout, mm::autoSpace());
    result->reserve(nL + nR);

    size_t i = 0, j = 0, k = 0;
    while (i < nL && j < nR) {
        T a = lhs->get<T>(i);
        T b = rhs->get<T>(j);
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

void __merge_sorted_arrays__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    Array *lhs    = frame.get<Array *>(nargs[0]);
    Array *rhs    = frame.get<Array *>(nargs[1]);
    Array *merged = nullptr;

    TypeCode code = lhs->elemType();

    switch (code) {
    case TypeCode::Int:
        merged = __merge_sorted_arrays_slots__<Int>(lhs, rhs, lhs->layout());
        break;
    case TypeCode::Long:
        merged = __merge_sorted_arrays_slots__<Long>(lhs, rhs, lhs->layout());
        break;
    case TypeCode::Float:
        merged = __merge_sorted_arrays_slots__<Float>(lhs, rhs, lhs->layout());
        break;
    case TypeCode::Double:
        merged = __merge_sorted_arrays_slots__<Double>(lhs, rhs, lhs->layout());
        break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(
                "<merge_sorted_arrays> not supported for element type " + typeCodeToString(code));
        frame.set(self, NullSlot);
        return;
    }

    frame.set(self, merged);
}
