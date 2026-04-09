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
 * Created: Mar. 07, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "antlr4-runtime/antlr4-runtime.h"
#include "camel/core/error/diagnostics/range.h"
#include "camel/core/source/ids.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::compile::gir {
class Node;
}

namespace camel::source {

/// Mark which compilation stage an origin came from, for diagnostics and debugger traces.
enum class OriginStage : uint8_t {
    Parser,
    AST,
    GCT,
    GIR,
    Bytecode,
    Runtime,
    Synthetic,
};

/// Mark the entity kind represented by an origin. An entity's position and semantic identity are
/// described by stage + kind together.
enum class OriginKind : uint8_t {
    Source,
    AstNode,
    GctNode,
    GirNode,
    Graph,
    BytecodePc,
    RuntimeSite,
    Combined,
    Synthetic,
};

std::string to_string(OriginStage stage);
std::string to_string(OriginKind kind);

/// Semantic part roles.
/// These roles do not replace primarySpan; they provide unified names for reverse mapping
/// from "primary span + semantic part".
enum class SemanticRole : uint8_t {
    Whole,
    Operator,
    Keyword,
    Callee,
    Receiver,
    Argument,
    ArgList,
    BranchCondition,
    BranchTarget,
    ValueProducer,
    BindingName,
    MemberName,
    IndexExpr,
    FuncName,
    ReturnType,
    Parameter,
    GenericParameter,
    GenericArgument,
    CaseValue,
    CaseBody,
    Capture,
};

std::string to_string(SemanticRole role);

/// Canonical cache for a single source file. Stores the full text and line start offsets for
/// offset -> line/column queries.
struct SourceFile {
    source_file_id_t id = kInvalidSourceFileId;
    std::string path;    // Logical or absolute path, used as the debugger/LSP file ID.
    std::string content; // Original file text; all span offsets are relative to it.
    std::vector<size_t> lineStarts = {
        0}; // Byte offset of each line start for fast line/column lookup.
};

/// Canonicalized source span. All stages reference it indirectly via SpanId instead of copying
/// CharRange.
struct SourceSpan {
    span_id_t id            = kInvalidSpanId;
    source_file_id_t fileId = kInvalidSourceFileId;
    size_t startOffset      = 0; // Half-open interval start.
    size_t endOffset        = 0; // Half-open interval end.

    bool valid() const { return id != kInvalidSpanId && fileId != kInvalidSourceFileId; }
};

/// Unified record for "which source code this entity came from" across stages.
/// parent is the main derivation chain; inputs is an optional multi-source input set.
struct OriginRecord {
    origin_id_t id        = kInvalidOriginId;
    span_id_t primarySpan = kInvalidSpanId; // Primary source span shown for this entity.
    origin_id_t parent =
        kInvalidOriginId; // Main derivation source, e.g. a GCT node from an AST node.
    OriginStage stage = OriginStage::Synthetic;
    OriginKind kind   = OriginKind::Synthetic;
    bool synthetic    = false;       // True if the entity is not directly present in source code.
    std::string label;               // Debug-friendly stage label, such as gct.data / gir.cast.
    std::vector<origin_id_t> inputs; // Extra sources for desugaring or merge scenarios.
};

/// Named source for a semantic part.
/// origin may point to a token-level anchor, an expression source, or a lower-level lowering
/// product.
struct SemanticPart {
    SemanticRole role  = SemanticRole::Whole;
    origin_id_t origin = kInvalidOriginId;
    int32_t slot       = -1; // Ordered index for parameters, branches, etc.; -1 when unnumbered.
    std::string label;       // For example "lhs", "rhs", "else", or "kwargs".
};

/// Semantic source bundle for an execution entity.
/// mainOrigin provides a stable fallback; parts / mergedInputs enable finer-grained debugging and
/// reverse mapping.
struct SemanticBundle {
    origin_id_t mainOrigin = kInvalidOriginId;
    std::vector<SemanticPart> parts;
    std::vector<origin_id_t> mergedInputs;
    bool synthetic = false;
    std::string syntheticReason;
};

/// Source-file registry and line/column lookup service.
class SourceManager {
  public:
    source_file_id_t registerFile(const std::string &path, const std::string &content);
    std::optional<source_file_id_t> findFileId(const std::string &path) const;
    const SourceFile *file(source_file_id_t id) const;
    camel::core::error::CharPos offsetToPos(source_file_id_t fileId, size_t offset) const;

