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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


#include "rule.h"

#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <mutex>
#include <ostream>

namespace camel::rewritemod {

using namespace camel::core::type;

RuleType *RuleType::get() {
    static RuleType *instance = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        const TypeCode code = registerOtherType("rewrite::Rule", TypeFlag::GC_Traced);
        void *mem           = mm::permSpace().alloc(sizeof(RuleType), alignof(RuleType));
        ASSERT(mem != nullptr, "Failed to allocate RuleType from permSpace");
        instance = new (mem) RuleType(code);
    });
    return instance;
}

bool RuleType::is(const Type *type) { return type != nullptr && type->code() == get()->code(); }

Type *RuleType::clone(bool) const { return get(); }

bool RuleType::equals(Type *other) const { return is(other); }

CastSafety RuleType::castSafetyFrom(Type *source) const {
    return is(source) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool RuleType::assignableFrom(Type *source) const { return is(source); }

OtherType *RuleType::cloneWithParams(std::span<Type *const>) const { return get(); }

RuleObject *RuleObject::create(
    camel::core::SymId name, bool twoWay, camel::core::sem::PermissionMask needs, uint64_t constParams, ::Function *lhs,
    ::Function *rhs, ::Function *when, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(RuleObject), alignof(RuleObject));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    return new (mem) RuleObject(name, twoWay, needs, constParams, lhs, rhs, when);
}

bool RuleObject::equals(const rtdata::Object *other, const Type *, bool) const {
    if (this == other) {
        return true;
    }
    if (!rtdata::isOfSameCls(this, other)) {
        return false;
    }
    const auto *that = static_cast<const RuleObject *>(other);
    return name_ == that->name_ && twoWay_ == that->twoWay_ && needs_ == that->needs_ &&
           constParams_ == that->constParams_ && lhs_ == that->lhs_ && rhs_ == that->rhs_ &&
           when_ == that->when_;
}

rtdata::Object *RuleObject::clone(mm::IAllocator &allocator, const Type *, bool) const {
    return create(name_, twoWay_, needs_, constParams_, lhs_, rhs_, when_, allocator);
}

void RuleObject::print(std::ostream &os, const Type *) const {
    os << "rule " << name() << (twoWay_ ? " (<=>)" : "");
}

void RuleObject::updateRefs(const rtdata::Object::RefRelocator &relocate, const Type *type) {
    const auto trace = [&](::Function *&field, const char *slot) {
        if (field == nullptr) {
            return;
        }
        auto *object = static_cast<rtdata::Object *>(field);
        object = relocate(
            object,
            nullptr,
            rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = type,
                .slotType  = nullptr,
                .ownerKind = "Rule",
                .slotName  = slot,
            });
        field = static_cast<::Function *>(object);
    };
    trace(lhs_, "lhs");
    trace(rhs_, "rhs");
    trace(when_, "when");
}

} // namespace camel::rewritemod

