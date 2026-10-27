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
    /// `Node<T>`: a node of the graph whose value type is `T`. A node read in a program is a node
    /// without one (a bare `Node`), and a declaration that states the type of the value a node
    /// stands for -- a rule's or a fact's parameter (`v: T`) -- states this type (design principles
    /// 5.9).
    static HandleType *ofValueType(type::Type *valueType);
    /// `Txn<F>`: the transaction on a function of type `F` (any type, a type variable of a
    /// generic declaration included); `F` null is the bare `Txn`.
    static HandleType *ofFunctionType(type::Type *functionType);
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
    bool typeArguments() const override {
        return kind_ == HandleKind::Node || kind_ == HandleKind::Txn;
    }

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
    static HandleType *internWithArgument(HandleType *bare, type::Type *argument);

    HandleType(type::TypeCode code, HandleKind kind, type::Type *valueType)
        : OtherType(
              code, valueType == nullptr ? 0 : 1,
              valueType == nullptr
                  ? nullptr
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
