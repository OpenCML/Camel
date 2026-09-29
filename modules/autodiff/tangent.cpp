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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tangent types (see tangent.h).
 */

#include "tangent.h"

#include "camel/core/derivative.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/type.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::autodiff {

using type::ArrayType;
using type::StructType;
using type::StructTypeFactory;
using type::TupleType;
using type::Type;
using type::TypeCode;

namespace {

Type *computeTangentType(Type *primal) {
    switch (primal->code()) {
    case TypeCode::Struct: {
        auto *st = tt::as_ptr<StructType>(primal);
        StructTypeFactory factory;
        bool any = false;
        for (size_t i = 0; i < st->size(); ++i) {
            if (Type *elem = tangentTypeOf(st->typeAt(i))) {
                factory.add(std::string(st->fieldName(i)), elem);
                any = true;
            }
        }
        return any ? factory.build() : nullptr;
    }
    case TypeCode::Tuple: {
        auto *tuple = tt::as_ptr<TupleType>(primal);
        std::vector<Type *> elems;
        for (Type *elem : tuple->types()) {
            if (Type *tangent = tangentTypeOf(elem)) {
                elems.push_back(tangent);
            }
        }
        return elems.empty() ? nullptr : TupleType::create(std::move(elems));
    }
    case TypeCode::Array: {
        Type *elem    = tt::as_ptr<ArrayType>(primal)->elemType();
        Type *tangent = elem ? tangentTypeOf(elem) : nullptr;
        return tangent ? ArrayType::create(tangent) : nullptr;
    }
    default:
        break;
    }
    const auto *space = camel::core::DerivativeRegistry::instance().findTangentSpace(primal);
    if (space == nullptr) {
        return nullptr;
    }
    return space->tangentType ? space->tangentType(primal) : primal;
}

} // namespace

Type *tangentTypeOf(Type *primal) {
    if (primal == nullptr) {
        return nullptr;
    }
    // Types are immutable and long-lived, so the mapping is cached by identity.
    static std::mutex mutex;
    static std::unordered_map<Type *, Type *> cache;
    {
        std::lock_guard lock(mutex);
        if (auto it = cache.find(primal); it != cache.end()) {
            return it->second;
        }
    }
    Type *tangent = computeTangentType(primal);
    std::lock_guard lock(mutex);
    cache.emplace(primal, tangent);
    return tangent;
}

bool isAggregate(const Type *t) {
    return t != nullptr && (t->code() == TypeCode::Struct || t->code() == TypeCode::Tuple);
}

size_t aggregateSize(Type *aggregate) {
    return aggregate->code() == TypeCode::Struct ? tt::as_ptr<StructType>(aggregate)->size()
                                                 : tt::as_ptr<TupleType>(aggregate)->size();
}

Type *aggregateElement(Type *aggregate, size_t index) {
    return aggregate->code() == TypeCode::Struct ? tt::as_ptr<StructType>(aggregate)->typeAt(index)
                                                 : tt::as_ptr<TupleType>(aggregate)->typeAt(index);
}

std::optional<size_t> tangentElementIndex(Type *aggregate, size_t index) {
    if (tangentTypeOf(aggregateElement(aggregate, index)) == nullptr) {
        return std::nullopt;
    }
    size_t position = 0;
    for (size_t i = 0; i < index; ++i) {
        if (tangentTypeOf(aggregateElement(aggregate, i)) != nullptr) {
            ++position;
        }
    }
    return position;
}

} // namespace camel::autodiff
