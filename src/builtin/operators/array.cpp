// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The native operators of the `array` module (stdlib/array.cml): the sort, the primitive `argsort`
 * the rest of the module is written on, the layout atoms and reductions along an axis
 * (builtin/operators/array_axis.cpp), and the kernels of the reductions `sum`, `min` and `max`
 * (builtin/operators/array_reduce.cpp). They answer on the `array` protocol; the built-in module
 * registers the executor factory under that name, so the module written in Camel needs no second
 * module of its own to load them.
 *
 * Each is here because composing it from the built-in operators would cost more than its size: a
 * sort written with recursion would recurse once per element, and a reduction written with
 * `reduce` calls the combining function once per element through the interpreter instead of
 * sweeping the array (design principles 10.4). The sort has an in-place form: the compiler
 * rewrites `xs = xs->sort` to it when nothing else refers to the array (design principles 11.3),
 * so the form changes its operand and its result is that operand.
 */

#include "array.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/rtdata/half.h"
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
        // -0 is below +0, so that the order of two zeros does not depend on where they stand
        // (design principles 7.7: the order `min` and `max` use).
        if (a == b)
            return std::signbit(a) && !std::signbit(b);
    }
    return a < b;
}

// ---- sorting ----

// (keys: number[] | bool[] | string[]) => int[]: the positions of the keys in ascending order of
// key. Equal keys keep their order (a stable sort), so sorting by one key and then by another
// works. One executor per key element type: which one runs is fixed by the declaration the call
// compiled through (`#op('array:argsort_i')` and its siblings, stdlib/array.cml), so nothing here
// asks the array which element type it holds and an element type with no declaration is refused
// where the call is written rather than here.

/// The order `argsort` puts two keys in: a NaN comes after every number, a string is compared by
/// its bytes (as the comparison operators order strings), and a bool is `false` below `true`.
template <typename Key> bool keyLess(Array *keys, Int64 a, Int64 b) {
    if constexpr (std::is_same_v<Key, String *>) {
        return keys->get<String *>(a)->compare(keys->get<String *>(b)) < 0;
    } else if constexpr (is_reduced_float_v<Key>) {
        // A bf16 or an f16 is ordered as the f32 it widens to, exactly.
        return numberLess(
            keys->dataAs<Key>()[a].toFloat(), keys->dataAs<Key>()[b].toFloat());
    } else {
        return numberLess(keys->get<Key>(a), keys->get<Key>(b));
    }
}

// The key a number is ordered by, held beside its position so that the sort reads memory in
// order: an index vector sorted through the keys' array reads a random element at every
// comparison, which for a long array of unordered keys costs ten times the sort of the keys.
template <typename Key> struct OrderKey {
    using type = Key;
    static type of(Array *keys, size_t i) { return keys->dataAs<Key>()[i]; }
};
template <> struct OrderKey<BFloat16> {
    using type = float;
    static type of(Array *keys, size_t i) { return keys->dataAs<BFloat16>()[i].toFloat(); }
};
template <> struct OrderKey<Float16> {
    using type = float;
    static type of(Array *keys, size_t i) { return keys->dataAs<Float16>()[i].toFloat(); }
};

template <typename Key> slot_t argsortOf(ArgsView &norm) {
    Array *keys    = norm.get<Array *>(0);
    const size_t n = keys->size();
    Array *result  = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), n);
    if constexpr (std::is_same_v<Key, String *>) {
        std::vector<Int64> order(n);
        std::iota(order.begin(), order.end(), Int64(0));
        std::stable_sort(order.begin(), order.end(), [&](Int64 a, Int64 b) {
            return keyLess<Key>(keys, a, b);
        });
        for (size_t i = 0; i < n; ++i)
            result->set<Int64>(i, order[i]);
    } else {
        using K = typename OrderKey<Key>::type;
        struct Entry {
            K key;
            Int64 position;
        };
        std::vector<Entry> entries(n);
        for (size_t i = 0; i < n; ++i)
            entries[i] = {OrderKey<Key>::of(keys, i), static_cast<Int64>(i)};
        // A total order: equal keys (every NaN among them) keep their positions' order, which is
        // what the stable sort of the positions gives.
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            if (numberLess(a.key, b.key))
                return true;
            if (numberLess(b.key, a.key))
                return false;
            return a.position < b.position;
        });
        for (size_t i = 0; i < n; ++i)
            result->set<Int64>(i, entries[i].position);
    }
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
ARRAY_ARGSORT_IMPL(bf, BFloat16)
ARRAY_ARGSORT_IMPL(f6, Float16)

