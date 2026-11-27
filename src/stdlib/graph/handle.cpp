// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "handle.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/program.h"
#include "camel/utils/assert.h"

#include <mutex>
#include <ostream>

namespace camel::graphmod {
namespace err = camel::core::error;

using namespace camel::core::type;
namespace sem = camel::core::sem;
using camel::runtime::GraphId;
using camel::runtime::Program;

namespace {
/// The intern key that tells a draft from the graph it behaves as: a draft is the `Graph` handle
/// kind, so only the key of the table entry and the type argument tell the two apart.
constexpr uintptr_t kDraftKeyWord = 0x4452u; // "DR"

/// Whether a type is one the compiler is still binding -- a type variable of a generic function, or
/// the argument of an enum instance its value does not determine. A draft's type argument is one of
/// these on the declaration side (`commit(d: Draft<F>)`), and it takes the type the argument has;
/// once it is a function type on both sides, the two have to be the same one.
bool binding(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    const auto code = type->code();
    return code == TypeCode::TypeVar || code == TypeCode::EnumHole;
}
} // namespace

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
    }
    return "?";
}

// ---- the types ----

HandleType *HandleType::get(HandleKind kind) {
    constexpr size_t kKinds          = static_cast<size_t>(HandleKind::Tag) + 1;
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
                code,
                std::span<const uintptr_t>(parts, 1),
                [&]() -> Type                 *{
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
    // whose value type the declaration does not state, not that the value it stands for is a node
    // of unspecified type (language reference 9.6). The reading is the same one a node's own type
    // has everywhere else, so `Node<Node<int>>` is `Node<int>` and `Node<Node>` is `Node`.
    if (is(valueType, HandleKind::Node)) {
        return static_cast<HandleType *>(valueType);
    }
    // One object per value type, from the type table (see intern.h): `equals` compares the value
    // types, so the value type enters by canonical id and `Node<int>` is one type wherever it is
    // written, instead of one per value-type object.
    const uintptr_t parts[1] = {typeKeyWord(TypeTable::instance().canonical(valueType))};
    return static_cast<HandleType *>(TypeTable::instance().internOther(
        bare->code(),
        std::span<const uintptr_t>(parts, 1),
        [&]() -> Type * {
            void *mem = mm::permSpace().alloc(sizeof(HandleType), alignof(HandleType));
            ASSERT(mem != nullptr, "Failed to allocate HandleType from permSpace");
            return new (mem) HandleType(bare->code(), HandleKind::Node, valueType);
        }));
}

bool HandleType::is(const Type *type, HandleKind kind) {
    return type != nullptr && type->code() == get(kind)->code();
}

HandleType *HandleType::ofDraft(Type *functionType) {
    const HandleType *graph = get(HandleKind::Graph);
    // One object per function type, from the type table, keyed by the canonical id of the type: a
    // draft of `(x: int) => int` is one type wherever it is written, and `Draft<A>` and `Draft<B>`
    // are two, so committing one to the other is the type error it should be.
    if (functionType == nullptr) {
        const uintptr_t bare[2] = {static_cast<uintptr_t>(HandleKind::Graph), kDraftKeyWord};
        return static_cast<HandleType *>(TypeTable::instance().internOther(
            graph->code(),
            std::span<const uintptr_t>(bare, 2),
            [&]() -> Type * {
                void *mem = mm::permSpace().alloc(sizeof(HandleType), alignof(HandleType));
                ASSERT(mem != nullptr, "Failed to allocate HandleType from permSpace");
                return new (mem) HandleType(graph->code(), HandleKind::Graph, nullptr, true);
            }));
    }
    // Effect qualifiers take no part in a function type's identity (design principles 5.2):
    // `(x: int) => int` and `(x: int) => int pure` are one type, so `Draft<A>` and `Draft<B>` are
    // one type too, and a draft opened on a `pure` function commits through a call that writes the
    // type without the qualifier. The key is the canonical id of the signature with its resource
    // clause left unwritten; the type the draft states is the one it was first asked for, since a
    // type has no identity but its structure (5.2), and the value's effects are read off the value.
    Type *draftKey = functionType;
    if (functionType->code() == TypeCode::Function) {
        auto *signature = static_cast<FunctionType *>(functionType);
        if (signature->resources().mode != sem::ResourceSignature::Mode::Generic) {
            draftKey = signature->withResources(sem::ResourceSignature{});
        }
    }
    const uintptr_t parts[3] = {
        static_cast<uintptr_t>(HandleKind::Graph),
        kDraftKeyWord,
        typeKeyWord(TypeTable::instance().canonical(draftKey))};
    return static_cast<HandleType *>(TypeTable::instance().internOther(
        graph->code(),
        std::span<const uintptr_t>(parts, 3),
        [&]() -> Type * {
            void *mem = mm::permSpace().alloc(sizeof(HandleType), alignof(HandleType));
            ASSERT(mem != nullptr, "Failed to allocate HandleType from permSpace");
            return new (mem) HandleType(graph->code(), HandleKind::Graph, functionType, true);
        }));
}

const HandleType *asHandleType(const Type *type, HandleKind kind) {
    return HandleType::is(type, kind) ? static_cast<const HandleType *>(type) : nullptr;
}

std::string HandleType::toString() const {
    Type *value = valueType();
    const std::string name = draft_ ? std::string("Draft") : std::string(handleKindName(kind_));
    return value == nullptr ? name : name + "<" + value->toString() + ">";
}

std::string HandleType::mangle() const {
    Type *value = valueType();
    const std::string name =
        draft_ ? std::string("graph.Draft") : std::string("graph.") + handleKindName(kind_);
    return value == nullptr ? name : name + "_" + value->mangle();
}

Type *HandleType::clone(bool deep) const {
    (void)deep;
    if (draft_) {
        return paramCount_ != 1 ? ofDraft(nullptr) : ofDraft(params_[0]);
    }
    if (paramCount_ != 1) {
        return get(kind_);
    }
    return ofValueType(params_[0]);
}

Type *HandleType::unify(Type *other) const {
    const auto *have = asHandleType(other, kind_);
    if (have == nullptr || have->draft_ != draft_) {
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
    if (draft_ && !binding(mine) && !binding(theirs)) {
        // A draft is committed as the function type it was opened on, and the commit answers with
        // that same type (design 6; `commit`'s own "the same type as the one that was opened"): the
        // two are one type, not two the one gives way to. A draft of `(x: int) => int` is not a
        // draft of `(x: float) => float`, however far one function type can stand for the other.
        return typeEqualsIgnoringResources(mine, theirs) ? const_cast<HandleType *>(this) : nullptr;
    }
    Type *united = mine->unify(theirs);
    if (united == nullptr) {
        return nullptr;
    }
    return draft_ ? ofDraft(united) : ofValueType(united);
}

bool HandleType::equals(Type *other) const {
    const auto *have = asHandleType(other, kind_);
    if (have == nullptr || have->draft_ != draft_) {
        return false;
    }
    Type *mine   = valueType();
    Type *theirs = have->valueType();
    if (mine == nullptr || theirs == nullptr) {
        return mine == theirs;
    }
    // The draft states the function type it commits, and an effect qualifier is not part of that
    // type (design principles 5.2): `Draft<(x: int) => int>` and `Draft<(x: int) => int pure>` are
    // one type, so a draft of one is the draft the other names.
    return typeEqualsIgnoringResources(mine, theirs);
}

CastSafety HandleType::castSafetyFrom(Type *source) const {
    const auto *have = asHandleType(source, kind_);
    if (have == nullptr) {
        return CastSafety::Forbidden;
    }
    // A handle type with a type argument stands for a particular one of the things of its kind --
    // `Node<int>` for a node of an int, `Draft<F>` for the draft of a function of type `F` -- so
    // the argument is compared for sameness and never for standing in: a draft of one function type
    // is not a draft of another, and a graph is not a draft (design 6; `commit` answers "the same
    // type as the one that was opened").
    if (draft_) {
        if (!have->draft_) {
            return CastSafety::Forbidden;
        }
        Type *mine   = valueType();
        Type *theirs = have->valueType();
        if (mine == nullptr || theirs == nullptr) {
            return mine == theirs ? CastSafety::Safe : CastSafety::Forbidden;
        }
        if (binding(mine) || binding(theirs)) {
            return CastSafety::Safe;
        }
        return typeEqualsIgnoringResources(mine, theirs) ? CastSafety::Safe : CastSafety::Forbidden;
    }
    return CastSafety::Safe;
}

bool HandleType::assignableFrom(Type *source) const {
    const auto *have = asHandleType(source, kind_);
    if (have == nullptr) {
        return false;
    }
    // A draft is a graph -- the kernels take it as one -- but a graph is not a draft, and a draft
    // of one function type is not a draft of another: that is the type error committing the wrong
    // function type would otherwise not be. Two drafts are the same draft only when their function
    // types are the same type; before the compiler has bound a type variable there is nothing to
    // compare, and the algebra binds it.
    if (draft_) {
        if (!have->draft_) {
            return false;
        }
        Type *mine   = valueType();
        Type *theirs = have->valueType();
        if (mine == nullptr || theirs == nullptr) {
            return mine == theirs;
        }
        if (binding(mine) || binding(theirs)) {
            return true;
        }
        return mine->equals(theirs);
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
    if (draft_) {
        if (params.empty()) {
            return ofDraft(nullptr);
        }
        return params.size() == 1 ? ofDraft(params[0]) : nullptr;
    }
    if (kind_ != HandleKind::Node) {
        return params.empty() ? get(kind_) : nullptr;
    }
    if (params.empty()) {
        return get(kind_);
    }
    if (params.size() != 1) {
        return nullptr;
    }
    return ofValueType(params[0]);
}

Type *
HandleType::cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    if (draft_) {
        if (arguments.empty()) {
            return ofDraft(nullptr);
        }
        if (arguments.size() != 1 || arguments[0].type == nullptr) {
            error = "Draft takes one type argument: the type of the function it commits";
            return nullptr;
        }
        return ofDraft(arguments[0].type);
    }
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
    // A handle that names a program names a node that program has. A node made in a draft is not
    // in the published graph yet, and a handle that claimed it would send every reader to a slot
    // the graph does not have -- so the mistake is refused here, where the handle is made, rather
    // than where somebody reads it.
    if (kind == HandleKind::Node && program != nullptr) {
        const auto *published = program->graph(graph);
        if (published == nullptr || !published->containsNodeRef(index)) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                "graph.handle",
                "the node is not in the program's graph");
        }
    }
    void *mem = allocator.alloc(sizeof(Handle), alignof(Handle));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    return new (mem) Handle(kind, program, nullptr, nullptr, graph, index, typeRef);
}

