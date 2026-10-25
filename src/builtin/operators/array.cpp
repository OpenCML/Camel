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
#include <iterator>
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
// works. One executor per key element type: which one runs is fixed by the declaration the call
// compiled through (`#op<'array:argsort_i'>` and its siblings, stdlib/array.cml), so nothing here
// asks the array which element type it holds and an element type with no declaration is refused
// where the call is written rather than here.

/// The order `argsort` puts two keys in: a NaN comes after every number, a string is compared by
/// its bytes (as the comparison operators order strings), and a bool is `false` below `true`.
template <typename Key> bool keyLess(Array *keys, Int64 a, Int64 b) {
    if constexpr (std::is_same_v<Key, String *>) {
        return keys->get<String *>(a)->compare(keys->get<String *>(b)) < 0;
    } else {
        return numberLess(keys->get<Key>(a), keys->get<Key>(b));
    }
}

template <typename Key> slot_t argsortOf(ArgsView &norm) {
    Array *keys    = norm.get<Array *>(0);
    const size_t n = keys->size();
    std::vector<Int64> order(n);
    std::iota(order.begin(), order.end(), Int64(0));
    std::stable_sort(
        order.begin(), order.end(), [&](Int64 a, Int64 b) { return keyLess<Key>(keys, a, b); });
    Array *result = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), n);
    for (size_t i = 0; i < n; ++i)
        result->set<Int64>(i, order[i]);
    return toSlot(result);
}

#define ARRAY_ARGSORT_IMPL(suffix, Key)                                             \
    slot_t __array_argsort_##suffix##__(ArgsView &, ArgsView &norm, Context &) {     \
        return argsortOf<Key>(norm);                                                \
    }

ARRAY_ARGSORT_IMPL(i, Int32)
ARRAY_ARGSORT_IMPL(l, Int64)
ARRAY_ARGSORT_IMPL(f, Float32)
ARRAY_ARGSORT_IMPL(d, Float64)
ARRAY_ARGSORT_IMPL(b, Bool)
ARRAY_ARGSORT_IMPL(s, String *)

#undef ARRAY_ARGSORT_IMPL

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

/// The three algorithms the sorts are written under. Which one an executor runs is a template
/// parameter, so no executor switches on it and the fault the old `switch` had for a value that
/// is not one of them is not a path any more (kUnknownSortAlgorithm, __doSortSlots__).
enum class SortAlgo { Insertion, Quick, Merge };

/// False for every algorithm: the condition of the `static_assert` that stands where the old
/// `default:` arm was, so an algorithm added to the enum without a body here is refused where the
/// file is compiled rather than running as an unsorted array.
template <SortAlgo> inline constexpr bool kUnknownSortAlgorithm = false;

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
//
// The algorithm and the element type are template parameters, because both are fixed where the
// executor is chosen: the declaration a call compiled through names its own element type
// (`#op<'array:sort_i'>`, ...), and the algorithm is the name the call used. Nothing here reads
// the element type code, so there is no run-time switch on it and no default arm that could
// guess: an element type no declaration names never reaches an executor at all.
template <SortAlgo Algo, typename T> Array *__doSortSlots__(Array *arr, const ArrayType *arrType, bool inPlace) {
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
    if constexpr (Algo == SortAlgo::Insertion) {
        insertionSortSlots<T>(vals, n, comp);
    } else if constexpr (Algo == SortAlgo::Quick) {
        quickSortSlots<T>(vals, 0, static_cast<ptrdiff_t>(n) - 1, comp);
    } else if constexpr (Algo == SortAlgo::Merge) {
        std::vector<T> tmp(n);
        mergeSortSlots<T>(vals, n, comp, tmp.data());
    } else {
        static_assert(kUnknownSortAlgorithm<Algo>, "unknown sort algorithm");
    }
    return target;
}

template <SortAlgo Algo, typename T> slot_t sortSlotsOf(ArgsView &norm, bool inPlace) {
    Array *arr          = norm.get<Array *>(0);
    const auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    return toSlot(__doSortSlots__<Algo, T>(arr, arrType, inPlace));
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

template <typename T> slot_t mergeSortedArraysOf(ArgsView &norm) {
    Array *lhs          = norm.get<Array *>(0);
    Array *rhs          = norm.get<Array *>(1);
    const auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    return toSlot(__merge_sorted_arrays_slots__<T>(lhs, rhs, arrType));
}

// One executor per element type for each algorithm: the returning form, and the in-place form the
// compiler calls for `xs = xs->sort` (OperatorTable::setInPlaceForm; no program names either).
// `sort` is the quicksort, so it shares the quicksort's pair rather than holding a second copy of
// it: the two are one operation under two names (design principles 11.3).
#define ARRAY_SORT_EXECUTOR(suffix, Algo, T)                                          \
    slot_t __array_##suffix##__(ArgsView &, ArgsView &norm, Context &) {              \
        return sortSlotsOf<Algo, T>(norm, false);                                     \
    }                                                                                 \
    slot_t __array_##suffix##_in_place__(ArgsView &, ArgsView &norm, Context &) {     \
        return sortSlotsOf<Algo, T>(norm, true);                                      \
    }

