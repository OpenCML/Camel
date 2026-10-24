// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The native operators of the `array` module (stdlib/array.cml): the sorting family, the merge
 * of two sorted arrays, the primitives `flatten`, `unique` and `argsort` that the rest of the
 * module is written on, and the reductions `sum`, `min` and `max` (builtin/operators/
 * array_reduce.cpp). They answer on the `array` protocol; the built-in module registers the
 * executor factory under that name, so the module written in Camel needs no second module of its
 * own to load them.
 *
 * Each is here because composing it from the built-in operators would cost more than its size: a
 * `flatten` folded with `concat` copies the result once per inner array, a `unique` folded with
 * `contains` and `concat` is quadratic and copies as well, a sort written with recursion would
 * recurse once per element, and a reduction written with `reduce` calls the combining function
 * once per element through the interpreter instead of sweeping the array (design principles
 * 10.4). The sorts are the one family with in-place forms: the compiler rewrites
 * `xs = xs->sort` to the in-place form when nothing else refers to the array (design principles
 * 11.3), so the forms change their operand and their result is that operand.
 */

#include "array.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

// A float order in which every NaN comes after every number, so that it is an order at all.
template <typename T> bool numberLess(T a, T b) {
    if constexpr (std::is_floating_point_v<T>) {
        if (std::isnan(b))
            return !std::isnan(a);
        if (std::isnan(a))
            return false;
    }
    return a < b;
}

// ---- sorting ----

// (keys: number[] | bool[] | string[]) => int[]: the positions of the keys in ascending order of
// key. Equal keys keep their order (a stable sort), so sorting by one key and then by another
// works.
slot_t __array_argsort__(ArgsView &, ArgsView &norm, Context &) {
    Array *keys         = norm.get<Array *>(0);
    const TypeCode code = tt::as_ptr<ArrayType>(norm.type(0))->elemTypeCode();
    const size_t n      = keys->size();
    std::vector<Int64> order(n);
    std::iota(order.begin(), order.end(), Int64(0));
    const auto sortBy = [&](auto less) { std::stable_sort(order.begin(), order.end(), less); };
    switch (code) {
    case TypeCode::Int32:
        sortBy([&](Int64 a, Int64 b) { return keys->get<Int32>(a) < keys->get<Int32>(b); });
        break;
    case TypeCode::Int64:
        sortBy([&](Int64 a, Int64 b) { return keys->get<Int64>(a) < keys->get<Int64>(b); });
        break;
    case TypeCode::Float32:
        sortBy([&](Int64 a, Int64 b) {
            return numberLess(keys->get<Float32>(a), keys->get<Float32>(b));
        });
        break;
    case TypeCode::Float64:
        sortBy([&](Int64 a, Int64 b) {
            return numberLess(keys->get<Float64>(a), keys->get<Float64>(b));
        });
        break;
    case TypeCode::Bool:
        sortBy([&](Int64 a, Int64 b) { return keys->get<Bool>(a) < keys->get<Bool>(b); });
        break;
    case TypeCode::String:
        // By byte, as the comparison operators order strings.
        sortBy([&](Int64 a, Int64 b) {
            return keys->get<String *>(a)->compare(keys->get<String *>(b)) < 0;
        });
        break;
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "array.argsort",
            typeCodeToString(code) + "[]",
            "an array of i32, i64, f32, f64, bool or string values");
    }
    Array *result = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), n);
    for (size_t i = 0; i < n; ++i)
        result->set<Int64>(i, order[i]);
    return toSlot(result);
}

// ---- sequence ----

// (xss: T[][]) => T[]: the elements of the inner arrays, in order.
slot_t __array_flatten__(ArgsView &, ArgsView &norm, Context &) {
    Array *outer          = norm.get<Array *>(0);
    const auto *innerType = tt::as_ptr<ArrayType>(tt::as_ptr<ArrayType>(norm.type(0))->elemType());
    size_t total          = 0;
    for (size_t i = 0; i < outer->size(); ++i)
        total += outer->get<Array *>(i)->size();
    Array *result = Array::create(mm::autoSpace(), innerType, total);
    // The argument is read again after the allocation.
    outer    = norm.get<Array *>(0);
    size_t k = 0;
    for (size_t i = 0; i < outer->size(); ++i) {
        Array *inner = outer->get<Array *>(i);
        for (size_t j = 0; j < inner->size(); ++j)
            result->set<slot_t>(k++, inner->get<slot_t>(j), innerType);
    }
    return toSlot(result);
}

