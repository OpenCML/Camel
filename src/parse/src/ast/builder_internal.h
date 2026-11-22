// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What this header owns: the invariants every translation unit of the AST builder writes through.
 *
 * Not "what they all happen to mention" -- what they must agree on. A node gets its token range
 * once, its origin once, its semantic bundle once, and all of it comes from the build's one
 * per-thread state. Copied into each unit these would be several sets of rules that drift apart;
 * shared, they are one.
 *
 * What is here and why it has to be:
 *
 *  - g_sourceContext / g_sourceFileId / g_arena: the build in progress. `createNodeAs` allocates
 *    through `g_arena`, so every unit that makes a node needs the same arena; a copy per unit
 *    would put one file's nodes in an arena the next file cannot see, and the origins written
 *    against `g_sourceContext` would be a different table's. Declared here, defined once.
 *  - createNodeAs / unwrapNodeAs / any2node: how a load becomes a node in this arena.
 *  - setNodeTokenRange / ...ByContext / ...Between / extractTokenRangeFromContext: the range.
 *  - setNodeOriginByOffsets / nodeOrigin / deriveAstAnchorOrigin: the origin chain.
 *  - semanticPart / registerAstSemanticBundle: the semantic bundle bound to that origin.
 *  - rangeOf / docOf: the two readings a diagnostic and an exported declaration share.
 *
 * What is deliberately not here: a helper only one unit calls stays in that unit. It is not an
 * invariant, and a header entry would claim a sharing that does not exist.
 */

#pragma once

#include "camel/core/operator_names.h"
#include "camel/core/operator_semantics.h"
#include "camel/core/tags.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/parse/ast/type_case_words.h"

#include <cctype>
#include <cstdint>
#include <functional>
#include <set>

#include "camel/parse/syntax/lexer.h"
#include "camel/parse/syntax/parser.h"
#include "camel/parse/syntax/report.h"

#include "camel/utils/scope.h"
#include "camel/utils/token.h"
#include "camel/utils/type.h"
#include "camel/utils/utf8.h"

#define DEBUG_LEVEL -1

// The helpers below were written where these namespaces are open, and they name their types
// unqualified. Only the AST builder's own translation units include this header, and each of them
// opens the same namespaces for the same reason, so this repeats their context rather than adding
// one: a unit that includes this header reads exactly what it read before the builder was split.
using namespace std;
using namespace AST;
using namespace camel::core::error;

namespace syn = camel::parse::syntax;
namespace cst = camel::parse::syntax::cst;
using TK      = camel::parse::syntax::TokenKind;
using NK      = camel::parse::syntax::NodeKind;