#define ARRAY_SORT_FAMILY(alg, Algo)                                                  \
    ARRAY_SORT_EXECUTOR(alg##_i, Algo, Int32)                                         \
    ARRAY_SORT_EXECUTOR(alg##_l, Algo, Int64)                                         \
    ARRAY_SORT_EXECUTOR(alg##_f, Algo, Float32)                                       \
    ARRAY_SORT_EXECUTOR(alg##_d, Algo, Float64)

ARRAY_SORT_FAMILY(insert_sort, SortAlgo::Insertion)
ARRAY_SORT_FAMILY(quick_sort, SortAlgo::Quick)
ARRAY_SORT_FAMILY(merge_sort, SortAlgo::Merge)

#undef ARRAY_SORT_FAMILY
#undef ARRAY_SORT_EXECUTOR

#define ARRAY_MERGE_SORTED_IMPL(suffix, T)                                                  \
    slot_t __array_merge_sorted_arrays_##suffix##__(ArgsView &, ArgsView &norm, Context &) { \
        return mergeSortedArraysOf<T>(norm);                                                \
    }

ARRAY_MERGE_SORTED_IMPL(i, Int32)
ARRAY_MERGE_SORTED_IMPL(l, Int64)
ARRAY_MERGE_SORTED_IMPL(f, Float32)
ARRAY_MERGE_SORTED_IMPL(d, Float64)

#undef ARRAY_MERGE_SORTED_IMPL

// ---- the sorting family's element types, one entry each ----

// Everything below is derived from these two tables, so a name and the executor it stands for
// cannot drift apart: the operator names of `stdlib/array.cml`'s declarations, the ops map, the
// purity and resource declarations and the in-place form of each operator are all one table's.

/// One element type of the sorting family: the suffix its operator names carry (the reductions'
/// convention — `_i` i32, `_l` int, `_f` f32, `_d` float), the two executors of each of
/// `kSortAlgorithms` in that order (the returning form first, the in-place form beside it), and
/// the executor of `merge_sorted_arrays`.
struct SortType {
    const char *suffix;
    operator_t forms[4][2];
    operator_t mergeSorted;
};

/// The four algorithm names the sorts are written under, in the order of `SortType::forms`.
/// `sort` is the quicksort: its pair of executors is the quicksort's.
constexpr const char *kSortAlgorithms[] = {"sort", "insert_sort", "quick_sort", "merge_sort"};

const SortType kSortTypes[] = {
    {"i",
     {{__array_quick_sort_i__, __array_quick_sort_i_in_place__},
      {__array_insert_sort_i__, __array_insert_sort_i_in_place__},
      {__array_quick_sort_i__, __array_quick_sort_i_in_place__},
      {__array_merge_sort_i__, __array_merge_sort_i_in_place__}},
     __array_merge_sorted_arrays_i__},
    {"l",
     {{__array_quick_sort_l__, __array_quick_sort_l_in_place__},
      {__array_insert_sort_l__, __array_insert_sort_l_in_place__},
      {__array_quick_sort_l__, __array_quick_sort_l_in_place__},
      {__array_merge_sort_l__, __array_merge_sort_l_in_place__}},
     __array_merge_sorted_arrays_l__},
    {"f",
     {{__array_quick_sort_f__, __array_quick_sort_f_in_place__},
      {__array_insert_sort_f__, __array_insert_sort_f_in_place__},
      {__array_quick_sort_f__, __array_quick_sort_f_in_place__},
      {__array_merge_sort_f__, __array_merge_sort_f_in_place__}},
     __array_merge_sorted_arrays_f__},
    {"d",
     {{__array_quick_sort_d__, __array_quick_sort_d_in_place__},
      {__array_insert_sort_d__, __array_insert_sort_d_in_place__},
      {__array_quick_sort_d__, __array_quick_sort_d_in_place__},
      {__array_merge_sort_d__, __array_merge_sort_d_in_place__}},
     __array_merge_sorted_arrays_d__},
};

/// The element types `argsort` takes keys of: the four above, and the two ordered types the sorts
/// do not order (`_b` bool, `_s` string).
const std::pair<const char *, operator_t> kArgsortTypes[] = {
    {"i", __array_argsort_i__},
    {"l", __array_argsort_l__},
    {"f", __array_argsort_f__},
    {"d", __array_argsort_d__},
    {"b", __array_argsort_b__},
    {"s", __array_argsort_s__},
};

/// The name of one form of one operation for one element type, without the module prefix:
/// `<alg>_<suffix>`, and `<alg>_<suffix>_in_place` for the form the compiler rewrites
/// `xs = xs->sort` to (Builder::inPlaceRebindingOf). The element type is part of the name of the
/// in-place form too, because the executor takes one element type and the URI is what picks it
/// (OperatorTable::inPlaceForm, ExecutorManager::find): a shared in-place name would have to read
/// the element type at run time, which is what these names remove.
std::string sortFormName(const std::string &alg, const char *suffix, bool inPlace) {
    std::string name = alg + "_" + suffix;
    if (inPlace) {
        name += "_in_place";
    }
    return name;
}

} // namespace

std::unordered_map<std::string, operator_t> getArrayOpsMap() {
    std::unordered_map<std::string, operator_t> ops = {
        // The primitives that are not sorts.
        {"flatten", __array_flatten__},
        {"unique", __array_unique__},
    };
    // The sorting family, one operator per element type (kSortTypes): `sort_<t>` returns a new
    // array (design principles 11.3) and `sort_<t>_in_place` is the form the compiler calls for
    // `xs = xs->sort` (OperatorTable::setInPlaceForm); no program names either. The element type
    // is in the name because the executor takes exactly one, which is what the declaration the
    // call compiled through fixed.
    for (const SortType &type : kSortTypes) {
        for (size_t algo = 0; algo < std::size(kSortAlgorithms); ++algo) {
            ops[sortFormName(kSortAlgorithms[algo], type.suffix, false)] = type.forms[algo][0];
            ops[sortFormName(kSortAlgorithms[algo], type.suffix, true)]  = type.forms[algo][1];
        }
        ops[std::string("merge_sorted_arrays_") + type.suffix] = type.mergeSorted;
    }
    // `argsort` takes the keys it can order: the sorts' four element types and the two ordered
    // types the sorts do not order (kArgsortTypes).
    for (const auto &[suffix, exec] : kArgsortTypes) {
        ops[std::string("argsort_") + suffix] = exec;
    }
    // The reductions' kernels (builtin/operators/array_reduce.cpp): one executor per element
    // type, named after the element type it takes. Each body-less declaration in stdlib/array.cml
    // names one of them with its `#op` tag and admits exactly that element type with its own
    // `when` (`Builder::declaredRuleApplies`), so the element type is known where the executor is
    // chosen and no executor asks an array what it holds. An element type no declaration admits
    // has no executor and runs the Camel body (: one operator, per-type executors).
    for (const auto &[name, exec] :
         std::initializer_list<std::pair<const char *, operator_t>>{
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
         }) {
        ops[name] = exec;
    }
    return ops;
}

void declareArrayOperatorFacts() {
    auto &table = camel::core::OperatorTable::instance();
    // Every operator that returns an array returns a new one: none of them changes its argument
    // (design principles 11.3). Only one name per operation is kept (11.3: `sort`, not `sorted`),
    // and there is one name per element type, because one executor takes one element type.
    table.declare(
        "array",
        {
            {"flatten", true},
            {"unique", true},
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
    for (const auto &[suffix, exec] : kArgsortTypes) {
        (void)exec;
        table.declare("array", {{std::string("argsort_") + suffix, true}});
    }
    // The sorts: the returning form of every element type, and the in-place form the compiler
    // introduces for `xs = xs->sort` (design principles 11.3; Builder::inPlaceRebindingOf). The
    // in-place form changes the array it is given, so it is not pure; what it changes is the
    // program's own variable, so it has no effect outside the program. It changes the array and
    // returns it, so it is a read-modify-write of its first operand, an object, whole
    // (docs/design/op-semantics 8), and it reuses its operand — the one fact the temporary-value
    // analysis needs. Sorting never faults now: an element type the sorts have no executor for
    // has no declaration either, and the call is refused where it is written.
    using namespace camel::core::sem;
    for (const SortType &type : kSortTypes) {
        for (const char *algo : kSortAlgorithms) {
            const std::string name = sortFormName(algo, type.suffix, false);
            table.declare("array", {{name, true}});
            const std::string inPlace = sortFormName(algo, type.suffix, true);
            table.set(std::string("array:") + inPlace, {.stateOnly = true, .mayFault = true});
            table.setInPlaceForm(std::string("array:") + name, std::string("array:") + inPlace);
            table.declareReusesOperand(std::string("array:") + inPlace);
            table.declareResources(
                std::string("array:") + inPlace,
                ResourceSpec{
                    .accesses =
                        {operandAccess(ResourceClass::Object, 0, AccessMode::ReadModifyWrite)},
                    .result = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0},
                });
        }
        table.declare("array", {{std::string("merge_sorted_arrays_") + type.suffix, true}});
    }
}