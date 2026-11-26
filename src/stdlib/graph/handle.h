// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The types of the graph module and the handle object behind most of them.
 *
 * A Program is the runtime Program itself: an immutable snapshot, compared by identity. Every
 * other handle (a graph, a node, a region, a type, an operator, a tag, a transaction) is a
 * Handle: a kind and the numbers that name the thing inside one Program. A handle holds the
 * Program it was read from, so reading through it always sees the snapshot it came from, and
 * two handles are equal when they name the same thing of the same Program.
 */

#pragma once

#include "camel/core/rtdata/base.h"
#include "camel/core/type/other.h"
#include "camel/runtime/graph_id.h"
#include "camel/runtime/node_ref.h"

#include <cstdint>
#include <string>

namespace camel::runtime {
class Program;
class GraphTransaction;
} // namespace camel::runtime

namespace camel::graphmod {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

enum class HandleKind : uint8_t { Program, Graph, Node, Region, TypeRef, Op, Tag };

/// The name the language knows the type by: `Program`, `Graph`, `Node`, ...
const char *handleKindName(HandleKind kind);

class HandleType final : public type::OtherType {
  public:
    static HandleType *get(HandleKind kind);
    /// `Node<T>`: a node of the graph whose value type is `T`. A node read in a program is a node
    /// without one (a bare `Node`), and a declaration that states the type of the value a node
    /// stands for -- a rule's or a fact's parameter (`v: T`) -- states this type (design principles
    /// 5.9).
    static HandleType *ofValueType(type::Type *valueType);
    static bool is(const type::Type *type, HandleKind kind);

    HandleKind kind() const { return kind_; }

    /// The type of the value this node stands for, or nullptr when it is written without one.
    type::Type *valueType() const { return paramCount_ == 1 ? params_[0] : nullptr; }

    /// A handle is not the value it stands for: it determines no type parameter of a generic
    /// function (design principles 5.9).
    bool determinesTypeParameters() const override { return false; }

    /// A node's argument is the type of the value it stands for, so the algebra reads it (design
    /// principles 5.9): `Node<T>` against `Node<int>` binds `T` to `int`. The other handle kinds
    /// take no argument.
    bool typeArguments() const override { return kind_ == HandleKind::Node; }

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    type::Type *unify(type::Type *other) const override;
    bool equals(type::Type *other) const override;
    type::CastSafety castSafetyFrom(type::Type *source) const override;
    bool assignableFrom(type::Type *source) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;
    type::Type *cloneWithArguments(
        std::span<const type::TypeArgument> arguments, std::string &error) const override;

  private:
    HandleType(type::TypeCode code, HandleKind kind, type::Type *valueType)
        : OtherType(
              code, valueType == nullptr ? 0 : 1,
              valueType == nullptr ? nullptr
                                   : copyParams(std::span<type::Type *const>(&valueType, 1))),
          kind_(kind) {}

    HandleKind kind_;
};

/// `type` as the handle type of `kind`, or nullptr when it is another type.
const HandleType *asHandleType(const type::Type *type, HandleKind kind);

class Handle final : public rtdata::Object {
  public:
    Handle(const Handle &)            = delete;
    Handle &operator=(const Handle &) = delete;

    /// A handle of `kind` into `program`. `graph` and `index` name the thing: a graph
    /// (`graph`), a node (`graph`, `index`), an operator (`index` is its OperId), a type
    /// (`typeRef`). What a kind does not use is zero.
    static Handle *create(
        HandleKind kind, const camel::runtime::Program *program, camel::runtime::GraphId graph,
        uint32_t index, type::Type *typeRef, mm::IAllocator &allocator);

    /// A Node handle that names a node of the graph one transaction owns. The owner is named, so
    /// no draft is held that a transaction does not own: this is the only way to be in a draft.
    ///
    /// Ownership, tracing and identity, in that order:
    /// - **Who holds it.** `draft` is the value `open` answered -- the one that holds the session
    ///   the transaction belongs to -- and the handle holds it the way it holds the Program it was
    ///   read from, so a handle keeps its session alive for as long as the handle is alive. A node
    ///   read through a draft therefore carries the draft, and an edit reaches the session through
    ///   the node it is given (design 15.2, 15.3: the draft is no argument).
    /// - **Tracing.** `updateRefs` traces the Program, and the draft a handle names. A transaction
    ///   itself is an ordinary C++ object outside the collector's graph and takes no part in
    ///   tracing.
    /// - **Identity.** Two handles name the same node when they name the same owner — the same
    ///   transaction pointer, or the same Program — and the same node in it.
    static Handle *createInTransaction(
        const camel::runtime::GraphTransaction &transaction, camel::runtime::gc_node_ref_t node,
        rtdata::Object *draft, mm::IAllocator &allocator);

    /// A Graph handle naming a graph a transaction is editing: the draft, and the graph it is a
    /// draft of. `program` is the Program the transaction was opened on, and `draft` the value
    /// that holds the session (`TxnState`), which is what `commit` and `abort` are given. A
    /// handle that names a draft is the one thing `edit`'s `d` is (design 15.1): the root query
    /// reads it and every edit op reaches the transaction through the nodes it names.
    static Handle *createDraft(
        const camel::runtime::GraphTransaction &transaction, rtdata::Object *draft,
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        mm::IAllocator &allocator);

    /// A Region handle for a region of the graph a transaction is editing. The draft keeps the
    /// region table the published graph had and the records its edits add, and `region_of` on a
    /// node of the draft names one of them -- so the handle names the transaction the same way a
    /// node handle does, and `region_kind` reads the draft's record rather than the published
    /// graph's (a region `inline` made has no record there at all).
    static Handle *createRegionInTransaction(
        const camel::runtime::GraphTransaction &transaction, rtdata::Object *draft, uint32_t region,
        mm::IAllocator &allocator);

    HandleKind kind() const { return kind_; }
    const camel::runtime::Program *program() const { return program_; }
    /// The transaction this handle names a node of, or null for a handle of a published Program.
    const camel::runtime::GraphTransaction *transaction() const { return transaction_; }
    /// The transaction as the kernels write through it: a handle is the only way to reach a
    /// draft, and an edit is a write by construction.
    camel::runtime::GraphTransaction *mutableTransaction() const {
        return const_cast<camel::runtime::GraphTransaction *>(transaction_);
    }
    /// The value holding the session the transaction belongs to, on a handle that names a draft.
    rtdata::Object *draft() const { return draft_; }
    camel::runtime::GraphId graph() const { return graph_; }
    uint32_t index() const { return index_; }
    type::Type *typeRef() const { return typeRef_; }

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    Handle(
        HandleKind kind, const camel::runtime::Program *program,
        const camel::runtime::GraphTransaction *transaction, rtdata::Object *draft,
        camel::runtime::GraphId graph, uint32_t index, type::Type *typeRef)
        : kind_(kind), program_(program), transaction_(transaction), draft_(draft), graph_(graph),
          index_(index), typeRef_(typeRef) {}

    HandleKind kind_;
    // program-holder-exempt: heap-self-tracking -- Handle is a heap object, and its updateRefs
    // relocates the Program it was read from (handle.cpp).
    const camel::runtime::Program *program_;
    const camel::runtime::GraphTransaction *transaction_;
    /// The value that holds the session a draft handle names, or null: a handle of a published
    /// Program and a node of a draft hold nothing of the session, which lives as long as the
    /// resource `open` made.
    rtdata::Object *draft_;
    camel::runtime::GraphId graph_;
    uint32_t index_;
    type::Type *typeRef_;
};

} // namespace camel::graphmod
