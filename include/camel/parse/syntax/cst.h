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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The concrete syntax tree of the hand-written parser.
 *
 * Storage. A tree is three flat arrays next to the token buffer: the nodes (24 bytes each: kind,
 * first and last token, the range of its children, its parent), the children (one 32-bit word
 * each: a node index, or a token index with the top bit set) and the errors. There is no heap
 * object per node and no virtual call; the arrays are filled front to back while parsing and
 * never touched again.
 *
 * Losslessness. A node covers the tokens from firstToken() to lastToken(); comments are tokens
 * of the buffer inside that range and white space is the text between tokens, so tree plus
 * buffer reproduce the source byte for byte.
 *
 * Access. `Node` is a two-word handle (tree, index). Children are found by kind
 * (`decl.get<cst::IdentRef>()`), by category (`pair.expr()`, `pair.type()`) or by position.
 * The typed handles `cst::X` exist for every kind of node_kinds.def; they add no members and
 * only say in a signature which kind a function takes.
 *
 * Shape. One kind per grammar production (the table in node_kinds.def gives each production).
 * Productions that only pass on to the next level leave no node.
 */

#pragma once

#include "token.h"

#include <iterator>

namespace camel::parse::syntax {

enum class NodeKind : uint8_t {
#define CAMEL_NODE(name, category) name,
#include "node_kinds.def"
#undef CAMEL_NODE
    Count_,
};

enum class NodeCategory : uint8_t { Other, Expr, Type };

inline constexpr size_t kNodeKindCount = static_cast<size_t>(NodeKind::Count_);

namespace detail {
inline constexpr NodeCategory kNodeCategories[kNodeKindCount] = {
#define CAMEL_NODE(name, category) NodeCategory::category,
#include "node_kinds.def"
#undef CAMEL_NODE
};
inline constexpr std::string_view kNodeNames[kNodeKindCount] = {
#define CAMEL_NODE(name, category) #name,
#include "node_kinds.def"
#undef CAMEL_NODE
};
} // namespace detail

constexpr NodeCategory nodeCategory(NodeKind kind) {
    return detail::kNodeCategories[static_cast<size_t>(kind)];
}
constexpr std::string_view nodeKindName(NodeKind kind) {
    return detail::kNodeNames[static_cast<size_t>(kind)];
}
constexpr bool isExprKind(NodeKind kind) { return nodeCategory(kind) == NodeCategory::Expr; }
constexpr bool isTypeKind(NodeKind kind) { return nodeCategory(kind) == NodeCategory::Type; }

// ---- Errors ----

/// What the parser found wrong at a token. The parser states the facts (which token, what
/// would have fitted, which construct it was in); camel/parse/syntax_errors.h turns them into
/// diagnostics.
struct SyntaxError {
    enum class Kind : uint8_t {
        Unexpected,            // the token fits nothing that can stand here; see `expected`
        Missing,               // the one token of `expected` had to stand here
        AssignmentInCondition, // `=` after the condition of an `if`
        MissingThen,           // an `if` whose condition is not followed by `then`
        MissingInitializer,    // `let x` without `=`; `token` is the last token of the binding
        KeywordAsName,         // a reserved word where a name is required
        ExtraSemicolon,        // `;;`
        ComparisonNeedsSpace,  // `i<n`: the `<` (aux) opened an argument list that fails here
        AngleCloseAfterSpace,  // `f<a >`: the `>` (token) is separated from the last argument
        LineStartsWithSign,    // a line starting with `+` that cannot start a statement
    };
    /// Members of `expected` that are not token kinds.
    enum Pseudo : uint8_t {
        Expression = 200,
        Declaration,
        Statement,
        TypeExpression,
        PatternItem,
    };

    Kind kind      = Kind::Unexpected;
    uint32_t token = 0; // index of the offending token in the buffer
    uint32_t aux   = 0; // a second token the kind refers to, if any
    std::vector<uint8_t> expected; // TokenKind values and Pseudo values
};

// ---- Tree ----

struct NodeData {
    uint32_t firstChild = 0; // index into the children array
    uint32_t childCount = 0;
    uint32_t firstToken = 0; // first and last token of the node that are not trivia; for a
    uint32_t lastToken  = 0; // node without tokens, the token it stands before
    uint32_t parent     = UINT32_MAX;
    NodeKind kind       = NodeKind::Error;
    uint8_t flags       = 0;
};

class Node;
class TokenRef;

class Tree {
  public:
    static constexpr uint32_t kTokenBit = 0x80000000u;
    static constexpr uint32_t kNoNode   = UINT32_MAX;

    Tree() = default;
    explicit Tree(TokenBuffer tokens) : tokens_(std::move(tokens)) {}

