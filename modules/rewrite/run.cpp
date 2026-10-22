// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "run.h"

#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <mutex>
#include <ostream>

namespace camel::rewritemod {

using namespace camel::core::type;

RunType *RunType::get() {
    static RunType *instance = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        const TypeCode code = registerOtherType("rewrite::Run", TypeFlag::GC_Traced);
        void *mem = camel::core::mm::permSpace().alloc(sizeof(RunType), alignof(RunType));
        ASSERT(mem != nullptr, "Failed to allocate RunType from permSpace");
        instance = new (mem) RunType(code);
    });
    return instance;
}

bool RunType::is(const Type *type) { return type != nullptr && type->code() == get()->code(); }

Type *RunType::clone(bool) const { return get(); }

bool RunType::equals(Type *other) const { return is(other); }

CastSafety RunType::castSafetyFrom(Type *source) const {
    return is(source) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool RunType::assignableFrom(Type *source) const { return is(source); }

OtherType *RunType::cloneWithParams(std::span<Type *const>) const { return get(); }

RunObject *RunObject::create(
    const camel::runtime::Program *program, const camel::runtime::RewriteStats &stats,
    double seconds, camel::core::mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(RunObject), alignof(RunObject));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    return new (mem) RunObject(program, stats, seconds);
}

bool RunObject::equals(const rtdata::Object *other, const Type *, bool) const {
    return this == other;
}

rtdata::Object *RunObject::clone(camel::core::mm::IAllocator &allocator, const Type *, bool) const {
    return create(program_, stats_, seconds_, allocator);
}

void RunObject::print(std::ostream &os, const Type *) const {
    os << "run: " << stats_.applied << " rewrites in " << stats_.rounds << " rounds, "
       << stats_.visited << " nodes visited";
}

void RunObject::updateRefs(const rtdata::Object::RefRelocator &relocate, const Type *type) {
    if (program_ == nullptr) {
        return;
    }
    auto *object = static_cast<rtdata::Object *>(const_cast<camel::runtime::Program *>(program_));
    object       = relocate(
        object,
        nullptr,
        rtdata::RefTraceInfo{
            .owner     = this,
            .ownerType = type,
            .slotType  = nullptr,
            .ownerKind = "Run",
            .slotName  = "program",
        });
    program_ = static_cast<const camel::runtime::Program *>(object);
}

} // namespace camel::rewritemod