  private:
    std::vector<SourceFile> files_ = {SourceFile{}};
    std::unordered_map<std::string, source_file_id_t> pathToId_; // path -> SourceFileId
};

/// Centralized storage for spans. A span has no stage semantics; it only identifies a source range.
class SpanArena {
  public:
    span_id_t create(source_file_id_t fileId, size_t startOffset, size_t endOffset);
    span_id_t fromTokenRange(
        source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens,
        const camel::core::error::TokenRange &range);
    span_id_t fromToken(
        source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens, antlr4::Token *token);
    const SourceSpan *span(span_id_t id) const;

  private:
    std::vector<SourceSpan> spans_ = {SourceSpan{}};
};

/// Centralized storage for origins. create creates root origins; derive walks the compilation chain
/// to create new entities.
class OriginTable {
  public:
    origin_id_t create(
        span_id_t primarySpan, OriginStage stage, OriginKind kind, std::string label = "",
        bool synthetic = false, std::vector<origin_id_t> inputs = {});
    origin_id_t derive(
        origin_id_t parent, OriginStage stage, OriginKind kind, std::string label = "",
        span_id_t primarySpan = kInvalidSpanId, bool synthetic = false,
        std::vector<origin_id_t> inputs = {});
    const OriginRecord *origin(origin_id_t id) const;

  private:
    std::vector<OriginRecord> origins_ = {OriginRecord{}};
};

/// Lightweight index for debugger queries.
/// It stores no spans, only stable execution-entity -> OriginId mappings.
class DebugMap {
  public:
    void registerGraphOrigin(const std::string &graphId, origin_id_t origin);
    void registerNodeOrigin(const std::string &nodeId, origin_id_t origin);
    void registerPcOrigin(size_t pc, origin_id_t origin);
    void registerRuntimeGraphOrigin(uintptr_t runtimeGraphKey, origin_id_t origin);

    origin_id_t graphOrigin(const std::string &graphId) const;
    origin_id_t nodeOrigin(const std::string &nodeId) const;
    origin_id_t pcOrigin(size_t pc) const;
    origin_id_t runtimeGraphOrigin(uintptr_t runtimeGraphKey) const;

  private:
    std::unordered_map<std::string, origin_id_t> graphOrigins_;      // graph stableId -> origin
    std::unordered_map<std::string, origin_id_t> nodeOrigins_;       // node stableId -> origin
    std::unordered_map<size_t, origin_id_t> pcOrigins_;              // bytecode pc -> origin
    std::unordered_map<uintptr_t, origin_id_t> runtimeGraphOrigins_; // runtime graph ptr -> origin
};

/// Semantic side table keyed by OriginId, mainly for tree-like stages such as AST/GCT.
class OriginSemanticMap {
  public:
    void registerBundle(origin_id_t origin, SemanticBundle bundle);
    const SemanticBundle *bundle(origin_id_t origin) const;

  private:
    std::unordered_map<origin_id_t, SemanticBundle> bundles_;
};

/// A side table keyed by stable execution entity IDs. It mainly serves GIR graphs/nodes and the
/// debugger.
class EntitySemanticMap {
  public:
    void registerBundle(const std::string &entityId, SemanticBundle bundle);
    const SemanticBundle *bundle(const std::string &entityId) const;

  private:
    std::unordered_map<std::string, SemanticBundle> bundles_;
};

/// Shared source-code mapping context for both compile time and runtime.
/// This is the single entry point for the whole system: files, spans, origins, debug map, and
/// the current execution point all live here.
class SourceContext {
  public:
    source_file_id_t registerFile(const std::string &path, const std::string &content);
    std::optional<source_file_id_t> findFileId(const std::string &path) const;

    span_id_t createSpan(source_file_id_t fileId, size_t startOffset, size_t endOffset);
    span_id_t spanFromTokenRange(
        source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens,
        const camel::core::error::TokenRange &range);
    span_id_t spanFromToken(
        source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens, antlr4::Token *token);