    const TokenBuffer &tokens() const { return tokens_; }
    const std::string &source() const { return tokens_.source(); }
    Node root() const;
    size_t nodeCount() const { return nodes_.size(); }
    size_t childSlotCount() const { return children_.size(); }
    const std::vector<SyntaxError> &errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty() || !tokens_.errors().empty(); }

    const NodeData &data(uint32_t id) const { return nodes_[id]; }
    uint32_t childWord(uint32_t slot) const { return children_[slot]; }

  private:
    friend class ParserImpl;
    TokenBuffer tokens_;
    std::vector<NodeData> nodes_;
    std::vector<uint32_t> children_;
    std::vector<SyntaxError> errors_;
    uint32_t root_ = kNoNode;
};

/// A token of a tree's buffer. Lines are zero-based; columns count code points.
class TokenRef {
  public:
    TokenRef() = default;
    TokenRef(const Tree *tree, uint32_t index) : tree_(tree), index_(index) {}

    explicit operator bool() const { return tree_ != nullptr; }
    uint32_t index() const { return index_; }
    const Token &raw() const { return tree_->tokens()[index_]; }
    TokenKind kind() const { return raw().kind; }
    bool is(TokenKind k) const { return tree_ != nullptr && raw().kind == k; }
    std::string_view view() const { return tree_->tokens().text(index_); }
    std::string text() const { return std::string(view()); }
    /// Handles are used like pointers: `token->text()`.
    const TokenRef *operator->() const { return this; }
    uint32_t begin() const { return raw().begin; }
    uint32_t end() const { return raw().end; }
    size_t line() const { return tree_->tokens().lineOf(raw().begin); }
    size_t column() const { return tree_->tokens().columnOf(raw().begin); }
    /// The line the token ends on (a string or a comment may span lines).
    size_t endLine() const {
        return raw().end > raw().begin ? tree_->tokens().lineOf(raw().end - 1) : line();
    }

  private:
    const Tree *tree_ = nullptr;
    uint32_t index_   = 0;
};

/// A child of a node: a node or a token.
class Child {
  public:
    Child() = default;
    Child(const Tree *tree, uint32_t word) : tree_(tree), word_(word) {}

    explicit operator bool() const { return tree_ != nullptr; }
    bool isToken() const { return tree_ != nullptr && (word_ & Tree::kTokenBit) != 0; }
    bool isNode() const { return tree_ != nullptr && (word_ & Tree::kTokenBit) == 0; }
    inline Node node() const;
    TokenRef token() const {
        return isToken() ? TokenRef(tree_, word_ & ~Tree::kTokenBit) : TokenRef();
    }
    inline std::string text() const;
    const Child *operator->() const { return this; }

  private:
    const Tree *tree_ = nullptr;
    uint32_t word_    = 0;
};

template <typename T> class NodeList;

class Node {
  public:
    /// Filters of NodeList other than a kind.
    static constexpr int kAnyExpr = -1;
    static constexpr int kAnyType = -2;
    static constexpr int kAnyNode = -3;

    Node() = default;
    Node(const Tree *tree, uint32_t id) : tree_(tree), id_(id) {}

    explicit operator bool() const { return tree_ != nullptr; }
    /// Handles are used like pointers: `node->kind()`.
    const Node *operator->() const { return this; }
    bool operator==(const Node &other) const { return tree_ == other.tree_ && id_ == other.id_; }
    const Tree *tree() const { return tree_; }
    uint32_t id() const { return id_; }
    NodeKind kind() const { return tree_->data(id_).kind; }
    bool is(NodeKind k) const { return tree_ != nullptr && kind() == k; }
    bool isExpr() const { return tree_ != nullptr && isExprKind(kind()); }
    bool isType() const { return tree_ != nullptr && isTypeKind(kind()); }
    Node parent() const {
        const uint32_t p = tree_->data(id_).parent;
        return p == Tree::kNoNode ? Node() : Node(tree_, p);
    }

    /// Whether the node covers no token at all (possible only in a text with errors).
    bool empty() const { return (tree_->data(id_).flags & 1) != 0; }
    size_t childCount() const { return tree_->data(id_).childCount; }
    Child child(size_t index) const {
        const NodeData &d = tree_->data(id_);
        return index < d.childCount
                   ? Child(tree_, tree_->childWord(d.firstChild + static_cast<uint32_t>(index)))
                   : Child();
    }
    Child lastChild() const { return childCount() == 0 ? Child() : child(childCount() - 1); }

