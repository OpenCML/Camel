// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
 *     TYPE              payload: the type
 *     DECL              payload: the name; flag: a function is declared
 *     FUNC, NREF, DREF  payload: the name; FUNC flags: a decorated block, a body-less
 *                       declaration of a C++ operator
 *     LINK, WITH        payload: the number of arguments written
 *     ACCS              payload: the index, or the key when the flag is clear
 *     BRCH              tag: BranchKind
 *     CASE              payload: the name bound; tag: CaseType
 *     CAST              payload: the target type; flag: written on a declaration
 *     EXIT              tag: ExitType
 *     EXEC              flags: sync block, scope of operands; tag: the numeric permissions
 *                       in force in it (design principles 7.5)
 *     EXPT              payload: the list of exported names
 *     LOOP              payload: the names of the state, then the name of the element with
 *                       `for`; tag: camel::core::LoopKind
 *     TMTCH             children: the subject type, then the TCASEs
 *     TCASE             tag: the shape mask; payload: the identity pattern types (if any);
 *                       child 0: the body
 *     SFOR              payload: the name of the walked entry, then the name of its type
 *                       (empty for `indices`); flag A: the generator is `indices`
 *     SLOOP             payload: the names of the state, then the name of the walked entry and
 *                       the name of its type (empty for `indices`); flag A: `indices`
 *     SCOMP             tag: 0 for `{ ... }`, 1 for `( ... )`, 2 for `T { ... }` (then child 1
 *                       is the type node naming `T`)
 *     VARI, ANNO, SACC  nothing
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
#include "camel/core/loop_kind.h"
#include "camel/core/operator_semantics.h"
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

    /// DATA: the string was written as a formatted string and lowered to `format` (the AST
    /// said so). Its format specifications are checked one by one, at their interpolations.
    bool fromFormattedString() const {
        check(NodeKind::DATA);
        return (flags_ & kFlagA) != 0;
    }
    void setFromFormattedString() {
        check(NodeKind::DATA);
        flags_ |= kFlagA;
    }

    /// TYPE: the type.
    Type *dataType() const {
        check(NodeKind::TYPE);
        return payload_.type;
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

    /// FUNC: the function a decorated block is (`@d { ... }`, language reference 7.6): its body
    /// is the block, its ANNO children are the block's decorators, and the LINK that is its
    /// parent calls it where the block is written.
    bool decoratedBlock() const {
        check(NodeKind::FUNC);
        return (flags_ & kFlagA) != 0;
    }
    void setDecoratedBlock() {
        check(NodeKind::FUNC);
        flags_ |= kFlagA;
    }

    /// FUNC, DECL: a body-less declaration (design principles 6.4, step 1). The FUNC
    /// the parser built has its type as its only child and no graph is built from it; the DECL the
    /// module makes of it stands for an operator of a C++ module, and what it declares is its
    /// signature.
    bool nativeDeclaration() const {
        ASSERT(kind_ == NodeKind::FUNC || kind_ == NodeKind::DECL, "The node is no declaration");
        return (flags_ & kFlagB) != 0;
    }
    void setNativeDeclaration() {
        ASSERT(kind_ == NodeKind::FUNC || kind_ == NodeKind::DECL, "The node is no declaration");
        flags_ |= kFlagB;
    }

    /// FUNC: the function a rule with `with` parameters is written as (design principles 5.5).
    /// Its `with` parameters are the rule's, its body makes the `Rule`, and a use of its name as
    /// a value -- `name<value>`, or `name` alone with every parameter defaulted -- is a call of
    /// it, not a function value, so the graph builder reads this mark where the name is used.
    bool ruleBinding() const {
        check(NodeKind::FUNC);
        return (flags_ & kFlagC) != 0;
    }
    void setRuleBinding() {
        check(NodeKind::FUNC);
        flags_ |= kFlagC;
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

    /// LOOP: `loop s1 = a1, ..., sk = ak (for x in xs)? (while c)? do b` (design principles
    /// 3.10). The children, in order: the k first values a1 ... ak; with `for`, the
    /// sequence xs; with `while`, an EXEC holding the condition c; last, the EXEC of the body b.
    /// The first values and the sequence are evaluated once, where the loop is; the condition
    /// sees the state names, the body the state names and the element.
    camel::core::LoopKind loopKind() const {
        check(NodeKind::LOOP);
        return static_cast<camel::core::LoopKind>(tag_);
    }
    /// LOOP: the number of names of the state (k).
    size_t loopStateCount() const {
        check(NodeKind::LOOP);
        return payload_.names->count - (camel::core::loopHasIterable(loopKind()) ? 1 : 0);
    }
    /// LOOP: the j-th name of the state.
    const std::string &loopStateName(size_t j) const {
        ASSERT(j < loopStateCount(), "No state name at this index");
        return *payload_.names->items[j];
    }
    /// LOOP with `for`: the name of the element.
    const std::string &loopElementName() const {
        ASSERT(camel::core::loopHasIterable(loopKind()), "The loop has no element");
        return *payload_.names->items[payload_.names->count - 1];
    }
    /// LOOP: the children, by role (see loopKind).
    node_ptr_t loopInitial(size_t j) const { return at(j); }
    node_ptr_t loopSequence() const {
        return camel::core::loopHasIterable(loopKind()) ? at(loopStateCount()) : nullptr;
    }
    node_ptr_t loopCondition() const {
        return camel::core::loopHasCondition(loopKind()) ? at(size() - 2) : nullptr;
    }
    node_ptr_t loopBody() const {
        check(NodeKind::LOOP);
        return back();
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
    /// CASE: the case carries a guard (`case p when c => ...`). A guarded case is taken only
    /// when its pattern matches *and* the guard holds, so it covers nothing on its own: the
    /// exhaustiveness checks (design principles 3.7, language reference 6.8.2) pass it by.
    bool guardedCase() const {
        check(NodeKind::CASE);
        return (flags_ & kFlagB) != 0;
    }
    void setGuardedCase() {
        check(NodeKind::CASE);
        flags_ |= kFlagB;
    }
    const std::string &binding() const {
        check(NodeKind::CASE);
        return *payload_.name;
    }
    /// CASE of an enum variant (CaseType::Enum): the variant as written, and the fields the
    /// pattern binds (design principles 5.3). For a tuple pattern the bindings are one per
    /// position, in order, with an empty name for `_`; for a struct pattern `fields` and
    /// `bindings` are the fields written, in the order written.
    struct EnumCase {
        const std::string *label;     // `Shape::Rect`
        const std::string **fields;   // the field names of a struct pattern
        const std::string **bindings; // the names bound, one per entry of the pattern
        size_t count;

        static size_t bytesFor(size_t count) {
            return sizeof(EnumCase) + 2 * count * sizeof(const std::string *);
        }
    };
    const EnumCase &enumCase() const {
        check(NodeKind::CASE);
        ASSERT(caseType() == CaseType::Enum, "The case names no enum variant");
        return *payload_.enumCase;
    }
    /// CASE of an enum variant: the pattern is written `{ ... }` (struct) rather than `( ... )`.
    bool enumStructPattern() const {
        check(NodeKind::CASE);
        ASSERT(caseType() == CaseType::Enum, "The case names no enum variant");
        return (flags_ & kFlagA) != 0;
    }

    /// TCASE: the shapes it selects, as a mask of typeCaseBit (design principles 5.5).
    uint16_t typeCasePatterns() const {
        check(NodeKind::TCASE);
        return tag_;
    }
    /// TCASE: the identity patterns of the case (`case int`, `case T`), in the order written
    /// after the shapes. Each is compared with the subject type exactly when the case is
    /// selected; a type variable is replaced by its binding first.
    size_t typeCaseTypeCount() const {
        check(NodeKind::TCASE);
        return payload_.types != nullptr ? payload_.types->count : 0;
    }
    Type *typeCaseTypeAt(size_t index) const {
        check(NodeKind::TCASE);
        ASSERT(payload_.types != nullptr && index < payload_.types->count, "No pattern type here");
        return payload_.types->items[index];
    }

    /// SFOR: the name bound to the walked entry in the body (`n`, or `j` for `indices`).
    const std::string &staticForValueName() const {
        check(NodeKind::SFOR);
        return *payload_.names->items[0];
    }
    /// SFOR: the name bound to the entry's type in the body (`F`; empty for `indices`).
    const std::string &staticForTypeName() const {
        check(NodeKind::SFOR);
        return *payload_.names->items[1];
    }
    /// SFOR: the name bound to the entry's tags in the body (`tags`, the third name of
    /// `(n, F, tags)`); empty when the head names two things or one. The element of `fields T` is
    /// the triple (name, type, tags); every other element gives one thing (`indices ts` the
    /// position, a written sequence the element), which is the rule the head follows.
    const std::string &staticForTagsName() const {
        check(NodeKind::SFOR);
        return *payload_.names->items[2];
    }
    /// SFOR: the generator walks the positions of a tuple value (`indices ts`) rather than the
    /// fields of a type (`fields T`).
    bool staticForIndices() const {
        check(NodeKind::SFOR);
        return (flags_ & kFlagA) != 0;
    }
    /// SFOR: the walked shape (child 0): a TYPE for `fields`, the operand value for `indices`.
    node_ptr_t staticForOperand() const {
        check(NodeKind::SFOR);
        return at(0);
    }
    /// SFOR: the body, entered once per round (child 1).
    node_ptr_t staticForBody() const {
        check(NodeKind::SFOR);
        return at(1);
    }

    /// SACC: the value whose field or element is taken (child 0).
    node_ptr_t staticAccessTarget() const {
        check(NodeKind::SACC);
        return at(0);
    }
    /// SACC: the compile-time key (child 1).
    node_ptr_t staticAccessKey() const {
        check(NodeKind::SACC);
        return at(1);
    }

    /// SCOMP: a struct comprehension (`{ ... }`) rather than a tuple one (`( ... )`).
    bool comprehensionIsStruct() const {
        check(NodeKind::SCOMP);
        return tag_ == 0;
    }
    /// SCOMP: the declared shape of `T { ... }`: a struct or tuple comprehension written with
    /// the shape it fills. The rounds then take both the field names and the kind from `T`.
    bool comprehensionSameShape() const {
        check(NodeKind::SCOMP);
        return tag_ == 2;
    }
    /// SCOMP: the `static for` whose rounds fill the result (child 0).
    node_ptr_t comprehensionGenerator() const {
        check(NodeKind::SCOMP);
        return at(0);
    }
    /// SCOMP: the type node naming the shape of `T { ... }` (child 1, same-shape only).
    node_ptr_t comprehensionShapeType() const {
        check(NodeKind::SCOMP);
        ASSERT(comprehensionSameShape(), "Only a same-shape comprehension has a shape type.");
        return at(1);
    }

    /// SLOOP: the number of names of the state (k).
    size_t staticLoopStateCount() const {
        check(NodeKind::SLOOP);
        return payload_.names->count - 3;
    }
    /// SLOOP: the j-th name of the state.
    const std::string &staticLoopStateName(size_t j) const {
        check(NodeKind::SLOOP);
        ASSERT(j < staticLoopStateCount(), "No state name at this index");
        return *payload_.names->items[j];
    }
    /// SLOOP: the name bound to the walked entry in the body (`n`, or `j` for `indices`).
    const std::string &staticLoopValueName() const {
        check(NodeKind::SLOOP);
        return *payload_.names->items[payload_.names->count - 3];
    }
    /// SLOOP: the name bound to the entry's type in the body (`F`; empty for `indices` and for a
    /// sequence).
    const std::string &staticLoopTypeName() const {
        check(NodeKind::SLOOP);
        return *payload_.names->items[payload_.names->count - 2];
    }
    /// SLOOP: the name bound to the entry's tags in the body (`tags`, the third name of
    /// `(n, F, tags)`); empty when the head names two things or one. See `staticForTagsName`.
    const std::string &staticLoopTagsName() const {
        check(NodeKind::SLOOP);
        return *payload_.names->items[payload_.names->count - 1];
    }
    /// SLOOP: the fold walks the positions of a tuple value (`indices ts`) rather than the
    /// fields of a type (`fields T`).
    bool staticLoopIndices() const {
        check(NodeKind::SLOOP);
        return (flags_ & kFlagA) != 0;
    }
    /// SLOOP: the fold walks a sequence written after `in` (`for i in 0..5`), rather than the
    /// fields of a type or the positions of a tuple value. The sequence must be one the compiler
    /// works out: an expression built only from compile-time constants through operations that
    /// are pure and cannot fail (and 
    /// which draws the line by that one rule). Then it is evaluated and unrolled -- a
    /// constant interval `a..b`, `range(a, b, step)` with constant arguments, an array literal
    /// or a constant array give one value per round, and a compile-time tuple gives the type at
    /// each position. The graph builder refuses anything else with
    /// `SemanticDiag::StaticLoopSequenceNotKnown`. Never set together with `staticLoopIndices`.
    bool staticLoopSequence() const {
        check(NodeKind::SLOOP);
        return (flags_ & kFlagB) != 0;
    }
    /// SLOOP: the j-th first value of the state (children 1 ... k).
    node_ptr_t staticLoopInitial(size_t j) const {
        check(NodeKind::SLOOP);
        return at(j + 1);
    }
    /// SLOOP: the walked shape (child 0): a TYPE for `fields`, the operand value for `indices`
    /// and for a written sequence.
    node_ptr_t staticLoopOperand() const {
        check(NodeKind::SLOOP);
        return at(0);
    }
    /// SLOOP: the body, entered once per field (the last child).
    node_ptr_t staticLoopBody() const {
        check(NodeKind::SLOOP);
        return back();
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

    /// The identity patterns of a TCASE: a counted array of types in the tree's arena.
    struct TypeList {
        size_t count;
        Type *items[1]; // `count` entries

        static size_t bytesFor(size_t count) {
            return sizeof(TypeList) + (count > 1 ? count - 1 : 0) * sizeof(Type *);
        }
    };

    union Payload {
        const data_ptr_t *data;
        Type *type;
        const std::string *name;
        uint64_t number;
        const NameList *names;
        const TypeList *types;
        const EnumCase *enumCase;
    };

    // The meaning of the kind-specific flags is given with each accessor above.
    static constexpr uint8_t kFlagA    = 1 << 0;
    static constexpr uint8_t kFlagB    = 1 << 1;
    static constexpr uint8_t kFlagC    = 1 << 2;
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
