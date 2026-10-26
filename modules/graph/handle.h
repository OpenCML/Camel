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
class GraphDraft;
} // namespace camel::runtime

namespace camel::graphmod {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

enum class HandleKind : uint8_t { Program, Graph, Node, Region, TypeRef, Op, Tag, Txn };

/// The name the language knows the type by: `Program`, `Graph`, `Node`, ...
const char *handleKindName(HandleKind kind);

class HandleType final : public type::OtherType {
  public:
    static HandleType *get(HandleKind kind);
    static bool is(const type::Type *type, HandleKind kind);

    HandleKind kind() const { return kind_; }

    /// A handle is not the value it stands for: it determines no type parameter of a generic
    /// function (design principles 5.9).
    bool determinesTypeParameters() const override { return false; }

    std::string toString() const override;
    std::string mangle() const override;
    type::Type *clone(bool deep = false) const override;
    bool equals(type::Type *other) const override;
    type::CastSafety castSafetyFrom(type::Type *source) const override;
    bool assignableFrom(type::Type *source) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;

  private:
    HandleType(type::TypeCode code, HandleKind kind) : OtherType(code), kind_(kind) {}

    HandleKind kind_;
};

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

    /// A Node handle that names a node of the draft being rewritten (no Program: a draft is not
    /// a published version). The node readers answer from the draft.
    static Handle *createInDraft(
        const camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t node,
        mm::IAllocator &allocator);

    HandleKind kind() const { return kind_; }
    const camel::runtime::Program *program() const { return program_; }
    const camel::runtime::GraphDraft *draft() const { return draft_; }
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
        const camel::runtime::GraphDraft *draft, camel::runtime::GraphId graph, uint32_t index,
        type::Type *typeRef)
        : kind_(kind), program_(program), draft_(draft), graph_(graph), index_(index),
          typeRef_(typeRef) {}

    HandleKind kind_;
    const camel::runtime::Program *program_;
    const camel::runtime::GraphDraft *draft_;
    camel::runtime::GraphId graph_;
    uint32_t index_;
    type::Type *typeRef_;
};

} // namespace camel::graphmod