#undef ARRAY_ARGSORT_IMPL

// ---- the sort ----

// Sorts `arr` and returns it: a copy of it, as the array a program passes is never changed
// (design principles 11.3), or, for the in-place form (`inPlace`), the array itself, which the
// compiler hands over only when no one else refers to it (design principles 11.3;
// Builder::inPlaceRebindingOf). The elements are numbers: the slots hold no references, so
// reordering them needs no write barrier. The order is the one `argsort` gives, ties in their
// original order, so `sort(xs)` and `xs[argsort(xs)]` are the same bits (the Camel body of `sort`
// in stdlib/array.cml is the second).
//
// The element type is a template parameter, because it is fixed where the executor is chosen: the
// declaration a call compiled through names its own element type (`#op('array:sort_i')`, ...).
// Nothing here reads the element type code, so there is no run-time switch on it and no default
// arm that could guess: an element type no declaration names never reaches an executor at all.
template <typename T>
Array *sortedSlots(Array *arr, const ArrayType *arrType, bool inPlace) {
    const size_t n = arr->size();
    // The in-place form is only taken when the storage is this value's alone; a view or an
    // imported (external) buffer means someone else sees the old order. Where the memory plan
    // could not prove the old value has no other observer, this is the answer of the array
    // itself (docs/design/arrays-memory/README.cn.md 4.3.1).
    const bool ownStorage = inPlace && arr->writableInPlace();
    Array *target = ownStorage ? arr : static_cast<Array *>(arr->clone(mm::autoSpace(), arrType));
    if (n <= 1) {
        return target;
    }
    T *vals = target->dataAs<T>();
    std::stable_sort(vals, vals + n, [](T a, T b) { return numberLess(a, b); });
    return target;
}

template <typename T> slot_t sortSlotsOf(ArgsView &norm, bool inPlace) {
    Array *arr          = norm.get<Array *>(0);
    const auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    return toSlot(sortedSlots<T>(arr, arrType, inPlace));
}

// One executor per element type: the returning form, and the in-place form the compiler calls for
// `xs = xs->sort` (OperatorTable::setInPlaceForm; no program names either).
#define ARRAY_SORT_EXECUTOR(suffix, T)                                                 \
    slot_t __array_sort_##suffix##__(ArgsView &, ArgsView &norm, Context &) {          \
        return sortSlotsOf<T>(norm, false);                                            \
    }                                                                                  \
    slot_t __array_sort_##suffix##_in_place__(ArgsView &, ArgsView &norm, Context &) { \
        return sortSlotsOf<T>(norm, true);                                             \
    }

ARRAY_SORT_EXECUTOR(i, Int32)
ARRAY_SORT_EXECUTOR(l, Int64)
ARRAY_SORT_EXECUTOR(f, Float32)
ARRAY_SORT_EXECUTOR(d, Float64)

#undef ARRAY_SORT_EXECUTOR

// ---- the merge ----

// `merge_sorted(xs, ys)` is `sort(concat(xs, ys))`; the kernel gives the same array. When both
// inputs are in order (the case the function exists for) one pass merges them: the element of
// `ys` goes out only when it is strictly below the element of `xs`, so equal elements keep their
// order, `xs` before `ys`, as the stable sort of the concatenation keeps them. An input that is
// not in order is sorted with the concatenation, so the result is the body's for every input. The
// order is `numberLess` throughout: every NaN after the numbers, -0 below +0.
template <typename T> bool inOrder(const T *values, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        if (numberLess(values[i], values[i - 1])) {
            return false;
        }
    }
    return true;
}

