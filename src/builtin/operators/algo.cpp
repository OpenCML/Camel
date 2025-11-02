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
 * Updated: Oct. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "algo.h"
#include "core/context/context.h"

enum class SortAlgo { Insertion, Quick, Merge };

template <typename T, typename Compare>
static void insertionSort(std::vector<T> &vals, Compare comp) {
    for (size_t i = 1; i < vals.size(); ++i) {
        T key = vals[i];
        size_t j = i;
        while (j > 0 && comp(key, vals[j - 1])) {
            vals[j] = vals[j - 1];
            --j;
        }
        vals[j] = key;
    }
}

template <typename T, typename Compare>
static void quickSort(std::vector<T> &vals, Compare comp, int left, int right) {
    if (left >= right)
        return;
    T pivot = vals[left];
    int i = left, j = right;
    while (i < j) {
        while (i < j && !comp(vals[j], pivot))
            --j;
        while (i < j && comp(vals[i], pivot))
            ++i;
        if (i < j)
            std::swap(vals[i], vals[j]);
    }
    std::swap(vals[left], vals[i]);
    quickSort(vals, comp, left, i - 1);
    quickSort(vals, comp, i + 1, right);
}

template <typename T, typename Compare> static void mergeSort(std::vector<T> &vals, Compare comp) {
    if (vals.size() <= 1)
        return;
    auto mid = vals.size() / 2;
    std::vector<T> left(vals.begin(), vals.begin() + mid);
    std::vector<T> right(vals.begin() + mid, vals.end());
    mergeSort(left, comp);
    mergeSort(right, comp);

    size_t i = 0, j = 0, k = 0;
    while (i < left.size() && j < right.size()) {
        if (comp(left[i], right[j])) {
            vals[k++] = left[i++];
        } else {
            vals[k++] = right[j++];
        }
    }
    while (i < left.size())
        vals[k++] = left[i++];
    while (j < right.size())
        vals[k++] = right[j++];
}

template <typename T, typename DataType>
void doSort(
    bool inplace, const std::vector<data_ptr_t> &rawCopy, std::vector<data_ptr_t> &raw,
    const type_ptr_t &elemType, GraphIR::data_idx_t self, GraphIR::data_idx_t arr, Frame &frame,
    SortAlgo algo) {

    auto comparator = [&](const data_ptr_t &a, const data_ptr_t &b) {
        return static_cast<DataType &>(*a).data() < static_cast<DataType &>(*b).data();
    };

    auto runSort = [&](std::vector<data_ptr_t> &vec) {
        switch (algo) {
        case SortAlgo::Insertion:
            insertionSort<data_ptr_t>(vec, comparator);
            break;
        case SortAlgo::Quick:
            quickSort<data_ptr_t>(vec, comparator, 0, (int)vec.size() - 1);
            break;
        case SortAlgo::Merge:
            mergeSort<data_ptr_t>(vec, comparator);
            break;
        default:
            throw std::runtime_error("Unknown sort algorithm");
        }
    };

    if (inplace) {
        runSort(raw);
        frame.set(self, frame.get(arr)); // 原数组已修改
    } else {
        std::vector<data_ptr_t> sorted = rawCopy;
        runSort(sorted);
        frame.set(self, std::make_shared<ArrayData>(Type::Array(elemType), std::move(sorted)));
    }
}

