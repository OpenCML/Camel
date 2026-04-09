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

#include "camel/core/source/manager.h"

#include "camel/compile/gir/nodes.h"

#include <algorithm>

using camel::core::error::CharPos;
using camel::core::error::CharRange;
using camel::core::error::TokenRange;

namespace camel::source {

namespace {

// Precompute each line's start offset so offset -> line/column only needs one upper_bound later.
std::vector<size_t> buildLineStarts(const std::string &content) {
    std::vector<size_t> lineStarts = {0};
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lineStarts.push_back(i + 1);
        }
    }
    return lineStarts;
}

// Convert a token half-open interval into a source byte interval.
// GCT/GIR no longer depend on tokens directly, so tokens are consumed only once at the frontend
// boundary.
std::pair<size_t, size_t>
tokenOffsets(const std::vector<antlr4::Token *> &tokens, const TokenRange &range) {
    if (tokens.empty() || range.start >= range.end) {
        return {0, 0};
    }

    size_t startIdx      = std::min(range.start, tokens.size() - 1);
    size_t endIdx        = std::min(range.end - 1, tokens.size() - 1);
    antlr4::Token *start = tokens[startIdx];
    antlr4::Token *end   = tokens[endIdx];
    if (!start || !end) {
        return {0, 0};
    }
    return {
        static_cast<size_t>(start->getStartIndex()),
        static_cast<size_t>(end->getStopIndex() + 1)};
}

} // namespace

std::string to_string(OriginStage stage) {
    switch (stage) {
    case OriginStage::Parser:
        return "Parser";
    case OriginStage::AST:
        return "AST";
    case OriginStage::GCT:
        return "GCT";
    case OriginStage::GIR:
        return "GIR";
    case OriginStage::Bytecode:
        return "Bytecode";
    case OriginStage::Runtime:
        return "Runtime";
    case OriginStage::Synthetic:
        return "Synthetic";
    }
    return "Unknown";
}

std::string to_string(OriginKind kind) {
    switch (kind) {
    case OriginKind::Source:
        return "Source";
    case OriginKind::AstNode:
        return "AstNode";
    case OriginKind::GctNode:
        return "GctNode";
    case OriginKind::GirNode:
        return "GirNode";
    case OriginKind::Graph:
        return "Graph";
    case OriginKind::BytecodePc:
        return "BytecodePc";
    case OriginKind::RuntimeSite:
        return "RuntimeSite";
    case OriginKind::Combined:
        return "Combined";
    case OriginKind::Synthetic:
        return "Synthetic";
    }
    return "Unknown";
}

std::string to_string(SemanticRole role) {
    switch (role) {
    case SemanticRole::Whole:
        return "whole";
    case SemanticRole::Operator:
        return "operator";
    case SemanticRole::Keyword:
        return "keyword";
    case SemanticRole::Callee:
        return "callee";
    case SemanticRole::Receiver:
        return "receiver";
    case SemanticRole::Argument:
        return "argument";
    case SemanticRole::ArgList:
        return "argList";
    case SemanticRole::BranchCondition:
        return "branchCondition";
    case SemanticRole::BranchTarget:
        return "branchTarget";
    case SemanticRole::ValueProducer:
        return "valueProducer";
    case SemanticRole::BindingName:
        return "bindingName";
    case SemanticRole::MemberName:
        return "memberName";
    case SemanticRole::IndexExpr:
        return "indexExpr";
    case SemanticRole::FuncName:
        return "funcName";
    case SemanticRole::ReturnType:
        return "returnType";
    case SemanticRole::Parameter:
        return "parameter";
    case SemanticRole::GenericParameter:
        return "genericParameter";
    case SemanticRole::GenericArgument:
        return "genericArgument";
    case SemanticRole::CaseValue:
        return "caseValue";
    case SemanticRole::CaseBody:
        return "caseBody";
    case SemanticRole::Capture:
        return "capture";
    }
    return "unknown";
}

source_file_id_t SourceManager::registerFile(const std::string &path, const std::string &content) {
    auto it = pathToId_.find(path);
    if (it != pathToId_.end()) {
        SourceFile &file = files_.at(it->second);
        file.content     = content;
        file.lineStarts  = buildLineStarts(content);
        return file.id;
    }

    source_file_id_t id = static_cast<source_file_id_t>(files_.size());
    files_.push_back(
        SourceFile{
            .id         = id,
            .path       = path,
            .content    = content,
            .lineStarts = buildLineStarts(content)});
    pathToId_[path] = id;
    return id;
}

std::optional<source_file_id_t> SourceManager::findFileId(const std::string &path) const {
    auto it = pathToId_.find(path);
    if (it == pathToId_.end()) {
        return std::nullopt;
    }
    return it->second;
}