    origin_id_t createOrigin(
        span_id_t primarySpan, OriginStage stage, OriginKind kind, std::string label = "",
        bool synthetic = false, std::vector<origin_id_t> inputs = {});
    origin_id_t deriveOrigin(
        origin_id_t parent, OriginStage stage, OriginKind kind, std::string label = "",
        span_id_t primarySpan = kInvalidSpanId, bool synthetic = false,
        std::vector<origin_id_t> inputs = {});

    camel::core::error::CharRange resolveSpan(span_id_t spanId) const;
    camel::core::error::CharRange resolveOrigin(origin_id_t originId) const;

    const SourceSpan *span(span_id_t spanId) const { return spans_.span(spanId); }
    const OriginRecord *origin(origin_id_t originId) const { return origins_.origin(originId); }
    const SourceFile *sourceFile(source_file_id_t fileId) const { return manager_.file(fileId); }
    const SourceFile *fileForSpan(span_id_t spanId) const;
    const SourceFile *fileForOrigin(origin_id_t originId) const;
    std::string pathForSpan(span_id_t spanId) const;
    std::string pathForOrigin(origin_id_t originId) const;

    DebugMap &debugMap() { return debugMap_; }
    const DebugMap &debugMap() const { return debugMap_; }

    void registerAstSemantic(origin_id_t origin, SemanticBundle bundle);
    void registerGctSemantic(origin_id_t origin, SemanticBundle bundle);
    void registerGirGraphSemantic(const std::string &graphId, SemanticBundle bundle);

    const SemanticBundle *astSemantic(origin_id_t origin) const;
    const SemanticBundle *gctSemantic(origin_id_t origin) const;
    const SemanticBundle *girGraphSemantic(const std::string &graphId) const;
    /// GIR node semantics: draft-time uses the origin side, while sealed entities use the stable
    /// entity ID side (`Node::debugEntityId()`, e.g. `gnode:...`).
    const SemanticBundle *girNodeSemantic(const camel::compile::gir::Node *node) const;

    void cloneGirGraphDebugInfo(const std::string &fromGraphId, const std::string &toGraphId);
    /// Copy debug bindings during GIR clone / inline; the source node may be draft or already
    /// sealed.
    void cloneGirNodeDebugBinding(
        const camel::compile::gir::Node *fromNode, const camel::compile::gir::Node *toNode);

    /// Draft-stage binding: do not write string keys into DebugMap before sealing.
    void bindGirNodeDraftDebug(
        const camel::compile::gir::Node *node, origin_id_t origin, SemanticBundle bundle);
    void unbindGirNodeDraftDebug(const camel::compile::gir::Node *node);

    /// After rearrange, bind the draft origin to `entityId` in both DebugMap and the sealed
    /// semantic table.
    void sealPromoteGirNodeDebug(const camel::compile::gir::Node *node, std::string entityId);

    origin_id_t girNodeDraftOrigin(const camel::compile::gir::Node *node) const;
    origin_id_t resolveGirNodeOrigin(const camel::compile::gir::Node *node) const;

    void setCurrentRuntimeOrigin(origin_id_t origin);
    origin_id_t currentRuntimeOrigin() const;

  private:
    SourceManager manager_;         // Owns source files and line/column lookup.
    SpanArena spans_;               // Stores source spans.
    OriginTable origins_;           // Stores derivation relationships across stages.
    DebugMap debugMap_;             // Fast lookup table for runtime/debugger queries.
    OriginSemanticMap astSemantic_; // AST semantic anchor side table.
    OriginSemanticMap gctSemantic_; // GCT lowering semantic source side table.
    EntitySemanticMap girGraphs_;   // GIR graph-level semantic source side table.
    EntitySemanticMap
        girNodes_; // GIR node-level semantic source side table (stable ID after sealing).
    /// GIR node semantics indexed by origin, matching the draft Node* mapping to avoid depending on
    /// constructor-time string IDs.
    OriginSemanticMap girNodeByOrigin_;
    std::unordered_map<const camel::compile::gir::Node *, origin_id_t> girDraftNodeOrigins_;

    mutable std::mutex runtimeOriginMutex_;
    origin_id_t currentRuntimeOrigin_ =
        kInvalidOriginId; // Most recent source origin executed by the current thread.
};

using source_context_ptr_t = std::shared_ptr<SourceContext>;

} // namespace camel::source
