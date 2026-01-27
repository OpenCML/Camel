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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "array.h"
#include "core/mm/mm.h"
#include "error/diagnostics/diagnostics.h"
#include "utils/assert.h"

using namespace std;

void ArrayType::computeLayout() const {
    if (!layout_) {
        layout_ = std::make_shared<ArrayTypeLayout>(elemType_->code(), refs_);
    }
}

ArrayType::ArrayType(Type *elementType) : CompositeType(TypeCode::Array), elemType_(elementType) {}

ArrayType *ArrayType::create(Type *elemType) {
    if (!elemType) {
        elemType = Type::Void();
    }
    void *mem = mm::permSpace().alloc(sizeof(ArrayType), alignof(ArrayType));
    ASSERT(mem != nullptr, "Failed to allocate ArrayType from permSpace");
    return new (mem) ArrayType(elemType);
}

void ArrayType::addRef(size_t index) { refs_.push_back(index); }

void ArrayType::setRefs(const std::vector<size_t> &refs) { refs_ = refs; }

Type *ArrayType::elemType() const { return elemType_; }

const ArrayTypeLayout &ArrayType::layout() const {
    if (!layout_) {
        computeLayout();
    }
    return *layout_;
}

Type *ArrayType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() > 0, "Type list is empty");
    ASSERT(!resolved(), "ArrayType is already resolved");

    ASSERT(
        typeList.size() == refs_.size(),
        "Type list size does not match the number of references in ArrayType");

    Type *newElemType = elemType_;
    for (const auto &type : typeList) {
        if (newElemType->code() == TypeCode::Void) {
            newElemType = type;
        } else if (!newElemType->equals(type)) {
            throw DiagnosticBuilder::of(SemanticDiag::ElementTypeMismatch)
                .commit("Array", type->toString(), newElemType->toString());
        }
    }
    auto newArray = ArrayType::create(newElemType);
    ASSERT(newArray->resolved(), "ArrayType is not fully resolved");
    return newArray;
}

bool ArrayType::resolved() const { return refs_.empty(); }

string ArrayType::toString() const { return elemType_->toString() + "[]"; }

std::string ArrayType::mangle() const {
    std::string result = "A";
    result += elemType_->mangle();
    return result;
}

Type *ArrayType::clone(bool deep /* = false */) const {
    auto newType     = ArrayType::create(deep ? elemType_->clone(true) : elemType_);
    newType->refs_   = refs_;
    newType->layout_ = layout_;
    return newType;
}

bool ArrayType::equals(Type *other) const {
    if (this == other) {
        return true;
    }
    if (!other || other->code() != TypeCode::Array) {
        return false;
    }
    const ArrayType &otherArr = static_cast<const ArrayType &>(*other);
    return elemType_->equals(otherArr.elemType_);
}

CastSafety ArrayType::castSafetyTo(const Type &other) const {
    if (this == &other) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

bool ArrayType::assignable(Type *type) const { return equals(type); }