template <typename T> slot_t mergeSortedOf(ArgsView &norm) {
    Array *xs           = norm.get<Array *>(0);
    Array *ys           = norm.get<Array *>(1);
    const auto *arrType = tt::as_ptr<ArrayType>(norm.type(0));
    const size_t n      = xs->size();
    const size_t m      = ys->size();
    Array *result       = Array::create(mm::autoSpace(), arrType, n + m);
    const T *x          = n > 0 ? xs->dataAs<T>() : nullptr;
    const T *y          = m > 0 ? ys->dataAs<T>() : nullptr;
    T *out              = n + m > 0 ? result->dataAs<T>() : nullptr;
    if (inOrder(x, n) && inOrder(y, m)) {
        size_t i = 0, j = 0, k = 0;
        while (i < n && j < m) {
            out[k++] = numberLess(y[j], x[i]) ? y[j++] : x[i++];
        }
        while (i < n) {
            out[k++] = x[i++];
        }
        while (j < m) {
            out[k++] = y[j++];
        }
    } else {
        std::copy(x, x + n, out);
        std::copy(y, y + m, out + n);
        std::stable_sort(out, out + n + m, [](T a, T b) { return numberLess(a, b); });
    }
    return toSlot(result);
}

#define ARRAY_MERGE_EXECUTOR(suffix, T)                                         \
    slot_t __array_merge_##suffix##__(ArgsView &, ArgsView &norm, Context &) {    \
        return mergeSortedOf<T>(norm);                                          \
    }

ARRAY_MERGE_EXECUTOR(i, Int32)
ARRAY_MERGE_EXECUTOR(l, Int64)
ARRAY_MERGE_EXECUTOR(f, Float32)
ARRAY_MERGE_EXECUTOR(d, Float64)

#undef ARRAY_MERGE_EXECUTOR

// ---- the sort's element types, one entry each ----

/// One element type of the sort: the suffix its operator names carry (the reductions'
/// convention -- `_i` i32, `_l` int, `_f` f32, `_d` float) and the two executors, the returning
/// form first, the in-place form beside it.
struct SortType {
    const char *suffix;
    operator_t forms[2];
};

const SortType kSortTypes[] = {
    {"i", {__array_sort_i__, __array_sort_i_in_place__}},
    {"l", {__array_sort_l__, __array_sort_l_in_place__}},
    {"f", {__array_sort_f__, __array_sort_f_in_place__}},
    {"d", {__array_sort_d__, __array_sort_d_in_place__}},
};

/// The element types `argsort` takes keys of: the four above, and the two ordered types the sorts
/// do not order (`_b` bool, `_s` string).
const std::pair<const char *, operator_t> kArgsortTypes[] = {
    {"i", __array_argsort_i__},
    {"l", __array_argsort_l__},
    {"f", __array_argsort_f__},
    {"d", __array_argsort_d__},
    {"bf", __array_argsort_bf__},
    {"f6", __array_argsort_f6__},
    {"b", __array_argsort_b__},
    {"s", __array_argsort_s__},
};

/// The name of one form of the sort for one element type, without the module prefix:
/// `sort_<suffix>`, and `sort_<suffix>_in_place` for the form the compiler rewrites
/// `xs = xs->sort` to (Builder::inPlaceRebindingOf). The element type is part of the name of the
/// in-place form too, because the executor takes one element type and the URI is what picks it
/// (OperatorTable::inPlaceForm, ExecutorManager::find): a shared in-place name would have to read
/// the element type at run time, which is what these names remove.
std::string sortFormName(const char *suffix, bool inPlace) {
    std::string name = std::string("sort_") + suffix;
    if (inPlace) {
        name += "_in_place";
    }
    return name;
}

} // namespace