const SourceFile *SourceManager::file(source_file_id_t id) const {
    if (id == kInvalidSourceFileId || id >= files_.size()) {
        return nullptr;
    }
    return &files_[id];
}

CharPos SourceManager::offsetToPos(source_file_id_t fileId, size_t offset) const {
    const SourceFile *src = file(fileId);
    if (!src) {
        return {};
    }
    size_t capped = std::min(offset, src->content.size());
    auto it       = std::upper_bound(src->lineStarts.begin(), src->lineStarts.end(), capped);
    size_t line =
        it == src->lineStarts.begin() ? 0 : static_cast<size_t>(it - src->lineStarts.begin() - 1);
    size_t col = capped - src->lineStarts[line];
    return {line, col};
}

span_id_t SpanArena::create(source_file_id_t fileId, size_t startOffset, size_t endOffset) {
    if (fileId == kInvalidSourceFileId) {
        return kInvalidSpanId;
    }
    span_id_t id = static_cast<span_id_t>(spans_.size());
    spans_.push_back(
        SourceSpan{
            .id          = id,
            .fileId      = fileId,
            .startOffset = startOffset,
            .endOffset   = std::max(startOffset, endOffset)});
    return id;
}

span_id_t SpanArena::fromTokenRange(
    source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens, const TokenRange &range) {
    auto [start, end] = tokenOffsets(tokens, range);
    return create(fileId, start, end);
}

span_id_t SpanArena::fromToken(
    source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens, antlr4::Token *token) {
    if (!token) {
        return kInvalidSpanId;
    }
    TokenRange range{
        static_cast<size_t>(token->getTokenIndex()),
        static_cast<size_t>(token->getTokenIndex() + 1)};
    return fromTokenRange(fileId, tokens, range);
}

const SourceSpan *SpanArena::span(span_id_t id) const {
    if (id == kInvalidSpanId || id >= spans_.size()) {
        return nullptr;
    }
    return &spans_[id];
}

origin_id_t OriginTable::create(
    span_id_t primarySpan, OriginStage stage, OriginKind kind, std::string label, bool synthetic,
    std::vector<origin_id_t> inputs) {
    origin_id_t id = static_cast<origin_id_t>(origins_.size());
    origins_.push_back(
        OriginRecord{
            .id          = id,
            .primarySpan = primarySpan,
            .parent      = kInvalidOriginId,
            .stage       = stage,
            .kind        = kind,
            .synthetic   = synthetic,
            .label       = std::move(label),
            .inputs      = std::move(inputs)});
    return id;
}

origin_id_t OriginTable::derive(
    origin_id_t parent, OriginStage stage, OriginKind kind, std::string label,
    span_id_t primarySpan, bool synthetic, std::vector<origin_id_t> inputs) {
    // If the derived node does not specify a span, inherit the parent's primary span along the
    // main derivation chain. This is the basis for source recovery after lossy GCT/GIR lowering.
    if (primarySpan == kInvalidSpanId) {
        if (const OriginRecord *parentOrigin = origin(parent)) {
            primarySpan = parentOrigin->primarySpan;
        }
    }
    origin_id_t id =
        create(primarySpan, stage, kind, std::move(label), synthetic, std::move(inputs));
    origins_.back().parent = parent;
    return id;
}

const OriginRecord *OriginTable::origin(origin_id_t id) const {
    if (id == kInvalidOriginId || id >= origins_.size()) {
        return nullptr;
    }
    return &origins_[static_cast<size_t>(id)];
}

void DebugMap::registerGraphOrigin(const std::string &graphId, origin_id_t origin) {
    if (!graphId.empty() && origin != kInvalidOriginId) {
        graphOrigins_[graphId] = origin;
    }
}

void DebugMap::registerNodeOrigin(const std::string &nodeId, origin_id_t origin) {
    if (!nodeId.empty() && origin != kInvalidOriginId) {
        nodeOrigins_[nodeId] = origin;
    }
}

void DebugMap::registerPcOrigin(size_t pc, origin_id_t origin) {
    if (origin != kInvalidOriginId) {
        pcOrigins_[pc] = origin;
    }
}

void DebugMap::registerRuntimeGraphOrigin(uintptr_t runtimeGraphKey, origin_id_t origin) {
    if (runtimeGraphKey != 0 && origin != kInvalidOriginId) {
        runtimeGraphOrigins_[runtimeGraphKey] = origin;
    }
}

origin_id_t DebugMap::graphOrigin(const std::string &graphId) const {
    auto it = graphOrigins_.find(graphId);
    return it == graphOrigins_.end() ? kInvalidOriginId : it->second;
}