// (xs: T[]) => T[]: the first occurrence of every value, in order. Values are compared as
// `==` compares them, so a NaN is never a duplicate of another NaN.
slot_t __array_unique__(ArgsView &, ArgsView &norm, Context &) {
    Array *arr          = norm.get<Array *>(0);
    const auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    const TypeCode code = arrType->elemTypeCode();
    Type *elemType      = arrType->elemType();
    const size_t n      = arr->size();
    std::vector<size_t> kept;
    kept.reserve(n);
    if (code == TypeCode::String) {
        std::unordered_set<std::string_view> seen;
        for (size_t i = 0; i < n; ++i) {
            if (seen.insert(arr->get<String *>(i)->view()).second)
                kept.push_back(i);
        }
    } else if (code == TypeCode::Float32 || code == TypeCode::Float64) {
        // By value, not by bits: 0.0 equals -0.0, and a NaN equals nothing.
        std::unordered_set<double> seen;
        for (size_t i = 0; i < n; ++i) {
            const double v = code == TypeCode::Float32 ? static_cast<double>(arr->get<Float32>(i))
                                                       : arr->get<Float64>(i);
            if (std::isnan(v) || seen.insert(v).second)
                kept.push_back(i);
        }
    } else if (!isGCTraced(code)) {
        std::unordered_set<slot_t> seen;
        for (size_t i = 0; i < n; ++i) {
            if (seen.insert(arr->get<slot_t>(i)).second)
                kept.push_back(i);
        }
    } else {
        // Composite values: compared pairwise, as `contains` does.
        for (size_t i = 0; i < n; ++i) {
            Object *value  = arr->get<Object *>(i);
            bool duplicate = false;
            for (size_t k : kept) {
                if (arr->get<Object *>(k)->equals(value, elemType, false)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                kept.push_back(i);
        }
    }
    Array *result = Array::create(mm::autoSpace(), arrType, kept.size());
    arr           = norm.get<Array *>(0);
    for (size_t k = 0; k < kept.size(); ++k)
        result->set<slot_t>(k, arr->get<slot_t>(kept[k]), arrType);
    return toSlot(result);
}

// ---- the sorts ----

enum class SortAlgo { Insertion, Quick, Merge };

template <typename T, typename Compare> void insertionSortSlots(T *vals, size_t n, Compare comp) {
    for (size_t i = 1; i < n; ++i) {
        T key    = vals[i];
        size_t j = i;
        while (j > 0 && comp(key, vals[j - 1])) {
            vals[j] = vals[j - 1];
            --j;
        }
        vals[j] = key;
    }
}

/// Quicksort over [left, right]: median-of-three pivot, Hoare partition, insertion sort for short
/// ranges. Recursion only into the smaller part, so the depth stays logarithmic on sorted or
/// constant input.
template <typename T, typename Compare>
void quickSortSlots(T *vals, ptrdiff_t left, ptrdiff_t right, Compare comp) {
    while (right - left >= 16) {
        const ptrdiff_t mid = left + (right - left) / 2;
        if (comp(vals[mid], vals[left]))
            std::swap(vals[mid], vals[left]);
        if (comp(vals[right], vals[left]))
            std::swap(vals[right], vals[left]);
        if (comp(vals[right], vals[mid]))
            std::swap(vals[right], vals[mid]);
        const T pivot = vals[mid];
        ptrdiff_t i = left, j = right;
        while (i <= j) {
            while (comp(vals[i], pivot))
                ++i;
            while (comp(pivot, vals[j]))
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
void mergeSortSlots(T *vals, size_t n, Compare comp, T *tmp) {
    if (n <= 1)
        return;
    size_t mid = n / 2;
    mergeSortSlots<T>(vals, mid, comp, tmp);
    mergeSortSlots<T>(vals + mid, n - mid, comp, tmp);
    size_t i = 0, j = mid, k = 0;
    while (i < mid && j < n) {
        if (comp(vals[i], vals[j]))
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

// Sorts `arr` and returns it: a copy of it, as the array a program passes is never changed
// (design principles 11.3), or, for the in-place forms (`inPlace`), the array itself, which
// the compiler hands over only when no one else refers to it (design principles 11.3;
// Builder::inPlaceRebindingOf). The elements are numbers: the slots hold no references, so
// reordering them needs no write barrier.
template <typename T>
Array *__doSortSlots__(Array *arr, SortAlgo algo, const ArrayType *arrType, bool inPlace) {
    size_t n = arr->size();
    // The in-place form is only taken when the block is unshared and writable; a view or an
    // imported (external) buffer means someone else sees the old order.
    Array *target = (inPlace && arr->canWriteInPlace())
                        ? arr
                        : static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));
    if (n <= 1) {
        return target;
    }
    T *vals   = target->dataAs<T>();
    auto comp = [](T a, T b) { return a < b; };
    switch (algo) {
    case SortAlgo::Insertion:
        insertionSortSlots<T>(vals, n, comp);
        break;
    case SortAlgo::Quick:
        quickSortSlots<T>(vals, 0, static_cast<ptrdiff_t>(n) - 1, comp);
        break;
    case SortAlgo::Merge: {
        std::vector<T> tmp(n);
        mergeSortSlots<T>(vals, n, comp, tmp.data());
        break;
    }
    default:
        throwRuntimeFault(RuntimeDiag::RuntimeError, "Unknown sort algorithm");
    }
    return target;
}

template <SortAlgo Algo>
Array *
__doSortWithTypeCode__(Array *arr, const ArrayType *arrType, bool inPlace, std::string_view fname) {
    TypeCode code = arrType->elemTypeCode();
    Array *result = nullptr;
    // The element type is checked before anything is sorted: an in-place form that faults
    // leaves the array as it was.
    switch (code) {
    case TypeCode::Int32:
        result = __doSortSlots__<Int32>(arr, Algo, arrType, inPlace);
        break;
    case TypeCode::Int64:
        result = __doSortSlots__<Int64>(arr, Algo, arrType, inPlace);
        break;
    case TypeCode::Float32:
        result = __doSortSlots__<Float32>(arr, Algo, arrType, inPlace);
        break;
    case TypeCode::Float64:
        result = __doSortSlots__<Float64>(arr, Algo, arrType, inPlace);
        break;
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            std::string(fname),
            typeCodeToString(code) + "[]",
            "an array of i32, i64, f32 or f64 values");
    }
    return result;
}

template <typename T>
Array *__merge_sorted_arrays_slots__(Array *lhs, Array *rhs, const ArrayType *arrType) {
    size_t nL = lhs->size(), nR = rhs->size();
    Array *result = Array::create(mm::autoSpace(), arrType, nL + nR);
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

// `name` returns a sorted copy; `name_in_place` is its in-place form (see __doSortSlots__).
#define ARRAY_SORT_IMPL(name, algo)                                                                  \
    slot_t __array_##name##__(ArgsView &, ArgsView &norm, Context &) {                               \
        Array *arr               = norm.get<Array *>(0);                                             \
        const ArrayType *arrType = tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));           \
        Array *result            = __doSortWithTypeCode__<algo>(arr, arrType, false, "<" #name ">"); \
        return result ? toSlot(result) : NullSlot;                                                   \
    }                                                                                                \
    slot_t __array_##name##_in_place__(ArgsView &, ArgsView &norm, Context &) {                      \
        Array *arr               = norm.get<Array *>(0);                                             \
        const ArrayType *arrType = tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));           \
        Array *result            = __doSortWithTypeCode__<algo>(arr, arrType, true, "<" #name ">");  \
        return result ? toSlot(result) : NullSlot;                                                   \
    }

ARRAY_SORT_IMPL(insert_sort, SortAlgo::Insertion)
ARRAY_SORT_IMPL(quick_sort, SortAlgo::Quick)
ARRAY_SORT_IMPL(merge_sort, SortAlgo::Merge)

// `sort` is the quicksort; `quick_sort` is the same kernel under the algorithm's own name.
slot_t __array_sort__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return __array_quick_sort__(with, norm, ctx);
}
slot_t __array_sort_in_place__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return __array_quick_sort_in_place__(with, norm, ctx);
}

