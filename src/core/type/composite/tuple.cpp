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
 * Created: Oct. 06, 2024
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "tuple.h"
#include "core/mm/alloc/allocator.h"
#include "core/mm/mm.h"
#include "error/diagnostics/diagnostics.h"
#include "utils/assert.h"
#include "utils/type.h"

using namespace std;

namespace {

TupleTypeLayout computeLayout(size_t size, size_t refCount) {
    size_t typesSize     = size * sizeof(Type *);
    size_t typeCodesSize = size * sizeof(TypeCode);
    size_t refsSize      = refCount * sizeof(size_t);
    size_t aTypes        = ::alignUp(typesSize, alignof(TypeCode));
    size_t aTypeCodes    = ::alignUp(typeCodesSize, alignof(size_t));
    size_t totalSize     = sizeof(TupleType) + aTypes + aTypeCodes + refsSize;
    return TupleTypeLayout{
        .totalSize            = totalSize,
        .size                 = size,
        .refCount             = refCount,
        .alignedTypesSize     = aTypes,
        .alignedTypeCodesSize = aTypeCodes,
    };
}

} // namespace

TupleType *TupleTypeFactory::build() { return TupleType::fromFactory(*this); }

TupleType::TupleType(
    const TupleTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
    const size_t *refs)
    : CompositeType(TypeCode::Tuple), size_(layout.size), refCount_(layout.refCount) {
    auto p        = layout.ptrs(data_);
    typesPtr_     = p.types;
    typeCodesPtr_ = p.typeCodes;
    refsPtr_      = p.refs;
    for (size_t i = 0; i < size_; ++i) {
        p.types[i]     = const_cast<Type *>(types[i]);
        p.typeCodes[i] = typeCodes[i];
    }
    for (size_t i = 0; i < refCount_; ++i) {
        p.refs[i] = refs[i];
    }
}

TupleType::TupleType(TupleTypeFactory &factory, const TupleTypeLayout &layout)
    : CompositeType(TypeCode::Tuple), size_(layout.size), refCount_(layout.refCount) {
    auto p        = layout.ptrs(data_);
    typesPtr_     = p.types;
    typeCodesPtr_ = p.typeCodes;
    refsPtr_      = p.refs;
    size_t refIdx = 0;
    for (size_t i = 0; i < size_; ++i) {
        Type *t        = factory.types_[i];
        p.types[i]     = t;
        p.typeCodes[i] = t->code();
        if (t->code() == TypeCode::Ref) {
            p.refs[refIdx++] = i;
        }
    }
}

TupleType::TupleType(const TupleTypeLayout &layout, const std::vector<Type *> &types)
    : CompositeType(TypeCode::Tuple), size_(layout.size), refCount_(layout.refCount) {
    auto p        = layout.ptrs(data_);
    typesPtr_     = p.types;
    typeCodesPtr_ = p.typeCodes;
    refsPtr_      = p.refs;
    size_t refIdx = 0;
    for (size_t i = 0; i < size_; ++i) {
        Type *t        = types[i];
        p.types[i]     = t;
        p.typeCodes[i] = t->code();
        if (t->code() == TypeCode::Ref) {
            p.refs[refIdx++] = i;
        }
    }
}

TupleType *TupleType::create() {
    TupleTypeLayout layout = computeLayout(0, 0);
    void *mem              = mm::permSpace().alloc(layout.totalSize, alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType(layout, nullptr, nullptr, nullptr);
}

TupleType *TupleType::create(const std::vector<Type *> &types) {
    size_t refCount = 0;
    for (Type *t : types) {
        if (t && t->code() == TypeCode::Ref)
            ++refCount;
    }
    TupleTypeLayout layout = computeLayout(types.size(), refCount);
    void *mem              = mm::permSpace().alloc(layout.totalSize, alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType(layout, types);
}

TupleType *TupleType::create(std::vector<Type *> &&types) { return create(types); }

TupleType *TupleType::fromFactory(TupleTypeFactory &factory) {
    size_t size     = factory.types_.size();
    size_t refCount = 0;
    for (Type *t : factory.types_) {
        if (t && t->code() == TypeCode::Ref)
            ++refCount;
    }
    TupleTypeLayout layout = computeLayout(size, refCount);
    void *mem              = mm::permSpace().alloc(layout.totalSize, alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType(factory, layout);
}

TupleType *TupleType::fromFactoryData(
    const Type *const *types, const TypeCode *typeCodes, const size_t *refs, size_t size,
    size_t refCount) {
    TupleTypeLayout layout = computeLayout(size, refCount);
    void *mem              = mm::permSpace().alloc(layout.totalSize, alignof(TupleType));
    ASSERT(mem != nullptr, "Failed to allocate TupleType from permSpace");
    return new (mem) TupleType(layout, types, typeCodes, refs);
}

TupleType *TupleType::slice(size_t start, size_t end) const {
    ASSERT(start <= end && end <= size_, "TupleType slice indices out of range");
    std::vector<Type *> slicedTypes;
    slicedTypes.reserve(end - start);
    for (size_t i = start; i < end; ++i) {
        slicedTypes.push_back(typesPtr_[i]);
    }
    return TupleType::create(std::move(slicedTypes));
}

Type *TupleType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "TupleType is already resolved");
    std::vector<Type *> types;
    types.reserve(size_);
    for (size_t i = 0; i < size_; ++i) {
        types.push_back(typesPtr_[i]);
    }
    size_t i = 0, j = 0;
    while (i < types.size() && j < typeList.size()) {
        if (types[i]->code() == TypeCode::Ref) {
            types[i] = typeList[j];
            j++;
        }
        i++;
    }
    ASSERT(j == typeList.size(), "Not all types in typeList are used");
    auto newTuple = TupleType::create(std::move(types));
    ASSERT(newTuple->resolved(), "TupleType is not fully resolved");
    return newTuple;
}

bool TupleType::resolved() const {
    for (size_t i = 0; i < size_; ++i) {
        if (typesPtr_[i]->code() == TypeCode::Ref) {
            return false;
        }
    }
    return true;
}

string TupleType::toString() const {
    string result = "(";
    for (size_t i = 0; i < size_; ++i) {
        Type *type = typesPtr_[i];
        if (type) {
            result += type->toString() + ", ";
        } else {
            result += "null, ";
        }
    }
    if (size_ > 0) {
        result.pop_back();
        result.pop_back();
    }
    result += ")";
    return result;
}

std::string TupleType::mangle() const {
    std::string result = "P";
    result += std::to_string(size_);
    for (size_t i = 0; i < size_; ++i) {
        result += typesPtr_[i]->mangle();
    }
    return result;
}

Type *TupleType::clone(bool deep /* = false */) const {
    std::vector<Type *> clonedTypes;
    clonedTypes.reserve(size_);
    if (deep) {
        for (size_t i = 0; i < size_; ++i) {
            clonedTypes.push_back(typesPtr_[i]->clone(true));
        }
    } else {
        for (size_t i = 0; i < size_; ++i) {
            clonedTypes.push_back(typesPtr_[i]);
        }
    }
    return TupleType::create(std::move(clonedTypes));
}

bool TupleType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    if (!other || other->code() != TypeCode::Tuple) {
        return false;
    }
    const TupleType &otherTuple = static_cast<const TupleType &>(*other);
    if (size_ != otherTuple.size_) {
        return false;
    }
    for (size_t i = 0; i < size_; i++) {
        if (!typesPtr_[i]->equals(otherTuple.typesPtr_[i])) {
            return false;
        }
    }
    return true;
}

CastSafety TupleType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool TupleType::assignable(Type *type) const { return equals(type); }