origin_id_t DebugMap::nodeOrigin(const std::string &nodeId) const {
    auto it = nodeOrigins_.find(nodeId);
    return it == nodeOrigins_.end() ? kInvalidOriginId : it->second;
}

origin_id_t DebugMap::pcOrigin(size_t pc) const {
    auto it = pcOrigins_.find(pc);
    return it == pcOrigins_.end() ? kInvalidOriginId : it->second;
}

origin_id_t DebugMap::runtimeGraphOrigin(uintptr_t runtimeGraphKey) const {
    auto it = runtimeGraphOrigins_.find(runtimeGraphKey);
    return it == runtimeGraphOrigins_.end() ? kInvalidOriginId : it->second;
}

void OriginSemanticMap::registerBundle(origin_id_t origin, SemanticBundle bundle) {
    if (origin == kInvalidOriginId) {
        return;
    }
    if (bundle.mainOrigin == kInvalidOriginId) {
        bundle.mainOrigin = origin;
    }
    bundles_[origin] = std::move(bundle);
}

const SemanticBundle *OriginSemanticMap::bundle(origin_id_t origin) const {
    auto it = bundles_.find(origin);
    return it == bundles_.end() ? nullptr : &it->second;
}

void EntitySemanticMap::registerBundle(const std::string &entityId, SemanticBundle bundle) {
    if (entityId.empty()) {
        return;
    }
    bundles_[entityId] = std::move(bundle);
}

const SemanticBundle *EntitySemanticMap::bundle(const std::string &entityId) const {
    auto it = bundles_.find(entityId);
    return it == bundles_.end() ? nullptr : &it->second;
}

source_file_id_t SourceContext::registerFile(const std::string &path, const std::string &content) {
    return manager_.registerFile(path, content);
}

std::optional<source_file_id_t> SourceContext::findFileId(const std::string &path) const {
    return manager_.findFileId(path);
}

span_id_t SourceContext::createSpan(source_file_id_t fileId, size_t startOffset, size_t endOffset) {
    return spans_.create(fileId, startOffset, endOffset);
}

span_id_t SourceContext::spanFromTokenRange(
    source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens, const TokenRange &range) {
    return spans_.fromTokenRange(fileId, tokens, range);
}

span_id_t SourceContext::spanFromToken(
    source_file_id_t fileId, const std::vector<antlr4::Token *> &tokens, antlr4::Token *token) {
    return spans_.fromToken(fileId, tokens, token);
}

origin_id_t SourceContext::createOrigin(
    span_id_t primarySpan, OriginStage stage, OriginKind kind, std::string label, bool synthetic,
    std::vector<origin_id_t> inputs) {
    return origins_
        .create(primarySpan, stage, kind, std::move(label), synthetic, std::move(inputs));
}

origin_id_t SourceContext::deriveOrigin(
    origin_id_t parent, OriginStage stage, OriginKind kind, std::string label,
    span_id_t primarySpan, bool synthetic, std::vector<origin_id_t> inputs) {
    return origins_
        .derive(parent, stage, kind, std::move(label), primarySpan, synthetic, std::move(inputs));
}

CharRange SourceContext::resolveSpan(span_id_t spanId) const {
    const SourceSpan *sp = spans_.span(spanId);
    if (!sp || !sp->valid()) {
        return {};
    }
    return {
        manager_.offsetToPos(sp->fileId, sp->startOffset),
        manager_.offsetToPos(sp->fileId, sp->endOffset)};
}

CharRange SourceContext::resolveOrigin(origin_id_t originId) const {
    const OriginRecord *rec = origins_.origin(originId);
    if (!rec) {
        return {};
    }
    // The current implementation defines an origin's effective source range as primarySpan.
    // Even when inputs contain multiple sources, external presentation still prefers the primary
    // span.
    return resolveSpan(rec->primarySpan);
}

const SourceFile *SourceContext::fileForSpan(span_id_t spanId) const {
    const SourceSpan *sp = spans_.span(spanId);
    if (!sp || !sp->valid()) {
        return nullptr;
    }
    return manager_.file(sp->fileId);
}

const SourceFile *SourceContext::fileForOrigin(origin_id_t originId) const {
    const OriginRecord *rec = origins_.origin(originId);
    if (!rec) {
        return nullptr;
    }
    return fileForSpan(rec->primarySpan);
}

std::string SourceContext::pathForSpan(span_id_t spanId) const {
    const SourceFile *file = fileForSpan(spanId);
    return file ? file->path : "";
}

std::string SourceContext::pathForOrigin(origin_id_t originId) const {
    const SourceFile *file = fileForOrigin(originId);
    return file ? file->path : "";
}

void SourceContext::registerAstSemantic(origin_id_t origin, SemanticBundle bundle) {
    astSemantic_.registerBundle(origin, std::move(bundle));
}