void __insert_sort__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arr_val = frame.get(nargs[0]);
    auto arr = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elementType();
    const auto &rawCopy = arr->raw();
    auto &raw = const_cast<std::vector<data_ptr_t> &>(arr->raw());

    if (elemType == Type::Int()) {
        doSort<int32_t, IntData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else if (elemType == Type::Long()) {
        doSort<int64_t, LongData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else if (elemType == Type::Float()) {
        doSort<float, FloatData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else if (elemType == Type::Double()) {
        doSort<double, DoubleData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<insert_sort> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
    }
}

void __insert_sort_inplace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arr_val = frame.get(nargs[0]);
    auto arr = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elementType();
    const auto &rawCopy = arr->raw();
    auto &raw = const_cast<std::vector<data_ptr_t> &>(arr->raw());

    if (elemType == Type::Int()) {
        doSort<int32_t, IntData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else if (elemType == Type::Long()) {
        doSort<int64_t, LongData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else if (elemType == Type::Float()) {
        doSort<float, FloatData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else if (elemType == Type::Double()) {
        doSort<double, DoubleData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Insertion);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<insert_sort> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
    }
}

void __quick_sort__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    const data_ptr_t &arr_val = frame.get(nargs[0]);
    auto arr = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elementType();
    const auto &rawCopy = arr->raw();
    auto &raw = const_cast<std::vector<data_ptr_t> &>(arr->raw());

    if (elemType == Type::Int()) {
        doSort<int32_t, IntData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else if (elemType == Type::Long()) {
        doSort<int64_t, LongData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else if (elemType == Type::Float()) {
        doSort<float, FloatData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else if (elemType == Type::Double()) {
        doSort<double, DoubleData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<quick_sort> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
    }
}

void __quick_sort_inplace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    const data_ptr_t &arr_val = frame.get(nargs[0]);
    auto arr = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elementType();
    const auto &rawCopy = arr->raw();
    auto &raw = const_cast<std::vector<data_ptr_t> &>(arr->raw());

    if (elemType == Type::Int()) {
        doSort<int32_t, IntData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else if (elemType == Type::Long()) {
        doSort<int64_t, LongData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else if (elemType == Type::Float()) {
        doSort<float, FloatData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else if (elemType == Type::Double()) {
        doSort<double, DoubleData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Quick);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<quick_sort> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
    }
}

void __merge_sort__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    const data_ptr_t &arr_val = frame.get(nargs[0]);
    auto arr = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elementType();
    const auto &rawCopy = arr->raw();
    auto &raw = const_cast<std::vector<data_ptr_t> &>(arr->raw());

    if (elemType == Type::Int()) {
        doSort<int32_t, IntData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else if (elemType == Type::Long()) {
        doSort<int64_t, LongData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else if (elemType == Type::Float()) {
        doSort<float, FloatData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else if (elemType == Type::Double()) {
        doSort<double, DoubleData>(
            false,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<merge_sort> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
    }
}

void __merge_sort_inplace__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    const data_ptr_t &arr_val = frame.get(nargs[0]);
    auto arr = arr_val->as<ArrayData>(arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(arr_val->type())->elementType();
    const auto &rawCopy = arr->raw();
    auto &raw = const_cast<std::vector<data_ptr_t> &>(arr->raw());

    if (elemType == Type::Int()) {
        doSort<int32_t, IntData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else if (elemType == Type::Long()) {
        doSort<int64_t, LongData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else if (elemType == Type::Float()) {
        doSort<float, FloatData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else if (elemType == Type::Double()) {
        doSort<double, DoubleData>(
            true,
            rawCopy,
            raw,
            elemType,
            self,
            nargs[0],
            frame,
            SortAlgo::Merge);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<merge_sort> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
    }
}

template <typename T, typename DataT>
static std::vector<data_ptr_t>
merge_sorted_arrays(const std::vector<data_ptr_t> &L, const std::vector<data_ptr_t> &R) {

    std::vector<data_ptr_t> merged;
    merged.reserve(L.size() + R.size());

    size_t i = 0, j = 0;
    while (i < L.size() && j < R.size()) {
        auto li = static_cast<DataT &>(*L[i]).data();
        auto rj = static_cast<DataT &>(*R[j]).data();
        if (li < rj) {
            merged.push_back(L[i++]);
        } else {
            merged.push_back(R[j++]);
        }
    }
    while (i < L.size())
        merged.push_back(L[i++]);
    while (j < R.size())
        merged.push_back(R[j++]);

    return merged;
}

void __merge_sorted_arrays__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    const data_ptr_t &left_arr_val = frame.get(nargs[0]);
    const data_ptr_t &right_arr_val = frame.get(nargs[1]);

    auto left = left_arr_val->as<ArrayData>(left_arr_val->type());
    auto right = right_arr_val->as<ArrayData>(right_arr_val->type());
    auto elemType = tt::as_shared<ArrayType>(left_arr_val->type())->elementType();

    const auto &L = left->raw();
    const auto &R = right->raw();

    std::vector<data_ptr_t> merged;

    if (elemType == Type::Int()) {
        merged = merge_sorted_arrays<int32_t, IntData>(L, R);
    } else if (elemType == Type::Long()) {
        merged = merge_sorted_arrays<int64_t, LongData>(L, R);
    } else if (elemType == Type::Float()) {
        merged = merge_sorted_arrays<float, FloatData>(L, R);
    } else if (elemType == Type::Double()) {
        merged = merge_sorted_arrays<double, DoubleData>(L, R);
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<merge_sorted> not supported for element type " + elemType->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, std::make_shared<ArrayData>(Type::Array(elemType), std::move(merged)));
}