namespace camel::parse::ast {

// The build in progress. One definition for the whole builder (builder.cpp), one value per thread:
// the units that write through them and the unit that opens them must be looking at the same one.
extern thread_local camel::source::source_context_ptr_t g_sourceContext;
extern thread_local camel::source::source_file_id_t g_sourceFileId;
// Where the nodes of the tree being built are allocated (value.h); empty outside a build.
extern thread_local std::shared_ptr<NodeArena> g_arena;

inline camel::source::origin_id_t nodeOrigin(const node_ptr_t &node) {
    return node ? node->load()->origin() : camel::source::kInvalidOriginId;
}

inline void setNodeOriginByOffsets(
    const node_ptr_t &node, size_t startOffset, size_t endOffset,
    const std::string &label = "ast") {
    if (!g_sourceContext || g_sourceFileId == camel::source::kInvalidSourceFileId) {
        return;
    }
    auto span = g_sourceContext->createSpan(g_sourceFileId, startOffset, endOffset);
    if (span == camel::source::kInvalidSpanId) {
        return;
    }
    auto origin = g_sourceContext->createOrigin(
        span,
        camel::source::OriginStage::AST,
        camel::source::OriginKind::AstNode,
        label);
    node->load()->setOrigin(origin);
}

// Where a diagnostic placed at tokens points: from the first character of `start` to the end
// of `end`. The end column is the start column of `end` plus its length in bytes, on the line
// `end` starts on (what the positions always were, also for a token with non-ASCII text).
inline CharRange rangeOf(syn::TokenRef start, syn::TokenRef end) {
    CharRange r{{0, 0}, {0, 0}};
    if (start) {
        r.start = {start.line(), start.column()};
    }
    const syn::TokenRef last = end ? end : start;
    if (last) {
        r.end = {last.line(), last.column() + (last.end() - last.begin())};
    }
    return r;
}
inline CharRange rangeOf(syn::TokenRef token) { return rangeOf(token, token); }

inline camel::source::origin_id_t
deriveAstAnchorOrigin(const node_ptr_t &owner, syn::TokenRef token, const std::string &label) {
    if (!g_sourceContext || g_sourceFileId == camel::source::kInvalidSourceFileId || !owner ||
        !token) {
        return camel::source::kInvalidOriginId;
    }
    auto span = g_sourceContext->createSpan(
        g_sourceFileId,
        static_cast<size_t>(token->begin()),
        static_cast<size_t>(token->end()));
    if (span == camel::source::kInvalidSpanId) {
        return camel::source::kInvalidOriginId;
    }
    auto parent = owner->load()->origin();
    if (parent != camel::source::kInvalidOriginId) {
        return g_sourceContext->deriveOrigin(
            parent,
            camel::source::OriginStage::AST,
            camel::source::OriginKind::AstNode,
            label,
            span);
    }
    return g_sourceContext->createOrigin(
        span,
        camel::source::OriginStage::AST,
        camel::source::OriginKind::AstNode,
        label);
}

inline camel::source::origin_id_t
deriveAstAnchorOrigin(const node_ptr_t &owner, syn::Node context, const std::string &label) {
    if (!g_sourceContext || g_sourceFileId == camel::source::kInvalidSourceFileId || !context ||
        !context->firstToken() || !context->lastToken()) {
        return camel::source::kInvalidOriginId;
    }
    auto span = g_sourceContext->createSpan(
        g_sourceFileId,
        static_cast<size_t>(context->firstToken()->begin()),
        static_cast<size_t>(context->lastToken()->end()));
    if (span == camel::source::kInvalidSpanId) {
        return camel::source::kInvalidOriginId;
    }
    auto parent = owner ? owner->load()->origin() : camel::source::kInvalidOriginId;
    if (parent != camel::source::kInvalidOriginId) {
        return g_sourceContext->deriveOrigin(
            parent,
            camel::source::OriginStage::AST,
            camel::source::OriginKind::AstNode,
            label,
            span);
    }
    return g_sourceContext->createOrigin(
        span,
        camel::source::OriginStage::AST,
        camel::source::OriginKind::AstNode,
        label);
}

inline camel::source::SemanticPart semanticPart(
    camel::source::SemanticRole role, camel::source::origin_id_t origin, int32_t slot = -1,
    const std::string &label = "") {
    return camel::source::SemanticPart{
        .role   = role,
        .origin = origin,
        .slot   = slot,
        .label  = label};
}

inline void registerAstSemanticBundle(
    const node_ptr_t &node, std::vector<camel::source::SemanticPart> parts = {},
    std::vector<camel::source::origin_id_t> mergedInputs = {}, bool synthetic = false,
    const std::string &syntheticReason = "") {
    if (!g_sourceContext || !node) {
        return;
    }
    auto mainOrigin = node->load()->origin();
    if (mainOrigin == camel::source::kInvalidOriginId) {
        return;
    }
    g_sourceContext->registerAstSemantic(
        mainOrigin,
        camel::source::SemanticBundle{
            .mainOrigin      = mainOrigin,
            .parts           = std::move(parts),
            .mergedInputs    = std::move(mergedInputs),
            .synthetic       = synthetic,
            .syntheticReason = syntheticReason});
}

template <typename LoadType, typename... Args> node_ptr_t createNodeAs(Args &&...args) {
    if (g_arena) {
        std::shared_ptr<Load> load = std::allocate_shared<LoadType>(
            NodeArenaAllocator<LoadType>(g_arena),
            std::forward<Args>(args)...);
        return std::allocate_shared<Node>(NodeArenaAllocator<Node>(g_arena), std::move(load));
    }
    return std::make_shared<Node>(
        std::dynamic_pointer_cast<Load>(std::make_shared<LoadType>(std::forward<Args>(args)...)));
}

template <typename LoadType> std::shared_ptr<LoadType> unwrapNodeAs(std::shared_ptr<Node> &node) {
    return std::dynamic_pointer_cast<LoadType>(node->load());
}

inline TokenRange extractTokenRangeFromContext(syn::Node context) {
    if (context && context->firstToken() && context->lastToken()) {
        return {context->firstToken()->index(), context->lastToken()->index() + 1};
    } else {
        return {0, 0};
    }
}

inline void setNodeTokenRange(const node_ptr_t &node, TokenRange range) {
    node->load()->setTokenRange(range);
}
inline void setNodeTokenRange(const node_ptr_t &node, size_t start, size_t end) {
    node->load()->setTokenRange(start, end);
}

inline void setNodeTokenRangeByContext(const node_ptr_t &node, syn::Node context) {
    node->load()->setTokenRange(extractTokenRangeFromContext(context));
    if (context && context->firstToken() && context->lastToken()) {
        setNodeOriginByOffsets(
            node,
            static_cast<size_t>(context->firstToken()->begin()),
            static_cast<size_t>(context->lastToken()->end()));
    }
}

/// The range from the first of two sibling nodes to the last.
inline void setNodeTokenRangeBetween(const node_ptr_t &node, syn::Node first, syn::Node last) {
    if (!first || !last) {
        node->load()->setTokenRange(0, 0);
        return;
    }
    size_t start = first->firstToken()->index();
    size_t end   = last->lastToken()->index() + 1;
    node->load()->setTokenRange(start, end);
    setNodeOriginByOffsets(
        node,
        static_cast<size_t>(first->firstToken()->begin()),
        static_cast<size_t>(last->lastToken()->end()));
}

/// The documentation of a declaration: the `///` lines right above its first token, or above
/// the `export` in front of it.
inline std::string docOf(syn::Node decl) {
    if (!decl || decl.empty()) {
        return {};
    }
    syn::Node outer = decl;
    if (decl.parent() && decl.parent().is(NK::ExportDecl)) {
        outer = decl.parent();
    }
    return decl.tree()->tokens().docAbove(outer.firstToken().index());
}

inline node_ptr_t any2node(Value &&a) { return std::move(a.as<node_ptr_t>()); }
inline node_ptr_t any2node(Value &a) { return a.as<node_ptr_t>(); }

} // namespace camel::parse::ast