Handle *Handle::createInTransaction(
    const camel::runtime::GraphTransaction &transaction, camel::runtime::gc_node_ref_t node,
    rtdata::Object *draft, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(Handle), alignof(Handle));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    // A node handle names the transaction, and the value that holds the session the transaction
    // belongs to: the draft is no argument of an edit, so the handle a node is read through is what
    // carries it.
    const auto &drafted = transaction.graph();
    return new (mem)
        Handle(HandleKind::Node, nullptr, &transaction, draft, drafted.id(), node, nullptr);
}

Handle *Handle::createDraft(
    const camel::runtime::GraphTransaction &transaction, rtdata::Object *draft,
    const Program *program, GraphId graph, mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(Handle), alignof(Handle));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    // The graph the draft is of, and the value that holds the session editing it: the one handle
    // that is `d`, and the only one that names the session a `commit` closes.
    return new (mem) Handle(HandleKind::Graph, program, &transaction, draft, graph, 0, nullptr);
}

Handle *Handle::createRegionInTransaction(
    const camel::runtime::GraphTransaction &transaction, rtdata::Object *draft, uint32_t region,
    mm::IAllocator &allocator) {
    void *mem = allocator.alloc(sizeof(Handle), alignof(Handle));
    if (mem == nullptr) {
        throw std::bad_alloc();
    }
    // A region of the draft is read through the draft: the record belongs to the transaction, and
    // the published graph has no row for a region an edit added. The draft is named for the same
    // reason a node handle names it -- it keeps the session alive while the handle is alive.
    const auto &drafted = transaction.graph();
    return new (mem)
        Handle(HandleKind::Region, nullptr, &transaction, draft, drafted.id(), region, nullptr);
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
    // A type is its structure and nothing else (design principles 5.2): two handles of the same
    // structure name the same type, whatever names (dimensions, parameters) they were written with.
    const bool sameType =
        typeRef_ == that->typeRef_ ||
        (kind_ == HandleKind::TypeRef && type::sameCanonicalType(typeRef_, that->typeRef_));
    return kind_ == that->kind_ && program_ == that->program_ &&
           transaction_ == that->transaction_ && graph_ == that->graph_ && index_ == that->index_ &&
           sameType;
}

rtdata::Object *Handle::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    if (draft_ != nullptr && kind_ == HandleKind::Graph) {
        return createDraft(*transaction_, draft_, program_, graph_, allocator);
    }
    if (transaction_ != nullptr) {
        return createInTransaction(*transaction_, index_, draft_, allocator);
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
    // The transaction a draft-backed handle names is borrowed, not owned: it is an ordinary C++
    // object outside the collector's graph, so it takes no part in tracing, and a handle that
    // outlives its session names nothing either. The value that holds the session is the one
    // exception: the handle of a draft is the only holder of it, so the collector moves it with
    // the handle.
    if (draft_ != nullptr) {
        draft_ = relocate(
            draft_,
            nullptr,
            rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = type,
                .slotType  = nullptr,
                .ownerKind = "Handle",
                .slotName  = "draft",
            });
    }
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