void SourceContext::registerGctSemantic(origin_id_t origin, SemanticBundle bundle) {
    gctSemantic_.registerBundle(origin, std::move(bundle));
}

void SourceContext::registerGirGraphSemantic(const std::string &graphId, SemanticBundle bundle) {
    girGraphs_.registerBundle(graphId, std::move(bundle));
}

const SemanticBundle *SourceContext::astSemantic(origin_id_t origin) const {
    return astSemantic_.bundle(origin);
}

const SemanticBundle *SourceContext::gctSemantic(origin_id_t origin) const {
    return gctSemantic_.bundle(origin);
}

const SemanticBundle *SourceContext::girGraphSemantic(const std::string &graphId) const {
    return girGraphs_.bundle(graphId);
}

const SemanticBundle *SourceContext::girNodeSemantic(const camel::compile::gir::Node *node) const {
    if (node == nullptr) {
        return nullptr;
    }
    if (origin_id_t o = girNodeDraftOrigin(node); o != kInvalidOriginId) {
        return girNodeByOrigin_.bundle(o);
    }
    return girNodes_.bundle(node->graph().nodeDebugEntityId(node));
}

void SourceContext::cloneGirGraphDebugInfo(
    const std::string &fromGraphId, const std::string &toGraphId) {
    if (fromGraphId.empty() || toGraphId.empty() || fromGraphId == toGraphId) {
        return;
    }
    if (auto origin = debugMap_.graphOrigin(fromGraphId); origin != kInvalidOriginId) {
        debugMap_.registerGraphOrigin(toGraphId, origin);
    }
    if (const auto *bundle = girGraphs_.bundle(fromGraphId)) {
        girGraphs_.registerBundle(toGraphId, *bundle);
    }
}

void SourceContext::bindGirNodeDraftDebug(
    const camel::compile::gir::Node *node, origin_id_t origin, SemanticBundle bundle) {
    if (node == nullptr || origin == kInvalidOriginId) {
        return;
    }
    girDraftNodeOrigins_[node] = origin;
    girNodeByOrigin_.registerBundle(origin, std::move(bundle));
}

void SourceContext::unbindGirNodeDraftDebug(const camel::compile::gir::Node *node) {
    if (node == nullptr) {
        return;
    }
    girDraftNodeOrigins_.erase(node);
}

origin_id_t SourceContext::girNodeDraftOrigin(const camel::compile::gir::Node *node) const {
    if (node == nullptr) {
        return kInvalidOriginId;
    }
    auto it = girDraftNodeOrigins_.find(node);
    return it == girDraftNodeOrigins_.end() ? kInvalidOriginId : it->second;
}

origin_id_t SourceContext::resolveGirNodeOrigin(const camel::compile::gir::Node *node) const {
    if (node == nullptr) {
        return kInvalidOriginId;
    }
    if (origin_id_t o = girNodeDraftOrigin(node); o != kInvalidOriginId) {
        return o;
    }
    return debugMap_.nodeOrigin(node->graph().nodeDebugEntityId(node));
}

void SourceContext::sealPromoteGirNodeDebug(
    const camel::compile::gir::Node *node, std::string entityId) {
    if (node == nullptr) {
        return;
    }
    origin_id_t o = girNodeDraftOrigin(node);
    if (o == kInvalidOriginId) {
        return;
    }
    debugMap_.registerNodeOrigin(entityId, o);
    if (const auto *bundle = girNodeByOrigin_.bundle(o)) {
        girNodes_.registerBundle(entityId, *bundle);
    }
}

void SourceContext::cloneGirNodeDebugBinding(
    const camel::compile::gir::Node *fromNode, const camel::compile::gir::Node *toNode) {
    if (fromNode == nullptr || toNode == nullptr || fromNode == toNode) {
        return;
    }
    origin_id_t o = resolveGirNodeOrigin(fromNode);
    if (o == kInvalidOriginId) {
        return;
    }
    const SemanticBundle *bundlePtr = girNodeSemantic(fromNode);
    SemanticBundle bundle;
    if (bundlePtr != nullptr) {
        bundle = *bundlePtr;
    } else {
        bundle.mainOrigin = o;
    }
    bindGirNodeDraftDebug(toNode, o, std::move(bundle));
}

void SourceContext::setCurrentRuntimeOrigin(origin_id_t origin) {
    std::lock_guard<std::mutex> lock(runtimeOriginMutex_);
    currentRuntimeOrigin_ = origin;
}

origin_id_t SourceContext::currentRuntimeOrigin() const {
    std::lock_guard<std::mutex> lock(runtimeOriginMutex_);
    return currentRuntimeOrigin_;
}

} // namespace camel::source