    TokenRef firstToken() const { return TokenRef(tree_, tree_->data(id_).firstToken); }
    TokenRef lastToken() const { return TokenRef(tree_, tree_->data(id_).lastToken); }
    uint32_t begin() const { return firstToken().begin(); }
    uint32_t end() const { return lastToken().end(); }
    /// The text of the node's tokens run together, without white space and comments.
    std::string text() const;
    /// The source text the node covers, as written.
    std::string_view sourceText() const {
        return std::string_view(tree_->source()).substr(begin(), end() - begin());
    }

    /// The `nth` child node that passes `filter` (a NodeKind value or one of the kAny filters).
    Node nodeBy(int filter, size_t nth = 0) const;
    size_t countBy(int filter) const;

    Node node(NodeKind k, size_t nth = 0) const { return nodeBy(static_cast<int>(k), nth); }
    template <typename T> T get(size_t nth = 0) const {
        return T(nodeBy(static_cast<int>(T::Kind), nth));
    }
    template <typename T> NodeList<T> all() const;
    Node expr(size_t nth = 0) const { return nodeBy(kAnyExpr, nth); }
    Node type(size_t nth = 0) const { return nodeBy(kAnyType, nth); }
    NodeList<Node> exprs() const;
    NodeList<Node> types() const;
    NodeList<Node> nodes() const;

    /// The `nth` child token of kind `k` (a direct child, not a token of a nested node).
    TokenRef tok(TokenKind k, size_t nth = 0) const;
    bool has(TokenKind k) const { return static_cast<bool>(tok(k)); }
    size_t tokCount(TokenKind k) const;

    static bool passes(NodeKind kind, int filter) {
        switch (filter) {
        case kAnyExpr:
            return isExprKind(kind);
        case kAnyType:
            return isTypeKind(kind);
        case kAnyNode:
            return true;
        default:
            return static_cast<int>(kind) == filter;
        }
    }

  protected:
    const Tree *tree_ = nullptr;
    uint32_t id_      = 0;
};

/// The child nodes of a node that pass a filter, in order. Nothing is allocated: the list is
/// the parent and the filter, and walks the parent's children.
template <typename T> class NodeList {
  public:
    NodeList(Node parent, int filter) : parent_(parent), filter_(filter) {}

    class iterator {
      public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const T *;
        using reference         = T;

        iterator(Node parent, int filter, size_t at) : parent_(parent), filter_(filter), at_(at) {
            skip();
        }
        T operator*() const { return T(parent_.child(at_).node()); }
        iterator &operator++() {
            ++at_;
            skip();
            return *this;
        }
        bool operator==(const iterator &other) const { return at_ == other.at_; }
        bool operator!=(const iterator &other) const { return at_ != other.at_; }

      private:
        void skip() {
            const size_t n = parent_ ? parent_.childCount() : 0;
            while (at_ < n) {
                const Child c = parent_.child(at_);
                if (c.isNode() && Node::passes(c.node().kind(), filter_)) {
                    break;
                }
                ++at_;
            }
        }
        Node parent_;
        int filter_;
        size_t at_;
    };

    iterator begin() const { return iterator(parent_, filter_, 0); }
    iterator end() const { return iterator(parent_, filter_, parent_ ? parent_.childCount() : 0); }
    size_t size() const { return parent_ ? parent_.countBy(filter_) : 0; }
    bool empty() const { return !parent_ || !parent_.nodeBy(filter_, 0); }
    T operator[](size_t nth) const { return T(parent_.nodeBy(filter_, nth)); }
    T front() const { return (*this)[0]; }
    T back() const { return (*this)[size() - 1]; }

  private:
    Node parent_;
    int filter_;
};

template <typename T> NodeList<T> Node::all() const {
    return NodeList<T>(*this, static_cast<int>(T::Kind));
}
inline NodeList<Node> Node::exprs() const { return NodeList<Node>(*this, kAnyExpr); }
inline NodeList<Node> Node::types() const { return NodeList<Node>(*this, kAnyType); }
inline NodeList<Node> Node::nodes() const { return NodeList<Node>(*this, kAnyNode); }

inline Node Child::node() const { return isNode() ? Node(tree_, word_) : Node(); }
inline std::string Child::text() const {
    if (isToken()) {
        return token().text();
    }
    return isNode() ? node().text() : std::string();
}

inline Node Tree::root() const { return root_ == kNoNode ? Node() : Node(this, root_); }

/// A handle that states the kind it refers to. It may be empty (a missing optional child).
template <NodeKind K> class Typed : public Node {
  public:
    static constexpr NodeKind Kind = K;
    Typed() = default;
    explicit Typed(Node node) : Node(node) {}
    const Typed *operator->() const { return this; }
};

namespace cst {
#define CAMEL_NODE(name, category) using name = Typed<NodeKind::name>;
#include "node_kinds.def"
#undef CAMEL_NODE
} // namespace cst

} // namespace camel::parse::syntax
