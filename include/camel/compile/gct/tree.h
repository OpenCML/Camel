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
 * Created: Oct. 13, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tree: the graph construction tree of one module, and the memory its nodes live in.
 *
 * The GCT builder makes a Tree from the AST of a module; the module keeps it (the GIR
 * builder reads it, and generic functions are lowered again from it at every instantiation,
 * also from other modules). Nodes are made with the make* functions and read through the
 * accessors of Node (node.h, which describes a node's layout).
 *
 * Memory. A tree owns three things, all released together when it is destroyed:
 *
 *  - An arena of chunks. Nodes, their child arrays and the name lists of EXPT nodes are
 *    carved from it in order and never freed singly. A chunk is kChunkBytes long and aligned
 *    to its size, and starts with a header that names the tree: the tree of a node is found
 *    from the node's address alone (Tree::of), which is what lets `*parent << child` grow a
 *    child array without being handed the tree. Nodes are always in a chunk; an array too
 *    long for one gets a block of its own.
 *  - The names: every name a node carries is stored once per tree, and nodes point to it.
 *  - The constants: a DATA node refers to a `shared_ptr` kept here, since a node itself has
 *    no destructor to release one.
 *
 * A child array grows by doubling and the array it replaces stays in the arena, so the
 * arrays take at most twice what the final ones need.
 *
 * A node may have children made by another tree (a generic function of an imported module is
 * lowered again by the module that calls it, with nodes of its own around it). The tree that
 * made a node must then outlive the trees that point to it; modules are kept by the context
 * until all of them go, so it does.
 *
 * Not thread-safe: a tree is built and read by the thread that compiles its module.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "camel/common/ref.h"
#include "camel/compile/gct/node.h"
#include "camel/compile/literal.h"
#include "camel/core/operator_table.h"

namespace camel::compile::gct {

/// The table's operator index (see the DREF the compiler makes for an operator by id).
using OperId = camel::core::OperId;

/// One `#` tag use (design principles 6.2) as written, before it is resolved: the name as
/// written (qualified by the namespaces of its module), its arguments lowered to data nodes
/// (a tag takes compile-time values), and where it is written. A use is kept beside the node it
/// is written on, not in the node: a Node is 32 bytes and the compiler, not the program, reads
/// these.
struct TagUse {
    std::string name;
    std::vector<node_ptr_t> args;
    origin_id_t origin = camel::source::kInvalidOriginId;
};

class Tree {
  public:
    Tree() = default;
    ~Tree();
    Tree(const Tree &)            = delete;
    Tree &operator=(const Tree &) = delete;

    /// The node of the module: an EXEC with its declarations and statements. Null until the
    /// builder has set it.
    node_ptr_t root() const { return root_; }
    void setRoot(node_ptr_t root) { root_ = root; }

    // ---- Tags written with `#` (design principles 6.2) --------------------------------------
    /// The tag uses written before `node` (a function, a block, or a type declaration).
    void addTagUses(const Node *node, std::vector<TagUse> uses);
    /// The tag uses of `node`; nullptr when it has none.
    const std::vector<TagUse> *tagUsesOf(const Node *node) const;
    /// The tag uses written on field `field` of the struct type `decl` declares.
    void addFieldTagUses(const Node *decl, size_t field, std::vector<TagUse> uses);
    /// The fields of `decl` that have tag uses, by field index; nullptr when none has.
    const std::map<size_t, std::vector<TagUse>> *fieldTagUsesOf(const Node *decl) const;

    // One function per kind of node. A new node has no children, no origin and no parent.
    // A name is the written reference (`a::b`); one that is empty or ends in `::` is not a
    // reference and is refused with std::runtime_error (makeFunc takes the empty name of an
    // anonymous function).
    node_ptr_t makeData(const data_ptr_t &data);
    node_ptr_t makeType(Type *type);
    node_ptr_t makeDecl(const std::string &name, bool isFunc);
    node_ptr_t makeFunc(const std::string &name);
    node_ptr_t makeNRef(const std::string &name);
    node_ptr_t makeDRef(const std::string &name);
    /// A DREF the compiler makes for an operator it already knows by id (`oper::EnumMake`):
    /// the graph builder finds the group from the id, in the built-in module, instead of
    /// resolving `name` through the scopes and imports of the program. `name` is the group's
    /// entity name, kept as the node's label (the GCT dump, a diagnostic about the node).
    node_ptr_t makeDRef(OperId oper, const std::string &name);
    /// The id of the operator a DREF made by makeDRef(OperId, name) stands for; nullopt for
    /// every other node.
    std::optional<OperId> operRefOf(const Node *node) const;
    node_ptr_t makeVari();
    node_ptr_t makeWait();
    node_ptr_t makeLink(size_t argCount);
    node_ptr_t makeWith(size_t argCount);
    node_ptr_t makeTypeApplication();
    node_ptr_t makeAccs(size_t index);
    node_ptr_t makeAccs(const std::string &key);
    node_ptr_t makeBrch(BranchKind kind = BranchKind::Cond);
    node_ptr_t makeCase(CaseType type, const std::string &binding = {});
    /// `match type` (children: TYPE, TCASE...) and one of its cases (child: the body).
    node_ptr_t makeTypeMatch();
    node_ptr_t makeTypeCase(uint16_t patterns);
    /// `static for (n, F) in fields T` (children: TYPE, body) or `static for (j) in indices ts`
    /// (children: the operand value, body). `valueName` is `n` (or `j`), `typeName` is `F`
    /// (empty for `indices`); both are bound only while the body is built.
    node_ptr_t makeStaticFor(
        const std::string &valueName, const std::string &typeName, bool byIndices = false);
    /// `static loop s1 = a1, ..., sk = ak for (n, F) in fields T do body` (children: TYPE, the k
    /// first values, body), or the same over `indices ts` (children: the operand value, the k
    /// first values, body). `state` are the names of the state; `valueName` is `n` (or `j`) and
    /// `typeName` is `F` (empty for `indices`), both bound only while the body is built.
    node_ptr_t makeStaticLoop(
        const std::vector<std::string> &state, const std::string &valueName,
        const std::string &typeName, bool byIndices = false);
    /// `t.[key]` (children: the target, the key expression).
    node_ptr_t makeStaticAccess();
    /// A comprehension (child: the `static for` that generates it): `isStruct` for
    /// `{ static for ... }`, false for `( static for ... )` (H51 step 4).
    node_ptr_t makeComprehension(bool isStruct);
    /// A CASE of an enum variant: `label` the variant as written (`Shape::Rect`), `fields` and
    /// `bindings` the entries of the pattern in the order written (see Node::EnumCase).
    node_ptr_t makeEnumCase(
        const std::string &label, const std::vector<std::string> &fields,
        const std::vector<std::string> &bindings, bool structShape);
    node_ptr_t makeCast(Type *targetType, bool annotation = false);
    node_ptr_t makeAnno();
    node_ptr_t makeExit(ExitType type = ExitType::Return);
    node_ptr_t makeExec(bool synced = false, bool operands = false);
    node_ptr_t makeExpt(const std::vector<Reference> &exports);
    /// A LOOP node: `state` the names of the state, `element` the name of the element (empty
    /// for a While loop). The children are appended by the caller (Node::loopKind).
    node_ptr_t makeLoop(
        camel::core::LoopKind kind, const std::vector<std::string> &state,
        const std::string &element);

    /// A copy, made by this tree, of `node` and of everything below it, with the origins of
    /// the original and without a parent: for a subtree that is needed in two places. The
    /// original may belong to another tree.
    node_ptr_t clone(const Node *node);

    /// The tree as indented text, one node per line (`camel inspect --gct`).
    void print(std::ostream &os) const;

    /// The `using namespace` in force in `node` (design principles 10.2): for the root, those
    /// of the module; for an EXEC that is a block, those written in the block; for a top-level
    /// declaration, those of the namespace blocks it is written in. What they name is found
    /// where a name is used. Null when there are none.
    void addUsings(const Node *node, const std::vector<Reference> &usings);
    const std::vector<Reference> *usingsOf(const Node *node) const;

    /// The namespaces of the block a top-level declaration is written in, when its name does
    /// not say them: an operator overload, whose name is the operator's (design principles
    /// 10.2). Null for any other node; the namespaces of a name are then its path.
    void setNamespaceOf(const Node *node, const std::vector<std::string> &path);
    const std::vector<std::string> *namespaceOf(const Node *node) const;

    /// The exact value of a numeric literal node, as written (design principles 5.1). It is
    /// kept beside the node, not in it: the type and the value the literal takes are decided
    /// later, from its context, and until then `0.1` must not become the nearest double.
    void setLiteralValue(const Node *node, literal::Value value);
    const literal::Value *literalValueOf(const Node *node) const;

    /// An element of a composite literal as it was written: its exact value (std::nullopt for
    /// an element that is not a numeric literal, and for a nested composite, whose own entry
    /// holds its elements) and the position it was written at. The position is where a
    /// diagnostic about the element points and what it quotes, as it does in a declared
    /// position; the value lets the element be rounded once, from what was written, when the
    /// type of its position is known (design principles 5.1).
    struct LiteralElement {
        std::optional<literal::Value> value;
        camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
    };

    /// The elements of a composite literal, one for each element in the order its data holds
    /// them (design principles 5.1).
    void setLiteralElements(const data_ptr_t &data, std::vector<LiteralElement> elements);
    const std::vector<LiteralElement> *literalElementsOf(const data_ptr_t &data) const;

    /// A parameter of a lambda whose type is left out (design principles 5.9): its index among
    /// the parameters (`with` parameters first) and the origin of the pair, where the
    /// diagnostic goes when the expected function type does not determine it. The signature
    /// built from the lambda stands `void` in its place until GIR, the only stage that knows
    /// the expected type, fills it.
    struct UnwrittenParam {
        size_t index                      = 0;
        camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
    };

    /// The parameters of the lambda `func` whose type was left out, in the order written. Null
    /// when there are none.
    void setUnwrittenParams(const Node *func, std::vector<UnwrittenParam> params);
    const std::vector<UnwrittenParam> *unwrittenParamsOf(const Node *func) const;

    /// The number of nodes made so far, attached to the root or not.
    size_t nodeCount() const { return nodeCount_; }
    /// The bytes taken from the system for nodes, child arrays and name lists.
    size_t arenaBytes() const { return arenaBytes_; }

    /// The tree that made `node`.
    static Tree *of(const Node *node);

  private:
    friend class Node;

    /// Chunks are aligned to their size, so that masking the address of anything inside one
    /// gives its header. 16 KiB holds about 500 nodes: a small module takes one chunk.
    static constexpr size_t kChunkBytes = 16 * 1024;
    /// An array longer than this gets a block of its own instead of a place in a chunk.
    static constexpr size_t kLargeBytes = kChunkBytes / 4;

    struct Chunk {
        Tree *owner;
        Chunk *next; // the chunk filled before this one
        size_t used; // bytes taken, the header included
    };

    /// `bytes` of memory aligned for a pointer, released with the tree: from the chunk being
    /// filled, or a block of its own when it is longer than kLargeBytes.
    void *allocate(size_t bytes);
    Node *makeNode(NodeKind kind);
    const std::string *intern(std::string_view name);
    const std::string *internReference(std::string_view name);
    /// Appends `child` to `parent`, a node of this tree, growing its child array when full.
    void append(Node &parent, Node *child);

    struct NameHash {
        using is_transparent = void;
        size_t operator()(std::string_view text) const {
            return std::hash<std::string_view>{}(text);
        }
    };

    node_ptr_t root_   = nullptr;
    Chunk *chunks_     = nullptr; // the chunk being filled; older ones follow by `next`
    size_t nodeCount_  = 0;
    size_t arenaBytes_ = 0;
    // Arrays too long for a chunk. One that was outgrown is kept until the tree goes.
    std::vector<std::unique_ptr<uint64_t[]>> largeBlocks_;
    // Node-based, so a name keeps its address while others are added.
    std::unordered_set<std::string, NameHash, std::equal_to<>> names_;
    // A deque: an element keeps its address while others are added.
    std::deque<data_ptr_t> constants_;
    std::unordered_map<const Node *, std::vector<Reference>> usings_;
    std::unordered_map<const Node *, std::vector<std::string>> namespaces_;
    std::unordered_map<const Node *, std::vector<TagUse>> tagUses_;
    // The DREFs the compiler made for an operator it knows by id (makeDRef(OperId, name)),
    // and that id: a DREF's payload is its name, so the id is kept beside the node.
    std::unordered_map<const Node *, OperId> operRefs_;
    std::unordered_map<const Node *, std::map<size_t, std::vector<TagUse>>> fieldTagUses_;
    std::unordered_map<const Node *, literal::Value> literalValues_;
    std::unordered_map<const Node *, std::vector<UnwrittenParam>> unwrittenParams_;
    // Keyed by the data object: a composite literal's elements are converted from it, and a
    // nested literal keeps its own entry.
    std::unordered_map<const camel::core::data::Data *, std::vector<LiteralElement>>
        literalElements_;
};

} // namespace camel::compile::gct
