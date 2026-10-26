// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "handle.h"

#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
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
            // One object per handle kind, from the type table (see intern.h): the kind is what
            // `equals` compares, so it is what its entry is keyed by.
            const uintptr_t parts[1] = {i};
            types[i]                 = static_cast<HandleType *>(TypeTable::instance().internOther(
                code, std::span<const uintptr_t>(parts, 1), [&]() -> Type * {
                    void *mem = mm::permSpace().alloc(sizeof(HandleType), alignof(HandleType));
                    ASSERT(mem != nullptr, "Failed to allocate HandleType from permSpace");
                    return new (mem) HandleType(code, k, nullptr);
                }));
        }
    });
    return types[static_cast<size_t>(kind)];
}

HandleType *HandleType::ofValueType(Type *valueType) {
    HandleType *bare = get(HandleKind::Node);
    if (valueType == nullptr) {
        return bare;
    }
    // A node whose value type is itself a node is that node: `a: Node` says the parameter is a node
    // whose value type the declaration does not state, not that the value it stands for is a node of
    // unspecified type (language reference 9.6). The reading is the same one a node's own type has
    // everywhere else, so `Node<Node<int>>` is `Node<int>` and `Node<Node>` is `Node`.
    if (is(valueType, HandleKind::Node)) {
        return static_cast<HandleType *>(valueType);
    }
    // One object per value type, from the type table (see intern.h): `equals` compares the value
    // types, so the value type enters by canonical id and `Node<int>` is one type wherever it is
    // written, instead of one per value-type object.
    const uintptr_t parts[1] = {typeKeyWord(TypeTable::instance().canonical(valueType))};
    return static_cast<HandleType *>(TypeTable::instance().internOther(
        bare->code(), std::span<const uintptr_t>(parts, 1), [&]() -> Type * {
            void *mem = mm::permSpace().alloc(sizeof(HandleType), alignof(HandleType));
            ASSERT(mem != nullptr, "Failed to allocate HandleType from permSpace");
            return new (mem) HandleType(bare->code(), HandleKind::Node, valueType);
        }));
}

bool HandleType::is(const Type *type, HandleKind kind) {
    return type != nullptr && type->code() == get(kind)->code();
}

const HandleType *asHandleType(const Type *type, HandleKind kind) {
    return HandleType::is(type, kind) ? static_cast<const HandleType *>(type) : nullptr;
}

std::string HandleType::toString() const {
    Type *value = valueType();
    return value == nullptr ? std::string(handleKindName(kind_))
                            : std::string(handleKindName(kind_)) + "<" + value->toString() + ">";
}

std::string HandleType::mangle() const {
    Type *value = valueType();
    return value == nullptr ? std::string("graph.") + handleKindName(kind_)
                            : std::string("graph.") + handleKindName(kind_) + "_" + value->mangle();
}

Type *HandleType::clone(bool deep) const {
    (void)deep;
    return paramCount_ == 1 ? ofValueType(params_[0]) : get(kind_);
}

Type *HandleType::unify(Type *other) const {
    const auto *have = asHandleType(other, kind_);
    if (have == nullptr) {
        return nullptr;
    }
    Type *mine   = valueType();
    Type *theirs = have->valueType();
    if (mine == nullptr) {
        return const_cast<HandleType *>(have);
    }
    if (theirs == nullptr) {
        return const_cast<HandleType *>(this);
    }
    Type *united = mine->unify(theirs);
    return united == nullptr ? nullptr : ofValueType(united);
}

bool HandleType::equals(Type *other) const {
    const auto *have = asHandleType(other, kind_);
    if (have == nullptr) {
        return false;
    }
    Type *mine   = valueType();
    Type *theirs = have->valueType();
    if (mine == nullptr || theirs == nullptr) {
        return mine == theirs;
    }
    return mine->equals(theirs);
}

CastSafety HandleType::castSafetyFrom(Type *source) const {
    return is(source, kind_) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool HandleType::assignableFrom(Type *source) const {
    const auto *have = asHandleType(source, kind_);
    if (have == nullptr) {
        return false;
    }
    // A node written without a value type is a node whose value type the program did not state: a
    // caller takes it where a value type is stated (`range::<int>(n)`), and a node whose value type
    // is stated is taken where none is.
    Type *mine   = valueType();
    Type *theirs = have->valueType();
    if (mine == nullptr || theirs == nullptr) {
        return true;
    }
    return mine->assignableFrom(theirs);
}

OtherType *HandleType::cloneWithParams(std::span<Type *const> params) const {
    if (kind_ != HandleKind::Node) {
        return params.empty() ? get(kind_) : nullptr;
    }
    if (params.empty()) {
        return get(kind_);
    }
    return params.size() == 1 ? ofValueType(params[0]) : nullptr;
}

Type *
HandleType::cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    if (kind_ != HandleKind::Node) {
        if (!arguments.empty()) {
            error = std::string(handleKindName(kind_)) + " takes no type argument";
            return nullptr;
        }
        return get(kind_);
    }
    if (arguments.empty()) {
        return get(kind_);
    }
    if (arguments.size() != 1 || arguments[0].type == nullptr) {
        error = "Node takes one type argument: the type of the value the node stands for";
        return nullptr;
    }
    return ofValueType(arguments[0].type);
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
