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
 * Created: May. 05, 2024
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Node: one node of the graph construction tree.
 *
 * Layout. A node is 32 bytes and has no virtual functions and no destructor:
 *
 *     kind (1) | flags (1) | tag (2) | child count (4)
 *     origin (8)                         the node's origin in the source mapping
 *     children (8)                       an array of child pointers in the tree's arena
 *     payload (8)                        what the kind carries, see below
 *
 * What a kind carries is spread over `payload`, `tag` and `flags`:
 *
 *     DATA              payload: the constant (a pointer to a shared_ptr the tree keeps)
 *     TYPE              payload: the type; tag: ImplMark
 *     DECL              payload: the name; flag: a function is declared
 *     FUNC, NREF, DREF  payload: the name
 *     LINK, WITH        payload: the number of arguments written
 *     ACCS              payload: the index, or the key when the flag is clear
 *     BRCH              tag: BranchKind
 *     CASE              payload: the name bound; tag: CaseType
 *     CAST              payload: the target type; flag: written on a declaration
 *     EXIT              tag: ExitType
 *     EXEC              flags: sync block, scope of operands; tag: the numeric permissions
 *                       in force in it (design principles 2.13)
 *     EXPT              payload: the list of exported names
 *     VARI, WAIT, ANNO  nothing
 *
 * Ownership and lifetime. Nodes are made by a Tree (tree.h) in its arena and are never freed
 * one by one: a node, its child array, the names it points to and the constants it refers to
 * live exactly as long as the Tree. A `node_ptr_t` is a plain pointer and owns nothing.
 * Names are interned by the tree, so two nodes of one tree with the same name point to the
 * same string.
 *
 * Invariants.
 *  - A node is the child of at most one node (operator<< asserts it); a subtree needed
 *    twice is copied with Tree::clone.
 *  - Children are only appended. The child array is reallocated when it is full, so a
 *    pointer into it (begin()) does not survive an append; the child nodes do not move.
 *  - An accessor is only called on a node of a kind it is for (asserted in debug builds).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "camel/compile/gct/types.h"
#include "camel/core/data.h"
#include "camel/core/func.h"
#include "camel/core/impl.h"
#include "camel/core/source/ids.h"
#include "camel/utils/assert.h"

namespace camel::compile::gct {

using origin_id_t = camel::source::origin_id_t;
using data_ptr_t  = camel::core::data::data_ptr_t;
using Type        = camel::core::type::Type;

class Node {
  public:
    Node(const Node &)            = delete;
    Node &operator=(const Node &) = delete;

    NodeKind type() const { return kind_; }

    /// The origin of the node in the source mapping (camel/core/source/manager.h).
    origin_id_t origin() const { return origin_; }
    void setOrigin(origin_id_t origin) { origin_ = origin; }

    // ------------------ Children ------------------
    size_t size() const { return childCount_; }
    bool empty() const { return childCount_ == 0; }
    /// The child at `index`; std::out_of_range when there is none.
    node_ptr_t at(size_t index) const {
        if (index >= childCount_) {
            throw std::out_of_range("GCT node has no child at this index");
        }
        return children_[index];
    }
    node_ptr_t operator[](size_t index) const { return at(index); }
    node_ptr_t front() const { return at(0); }
    node_ptr_t back() const { return at(childCount_ == 0 ? 0 : childCount_ - 1); }
    Node *const *begin() const { return children_; }
    Node *const *end() const { return children_ + childCount_; }

    /// Appends `child` (a null pointer is ignored) and returns this node, so that appends
    /// chain: `*call << callee << argument`. The child must not have a parent.
    Node &operator<<(node_ptr_t child);

    // ------------------ What the node carries ------------------

    /// DATA: the constant.
    const data_ptr_t &data() const {
        check(NodeKind::DATA);
        return *payload_.data;
    }

    /// TYPE: the type, and how a function of that type is implemented.
    Type *dataType() const {
        check(NodeKind::TYPE);
        return payload_.type;
    }
    ImplMark implMark() const {
        check(NodeKind::TYPE);
        return static_cast<ImplMark>(tag_);
    }

    /// DECL, FUNC, NREF, DREF: the name as written, qualified names with `::` between the
    /// parts. Empty for an anonymous FUNC.
    const std::string &name() const {
        ASSERT(
            kind_ == NodeKind::DECL || kind_ == NodeKind::FUNC || kind_ == NodeKind::NREF ||
                kind_ == NodeKind::DREF,
            "The node has no name");
        return *payload_.name;
    }

    /// DECL: whether a function is declared (else a type).
    bool isFunc() const {
        check(NodeKind::DECL);
        return (flags_ & kFlagA) != 0;
    }

    /// LINK, WITH: the number of arguments written.
    size_t argCount() const {
        ASSERT(kind_ == NodeKind::LINK || kind_ == NodeKind::WITH, "The node takes no arguments");
        return static_cast<size_t>(payload_.number);
    }