slot_t __array_merge_sorted_arrays__(ArgsView &, ArgsView &norm, Context &) {
    Array *lhs               = norm.get<Array *>(0);
    Array *rhs               = norm.get<Array *>(1);
    const ArrayType *arrType = tt::as_ptr<camel::core::type::ArrayType>(norm.type(0));
    TypeCode code            = arrType->elemTypeCode();
    Array *merged            = nullptr;
    switch (code) {
    case TypeCode::Int32:
        merged = __merge_sorted_arrays_slots__<Int32>(lhs, rhs, arrType);
        break;
    case TypeCode::Int64:
        merged = __merge_sorted_arrays_slots__<Int64>(lhs, rhs, arrType);
        break;
    case TypeCode::Float32:
        merged = __merge_sorted_arrays_slots__<Float32>(lhs, rhs, arrType);
        break;
    case TypeCode::Float64:
        merged = __merge_sorted_arrays_slots__<Float64>(lhs, rhs, arrType);
        break;
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "array.merge_sorted_arrays",
            typeCodeToString(code) + "[]",
            "two arrays of i32, i64, f32 or f64 values");
    }
    return toSlot(merged);
}

#undef ARRAY_SORT_IMPL

} // namespace

std::unordered_map<std::string, operator_t> getArrayOpsMap() {
    return {
        // One name per operation: `sort` returns a new array (design principles 11.3). The
        // `*_in_place` forms are the ones the compiler calls for `xs = xs->sort`
        // (OperatorTable::setInPlaceForm); no program names them.
        {"sort", __array_sort__},
        {"sort_in_place", __array_sort_in_place__},
        {"insert_sort", __array_insert_sort__},
        {"insert_sort_in_place", __array_insert_sort_in_place__},
        {"quick_sort", __array_quick_sort__},
        {"quick_sort_in_place", __array_quick_sort_in_place__},
        {"merge_sort", __array_merge_sort__},
        {"merge_sort_in_place", __array_merge_sort_in_place__},
        {"merge_sorted_arrays", __array_merge_sorted_arrays__},
        {"flatten", __array_flatten__},
        {"unique", __array_unique__},
        {"argsort", __array_argsort__},
        // The reductions' kernels (builtin/operators/array_reduce.cpp): one executor per element
        // type, named after the element type it takes. Each body-less declaration in
        // stdlib/array.cml names one of them with its `#op` tag and admits exactly that element
        // type with its own `when` (`Builder::declaredRuleApplies`), so the element type is known
        // where the executor is chosen and no executor asks an array what it holds. An element
        // type no declaration admits has no executor and runs the Camel body (H228: one operator,
        // per-type executors).
        {"sum_i", __array_sum_i__},
        {"sum_l", __array_sum_l__},
        {"sum_f", __array_sum_f__},
        {"sum_d", __array_sum_d__},
        {"sum_bf", __array_sum_bf__},
        {"sum_f6", __array_sum_f6__},
        {"min_i", __array_min_i__},
        {"min_l", __array_min_l__},
        {"min_f", __array_min_f__},
        {"min_d", __array_min_d__},
        {"min_bf", __array_min_bf__},
        {"min_f6", __array_min_f6__},
        {"min_b", __array_min_b__},
        {"min_s", __array_min_s__},
        {"max_i", __array_max_i__},
        {"max_l", __array_max_l__},
        {"max_f", __array_max_f__},
        {"max_d", __array_max_d__},
        {"max_bf", __array_max_bf__},
        {"max_f6", __array_max_f6__},
        {"max_b", __array_max_b__},
        {"max_s", __array_max_s__},
    };
}

