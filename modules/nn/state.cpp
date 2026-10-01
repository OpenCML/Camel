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
 * Optimizer state (see state.h).
 */

#include "state.h"

#include "camel/core/mm.h"

#include <ostream>

namespace camel::nn {

using namespace camel::core::type;

OptimizerStateType::OptimizerStateType() : OtherType(typeCode()) {}

TypeCode OptimizerStateType::typeCode() {
    static TypeCode code = registerOtherType("OptimizerState", TypeFlag::GC_Traced);
    return code;
}

Type *OptimizerStateType::Default() {
    static OptimizerStateType *type = [] {
        void *mem = mm::permSpace().alloc(sizeof(OptimizerStateType), alignof(OptimizerStateType));
        ASSERT(mem != nullptr, "Failed to allocate OptimizerStateType from permSpace");
        return new (mem) OptimizerStateType();
    }();
    return type;
}

std::string OptimizerStateType::toString() const { return "OptimizerState"; }

std::string OptimizerStateType::mangle() const { return "O"; }

Type *OptimizerStateType::clone(bool deep) const {
    (void)deep;
    return Default();
}

bool OptimizerStateType::equals(Type *type) const {
    return type != nullptr && type->code() == typeCode();
}

CastSafety OptimizerStateType::castSafetyFrom(Type *sourceType) const {
    if (auto r = Type::checkCastSafetyWithAny(code(), sourceType)) {
        return *r;
    }
    return assignableFrom(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool OptimizerStateType::assignableFrom(Type *sourceType) const {
    return sourceType != nullptr && sourceType->code() == typeCode();
}

OtherType *OptimizerStateType::cloneWithParams(std::span<Type *const> params) const {
    (void)params;
    return static_cast<OtherType *>(Default());
}

OptimizerStateObject *OptimizerStateObject::create(
    ::Tuple *trees, TupleType *treesType, int64_t step, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(OptimizerStateObject), alignof(OptimizerStateObject));
    if (!mem) {
        throw std::bad_alloc();
    }
    auto *state = new (mem) OptimizerStateObject(trees, treesType, step);
    mm::writeBarrier(state, nullptr, trees, treesType);
    return state;
}

bool OptimizerStateObject::equals(const rtdata::Object *other, const Type *type, bool deep) const {
    (void)type;
    auto *rhs = dynamic_cast<const OptimizerStateObject *>(other);
    if (rhs == nullptr) {
        return false;
    }
    return this == rhs || (step_ == rhs->step_ && trees_->equals(rhs->trees_, treesType_, deep));
}

rtdata::Object *
OptimizerStateObject::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    (void)type;
    auto *trees = static_cast<::Tuple *>(trees_->clone(allocator, treesType_, deep));
    return create(trees, treesType_, step_, allocator);
}

void OptimizerStateObject::print(std::ostream &os, const Type *type) const {
    (void)type;
    os << "OptimizerState(step=" << step_ << ")";
}

void OptimizerStateObject::updateRefs(
    const rtdata::Object::RefRelocator &relocate, const Type *type) {
    if (trees_ == nullptr) {
        return;
    }
    trees_ = static_cast<::Tuple *>(relocate(
        trees_,
        treesType_,
        rtdata::RefTraceInfo{
            .owner     = this,
            .ownerType = type,
            .slotType  = treesType_,
            .ownerKind = "OptimizerStateObject",
            .slotName  = "trees",
            .slotIndex = rtdata::RefTraceInfo::npos,
        }));
}

} // namespace camel::nn
