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
 * Author: Camel Contributors
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Array primitives that the library written in Camel (stdlib/array.cml) builds on. Each is
 * here because composing it from the built-in operators would cost more than its size: a
 * `flatten` folded with `concat` copies the result once per inner array, a `unique` folded
 * with `contains` and `concat` is quadratic and copies as well, and a sort written with
 * recursion would recurse once per element. All three are pure and return a new array.
 */

#include "sequence.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

namespace {

// (xss: T[][]) => T[]: the elements of the inner arrays, in order.
slot_t __algo_flatten__(ArgsView &, ArgsView &norm, Context &) {
    Array *outer         = norm.get<Array *>(0);
    const auto *innerType =
        tt::as_ptr<ArrayType>(tt::as_ptr<ArrayType>(norm.type(0))->elemType());
    size_t total = 0;
    for (size_t i = 0; i < outer->size(); ++i)
        total += outer->get<Array *>(i)->size();
    Array *result = Array::create(mm::autoSpace(), total);
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
slot_t __algo_unique__(ArgsView &, ArgsView &norm, Context &) {
    Array *arr             = norm.get<Array *>(0);
    const auto *arrType    = tt::as_ptr<ArrayType>(norm.type(0));
    const TypeCode code    = arrType->elemTypeCode();
    Type *elemType         = arrType->elemType();
    const size_t n         = arr->size();
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
    Array *result = Array::create(mm::autoSpace(), kept.size());
    arr           = norm.get<Array *>(0);
    for (size_t k = 0; k < kept.size(); ++k)
        result->set<slot_t>(k, arr->get<slot_t>(kept[k]), arrType);
    return toSlot(result);
}

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

// (keys: K[]) => int[]: the positions of the keys in ascending order of key. Equal keys keep
// their order (a stable sort), so sorting by one key and then by another works.
slot_t __algo_argsort__(ArgsView &, ArgsView &norm, Context &) {
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
            "algo.argsort",
            typeCodeToString(code) + "[]",
            "an array of numbers, strings or bools");
    }
    Array *result = Array::create(mm::autoSpace(), n);
    for (size_t i = 0; i < n; ++i)
        result->set<Int64>(i, order[i]);
    return toSlot(result);
}

} // namespace

const std::vector<oper_group_ptr_t> &getAlgoSequenceGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "flatten",
            {{"algo:flatten",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(xss: T[][]) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      Type *inner = tt::as_ptr<ArrayType>(norm[0])->elemType();
                      if (inner == nullptr || inner->code() != TypeCode::Array)
                          return std::nullopt;
                      return ArrayType::create(tt::as_ptr<ArrayType>(inner)->elemType());
                  })}}),
        OperatorGroup::create(
            "unique",
            {{"algo:unique",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(xs: T[]) => T[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      return ArrayType::create(tt::as_ptr<ArrayType>(norm[0])->elemType());
                  })}}),
        OperatorGroup::create(
            "argsort",
            {{"algo:argsort",
              DynamicFuncTypeResolver::create(
                  {{0, {}}, {1, {false}}},
                  "(keys: K[]) => int[]",
                  [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                      -> std::optional<Type *> {
                      if (norm[0]->code() != TypeCode::Array)
                          return std::nullopt;
                      Type *elem = tt::as_ptr<ArrayType>(norm[0])->elemType();
                      if (elem == nullptr)
                          return std::nullopt;
                      switch (elem->code()) {
                      case TypeCode::Int32:
                      case TypeCode::Int64:
                      case TypeCode::Float32:
                      case TypeCode::Float64:
                      case TypeCode::Bool:
                      case TypeCode::String:
                          return ArrayType::create(Type::Int64());
                      default:
                          return std::nullopt;
                      }
                  })}}),
    };
    return groups;
}

std::unordered_map<std::string, operator_t> getAlgoSequenceOpsMap() {
    return {
        {"flatten", __algo_flatten__},
        {"unique", __algo_unique__},
        {"argsort", __algo_argsort__},
    };
}