    /// EXIT: how the function is left.
    ExitType exitType() const {
        check(NodeKind::EXIT);
        return static_cast<ExitType>(tag_);
    }

    /// EXEC: a sync block.
    bool synced() const {
        check(NodeKind::EXEC);
        return (flags_ & kFlagA) != 0;
    }
    /// EXEC: a scope the lowering introduced (the operands of a composite literal
    /// `{ a: f(), b: g() }`, the names of a declaration `let (p, q) = f()`): not a block of
    /// the program, so its expressions take the ordering of the block they appear in.
    bool operands() const {
        check(NodeKind::EXEC);
        return (flags_ & kFlagB) != 0;
    }

    /// EXEC: the numeric permissions (camel/core/operator_semantics.h, PermissionMask) in
    /// force in this scope: those `@numeric(...)` grants on it, on the function whose body it
    /// is, and on every block and function it is lexically nested in (design principles 2.13:
    /// an arm, a loop body or a lambda written in the code is part of the code). Every EXEC
    /// the GCT builder makes carries them, so the graph builder reads them from the nearest
    /// EXEC whatever order it builds graphs in. 0 when none is granted.
    uint8_t numericPermissions() const {
        check(NodeKind::EXEC);
        return static_cast<uint8_t>(tag_);
    }
    void setNumericPermissions(uint8_t permissions) {
        check(NodeKind::EXEC);
        tag_ = permissions;
    }

    /// EXPT: the exported names.
    size_t exportCount() const {
        check(NodeKind::EXPT);
        return payload_.names->count;
    }
    const std::string &exportName(size_t index) const {
        check(NodeKind::EXPT);
        ASSERT(index < payload_.names->count, "No exported name at this index");
        return *payload_.names->items[index];
    }

    /// ACCS: an element by position (`t.0`) or a field by name (`s.x`).
    bool accessByIndex() const {
        check(NodeKind::ACCS);
        return (flags_ & kFlagA) != 0;
    }
    size_t accessIndex() const {
        ASSERT(accessByIndex(), "The access is by name");
        return static_cast<size_t>(payload_.number);
    }
    const std::string &accessKey() const {
        ASSERT(!accessByIndex(), "The access is by position");
        return *payload_.name;
    }

    /// BRCH: what selects the arm.
    BranchKind branchKind() const {
        check(NodeKind::BRCH);
        return static_cast<BranchKind>(tag_);
    }

    /// CASE: the pattern, and the name it binds (Bind and Error; empty otherwise).
    CaseType caseType() const {
        check(NodeKind::CASE);
        return static_cast<CaseType>(tag_);
    }
    const std::string &binding() const {
        check(NodeKind::CASE);
        return *payload_.name;
    }

    /// CAST: the target type.
    Type *targetType() const {
        check(NodeKind::CAST);
        return payload_.type;
    }
    /// CAST: the conversion is the type written on a declaration (`let x: T = 0`), not an
    /// `as` expression. It only matters for a literal converted to a type written with a
    /// type variable, which is converted when the generic function is instantiated by the
    /// rule the GCT applies to the same literal and a concrete type.
    bool annotation() const {
        check(NodeKind::CAST);
        return (flags_ & kFlagA) != 0;
    }

    /// The node as one line of the tree dump (`camel inspect --gct`).
    std::string toString() const;

  private:
    friend class Tree;

    /// The names of an EXPT node: a counted array in the tree's arena.
    struct NameList {
        size_t count;
        const std::string *items[1]; // `count` entries

        /// The size of a list of `count` names.
        static size_t bytesFor(size_t count) {
            return sizeof(NameList) + (count > 1 ? count - 1 : 0) * sizeof(const std::string *);
        }
    };

    union Payload {
        const data_ptr_t *data;
        Type *type;
        const std::string *name;
        uint64_t number;
        const NameList *names;
    };

    // The meaning of the two kind-specific flags is given with each accessor above.
    static constexpr uint8_t kFlagA    = 1 << 0;
    static constexpr uint8_t kFlagB    = 1 << 1;
    static constexpr uint8_t kAttached = 1 << 7; // the node is the child of a node

    explicit Node(NodeKind kind) : kind_(kind) { payload_.number = 0; }

    void check([[maybe_unused]] NodeKind expected) const {
        ASSERT(kind_ == expected, "The node is not of the kind this accessor is for");
    }

    NodeKind kind_;
    uint8_t flags_       = 0;
    uint16_t tag_        = 0;
    uint32_t childCount_ = 0;
    origin_id_t origin_  = camel::source::kInvalidOriginId;
    Node **children_     = nullptr;
    Payload payload_;
};

static_assert(sizeof(Node) == 32, "A GCT node is four words");

} // namespace camel::compile::gct
