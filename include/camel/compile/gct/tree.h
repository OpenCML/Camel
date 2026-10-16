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
 * Updated: Oct. 13, 2026
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
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "camel/common/ref.h"
#include "camel/compile/gct/node.h"

namespace camel::compile::gct {

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

    // One function per kind of node. A new node has no children, no origin and no parent.
    // A name is the written reference (`a::b`); one that is empty or ends in `::` is not a
    // reference and is refused with std::runtime_error (makeFunc takes the empty name of an
    // anonymous function).
    node_ptr_t makeData(const data_ptr_t &data);
    node_ptr_t makeType(Type *type, ImplMark impl);
    node_ptr_t makeDecl(const std::string &name, bool isFunc);
    node_ptr_t makeFunc(const std::string &name);
    node_ptr_t makeNRef(const std::string &name);
    node_ptr_t makeDRef(const std::string &name);
    node_ptr_t makeVari();
    node_ptr_t makeWait();
    node_ptr_t makeLink(size_t argCount);
    node_ptr_t makeWith(size_t argCount);
    node_ptr_t makeAccs(size_t index);
    node_ptr_t makeAccs(const std::string &key);
    node_ptr_t makeBrch(BranchKind kind = BranchKind::Cond);
    node_ptr_t makeCase(CaseType type, const std::string &binding = {});
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

    /// The `using namespace` in force in `node` (design principles 2.12): for the root, those
    /// of the module; for an EXEC that is a block, those written in the block; for a top-level
    /// declaration, those of the namespace blocks it is written in. What they name is found
    /// where a name is used. Null when there are none.
    void addUsings(const Node *node, const std::vector<Reference> &usings);
    const std::vector<Reference> *usingsOf(const Node *node) const;

    /// The namespaces of the block a top-level declaration is written in, when its name does
    /// not say them: an operator overload, whose name is the operator's (design principles
    /// 2.13). Null for any other node; the namespaces of a name are then its path.
    void setNamespaceOf(const Node *node, const std::vector<std::string> &path);
    const std::vector<std::string> *namespaceOf(const Node *node) const;

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
};

} // namespace camel::compile::gct
