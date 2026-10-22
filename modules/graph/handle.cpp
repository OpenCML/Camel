// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "handle.h"

#include "camel/core/mm.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/program.h"
#include "camel/utils/assert.h"

#include <mutex>
#include <ostream>

namespace camel::graphmod {

using namespace camel::core::type;
using camel::runtime::GraphId;
using camel::runtime::Program;

const char *handleKindName(HandleKind kind) {
    switch (kind) {
    case HandleKind::Program:
        return "Program";
    case HandleKind::Graph:
        return "Graph";
    case HandleKind::Node:
        return "Node";
    case HandleKind::Region:
        return "Region";
    case HandleKind::TypeRef:
        return "TypeRef";
    case HandleKind::Op:
        return "Op";
    case HandleKind::Tag:
        return "Tag";
    case HandleKind::Txn:
        return "Txn";
    }
    return "?";
}

// ---- the types ----

HandleType *HandleType::get(HandleKind kind) {
    constexpr size_t kKinds          = static_cast<size_t>(HandleKind::Txn) + 1;
    static HandleType *types[kKinds] = {};
    static std::once_flag once;
    std::call_once(once, [] {
        for (size_t i = 0; i < kKinds; ++i) {
            const auto k = static_cast<HandleKind>(i);
            const TypeCode code =
                registerOtherType(std::string("graph::") + handleKindName(k), TypeFlag::GC_Traced);
            void *mem = mm::permSpace().alloc(sizeof(HandleType), alignof(HandleType));
            ASSERT(mem != nullptr, "Failed to allocate HandleType from permSpace");
            types[i] = new (mem) HandleType(code, k);
        }
    });
    return types[static_cast<size_t>(kind)];
}

bool HandleType::is(const Type *type, HandleKind kind) {
    return type != nullptr && type->code() == get(kind)->code();
}

std::string HandleType::toString() const { return handleKindName(kind_); }

std::string HandleType::mangle() const { return std::string("graph.") + handleKindName(kind_); }

Type *HandleType::clone(bool deep) const {
    (void)deep;
    return get(kind_);
}

bool HandleType::equals(Type *other) const { return is(other, kind_); }

CastSafety HandleType::castSafetyFrom(Type *source) const {
    return is(source, kind_) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool HandleType::assignableFrom(Type *source) const { return is(source, kind_); }

OtherType *HandleType::cloneWithParams(std::span<Type *const> params) const {
    (void)params;
    return get(kind_);
}

// ---- the object ----

Handle *Handle::create(
    HandleKind kind, const Program *program, GraphId graph, uint32_t index, Type *typeRef,
    mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(Handle), alignof(Handle));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    return new (mem) Handle(kind, program, nullptr, graph, index, typeRef);
}

Handle *Handle::createInDraft(
    const camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t node,
    mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(Handle), alignof(Handle));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    return new (mem) Handle(HandleKind::Node, nullptr, &draft, draft.id(), node, nullptr);
}

bool Handle::equals(const rtdata::Object *other, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    if (this == other) {
        return true;
    }
    if (!rtdata::isOfSameCls(this, other)) {
        return false;
    }
    const auto *that = static_cast<const Handle *>(other);
    return kind_ == that->kind_ && program_ == that->program_ && draft_ == that->draft_ &&
           graph_ == that->graph_ && index_ == that->index_ && typeRef_ == that->typeRef_;
}

rtdata::Object *Handle::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    if (draft_ != nullptr) {
        return createInDraft(*draft_, index_, allocator);
    }
    return create(kind_, program_, graph_, index_, typeRef_, allocator);
}

void Handle::print(std::ostream &os, const Type *type) const {
    (void)type;
    os << handleKindName(kind_) << '(' << toValue(graph_) << ", " << index_ << ')';
}

void Handle::updateRefs(const rtdata::Object::RefRelocator &relocate, const Type *type) {
    // The Program a handle was read from lives as long as the handle does. A handle the
    // collector reaches after the program was freed (a dead one, not yet swept) names nothing.
    if (program_ == nullptr || !mm::graphSpace().isAllocated(const_cast<Program *>(program_))) {
        return;
    }
    relocate(
        static_cast<rtdata::Object *>(const_cast<Program *>(program_)),
        nullptr,
        rtdata::RefTraceInfo{
            .owner     = this,
            .ownerType = type,
            .slotType  = nullptr,
            .ownerKind = "Handle",
            .slotName  = "program",
        });
}

} // namespace camel::graphmod