void declareArrayOperatorFacts() {
    auto &table = camel::core::OperatorTable::instance();
    // Every operator returns a new array: none of them changes its argument (design principles
    // 11.3). Only one name per operation is kept (11.3: `sort`, not `sorted`).
    table.declare(
        "array",
        {
            {"sort", true},
            {"insert_sort", true},
            {"quick_sort", true},
            {"merge_sort", true},
            {"merge_sorted_arrays", true},
            {"flatten", true},
            {"unique", true},
            {"argsort", true},
            // The reductions' kernels are pure as the functions they execute are: the fold reads
            // the array it is given and touches nothing else. The declarations in the map above
            // name them; this is what tells the compiler what a call of one may touch. One entry
            // per element type, because one executor takes one element type.
            {"sum_i", true},
            {"sum_l", true},
            {"sum_f", true},
            {"sum_d", true},
            {"sum_bf", true},
            {"sum_f6", true},
            {"min_i", true},
            {"min_l", true},
            {"min_f", true},
            {"min_d", true},
            {"min_bf", true},
            {"min_f6", true},
            {"min_b", true},
            {"min_s", true},
            {"max_i", true},
            {"max_l", true},
            {"max_f", true},
            {"max_d", true},
            {"max_bf", true},
            {"max_f6", true},
            {"max_b", true},
            {"max_s", true},
        });
    // The in-place forms of the sorting operators (design principles 11.3): `xs = xs->sort`
    // sorts the variable's own array when nothing else refers to it (Builder::
    // inPlaceRebindingOf). Each changes the array it is given, so it is not pure; what it
    // changes is the program's own variable, so it has no effect outside the program. An array
    // of numbers is sorted without a fault, so the array is never left half done.
    for (const char *name : {"sort", "insert_sort", "quick_sort", "merge_sort"}) {
        const std::string uri = std::string("array:") + name;
        table.set(uri + "_in_place", {.stateOnly = true, .mayFault = true});
        table.setInPlaceForm(uri, uri + "_in_place");
        // The form returns its operand, changed: the one fact the temporary-value analysis needs.
        table.declareReusesOperand(uri + "_in_place");
    }
    // An in-place form changes the array it is given and returns it (docs/design/op-semantics
    // 8): a read-modify-write of its first operand, an object, whole.
    using namespace camel::core::sem;
    for (const char *name : {"sort", "insert_sort", "quick_sort", "merge_sort"}) {
        table.declareResources(
            std::string("array:") + name + "_in_place",
            ResourceSpec{
                .accesses = {operandAccess(ResourceClass::Object, 0, AccessMode::ReadModifyWrite)},
                .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0},
            });
    }
}