std::unordered_map<std::string, operator_t> getArrayOpsMap() {
    std::unordered_map<std::string, operator_t> ops = {
        // The layout atoms and the reductions along an axis (array_axis.cpp).
        {"shape", __array_shape__},
        {"numel", __array_numel__},
        {"dim", __array_dim__},
        {"elements", __array_elements__},
        {"to_bytes", __array_to_bytes__},
        {"sum_axis", __array_sum_axis__},
        {"mean_axis", __array_mean_axis__},
        {"min_axis", __array_min_axis__},
        {"max_axis", __array_max_axis__},
        {"argmax_axis", __array_argmax_axis__},
        {"argmin_axis", __array_argmin_axis__},
    };
    // The sorting family, one operator per element type (kSortTypes): `sort_<t>` returns a new
    // array (design principles 11.3) and `sort_<t>_in_place` is the form the compiler calls for
    // `xs = xs->sort` (OperatorTable::setInPlaceForm); no program names either. The element type
    // is in the name because the executor takes exactly one, which is what the declaration the
    // call compiled through fixed.
    for (const SortType &type : kSortTypes) {
        ops[sortFormName(type.suffix, false)] = type.forms[0];
        ops[sortFormName(type.suffix, true)]  = type.forms[1];
    }
    // `argsort` takes the keys it can order: the sorts' four element types and the two ordered
    // types the sorts do not order (kArgsortTypes).
    for (const auto &[suffix, exec] : kArgsortTypes) {
        ops[std::string("argsort_") + suffix] = exec;
    }
    // `merge_sorted`'s kernels, one per element type like the sort's.
    ops["merge_sorted_i"] = __array_merge_i__;
    ops["merge_sorted_l"] = __array_merge_l__;
    ops["merge_sorted_f"] = __array_merge_f__;
    ops["merge_sorted_d"] = __array_merge_d__;
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
             {"min_i", __array_min_i__},
             {"min_l", __array_min_l__},
             {"min_f", __array_min_f__},
             {"min_d", __array_min_d__},
             {"max_i", __array_max_i__},
             {"max_l", __array_max_l__},
             {"max_f", __array_max_f__},
             {"max_d", __array_max_d__},
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
            {"shape", true},
            {"numel", true},
            {"dim", true},
            {"elements", true},
            {"to_bytes", true},
            {"sum_axis", true},
            {"mean_axis", true},
            {"min_axis", true},
            {"max_axis", true},
            {"argmax_axis", true},
            {"argmin_axis", true},
            // The reductions' kernels are pure as the functions they execute are: the fold reads
            // the array it is given and touches nothing else. The declarations in the map above
            // name them; this is what tells the compiler what a call of one may touch. One entry
            // per element type, because one executor takes one element type.
            {"sum_i", true},
            {"sum_l", true},
            {"sum_f", true},
            {"sum_d", true},
            {"min_i", true},
            {"min_l", true},
            {"min_f", true},
            {"min_d", true},
            {"max_i", true},
            {"max_l", true},
            {"max_f", true},
            {"max_d", true},
        });
    for (const auto &[suffix, exec] : kArgsortTypes) {
        (void)exec;
        table.declare("array", {{std::string("argsort_") + suffix, true}});
    }
    table.declare(
        "array",
        {{"merge_sorted_i", true},
         {"merge_sorted_l", true},
         {"merge_sorted_f", true},
         {"merge_sorted_d", true}});
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
        const std::string name = sortFormName(type.suffix, false);
        table.declare("array", {{name, true}});
        const std::string inPlace = sortFormName(type.suffix, true);
        table.set(std::string("array:") + inPlace, {.stateOnly = true, .mayFault = true});
        table.setInPlaceForm(std::string("array:") + name, std::string("array:") + inPlace);
        table.declareReusesOperand(std::string("array:") + inPlace);
        table.declareResources(
            std::string("array:") + inPlace,
            ResourceSpec{
                .accesses = {operandAccess(ResourceClass::Object, 0, AccessMode::ReadModifyWrite)},
                .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0},
            });
    }
}
