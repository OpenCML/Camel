// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/source/manager.h"

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

// The byte offset of each code point of `content`, plus one entry for the end of the text.
// Empty when the text is pure ASCII, where a code point index is its byte offset.
std::vector<uint32_t> buildCodePointOffsets(const std::string &content) {
    bool ascii = true;
    for (unsigned char c : content) {
        if (c >= 0x80) {
            ascii = false;
            break;
        }
    }
    if (ascii) {
        return {};
    }
    std::vector<uint32_t> offsets;
    offsets.reserve(content.size() + 1);
    for (size_t i = 0; i < content.size(); ++i) {
        // A continuation byte (10xxxxxx) belongs to the code point before it.
        if ((static_cast<unsigned char>(content[i]) & 0xC0) != 0x80) {
            offsets.push_back(static_cast<uint32_t>(i));
        }
    }
    offsets.push_back(static_cast<uint32_t>(content.size()));
    return offsets;
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
        SourceFile &file      = files_.at(it->second);
        file.content          = content;
        file.lineStarts       = buildLineStarts(content);
        file.codePointOffsets = buildCodePointOffsets(content);
        return file.id;
    }

    source_file_id_t id = static_cast<source_file_id_t>(files_.size());
    files_.push_back(
        SourceFile{
            .id               = id,
            .path             = path,
            .content          = content,
            .lineStarts       = buildLineStarts(content),
            .codePointOffsets = buildCodePointOffsets(content)});
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
    // The column counts characters (code points), as the positions of tokens do, so a
    // diagnostic placed by a span and one placed by a token agree on a line with non-ASCII
    // text.
    size_t col = 0;
    for (size_t i = src->lineStarts[line]; i < capped; ++i) {
        if ((static_cast<unsigned char>(src->content[i]) & 0xC0) != 0x80) {
            ++col;
        }
    }
    return {line, col};
}

size_t SourceManager::codePointToByteOffset(source_file_id_t fileId, size_t index) const {
    const SourceFile *src = file(fileId);
    if (!src || src->codePointOffsets.empty()) {
        return index;
    }
    return src->codePointOffsets[std::min(index, src->codePointOffsets.size() - 1)];
}

std::string_view SourceManager::lineText(source_file_id_t fileId, size_t line) const {
    const SourceFile *src = file(fileId);
    if (!src || line >= src->lineStarts.size()) {
        return {};
    }
    size_t begin = src->lineStarts[line];
    size_t end =
        line + 1 < src->lineStarts.size() ? src->lineStarts[line + 1] - 1 : src->content.size();
    if (end > begin && src->content[end - 1] == '\r') {
        --end;
    }
    return std::string_view(src->content).substr(begin, end - begin);
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

void DebugMap::registerGraphOrigin(camel::runtime::GraphId graph, origin_id_t origin) {
    if (camel::runtime::isValid(graph) && origin != kInvalidOriginId) {
        graphOrigins_[graph] = origin;
    }
}

void DebugMap::registerEntryOrigin(origin_id_t origin) {
    if (origin != kInvalidOriginId) {
        entryOrigin_ = origin;
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

origin_id_t DebugMap::graphOrigin(camel::runtime::GraphId graph) const {
    auto it = graphOrigins_.find(graph);
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

void OriginSemanticMap::registerBundle(origin_id_t origin, SemanticBundle bundle) {
    if (origin == kInvalidOriginId) {
        return;
    }
    if (bundle.mainOrigin == kInvalidOriginId) {
        bundle.mainOrigin = origin;
    }
    if (origin >= kDenseLimit) {
        sparse_[origin] = std::move(bundle);
        return;
    }
    const auto index = static_cast<size_t>(origin);
    if (index >= slots_.size()) {
        slots_.resize(index + 1, 0);
    }
    if (slots_[index] != 0) {
        bundles_[slots_[index] - 1] = std::move(bundle);
        return;
    }
    bundles_.push_back(std::move(bundle));
    slots_[index] = static_cast<uint32_t>(bundles_.size());
}

const SemanticBundle *OriginSemanticMap::bundle(origin_id_t origin) const {
    if (origin < slots_.size()) {
        const uint32_t slot = slots_[static_cast<size_t>(origin)];
        return slot == 0 ? nullptr : &bundles_[slot - 1];
    }
    if (sparse_.empty()) {
        return nullptr;
    }
    auto it = sparse_.find(origin);
    return it == sparse_.end() ? nullptr : &it->second;
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

span_id_t
SourceContext::createSpan(source_file_id_t fileId, size_t startOffset, size_t endOffset) {
    return spans_.create(fileId, startOffset, endOffset);
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

void SourceContext::registerGirGraphSemantic(camel::runtime::GraphId graph, SemanticBundle bundle) {
    if (camel::runtime::isValid(graph)) {
        girGraphs_[graph] = std::move(bundle);
    }
}

const SemanticBundle *SourceContext::astSemantic(origin_id_t origin) const {
    return astSemantic_.bundle(origin);
}

const SemanticBundle *SourceContext::gctSemantic(origin_id_t origin) const {
    return gctSemantic_.bundle(origin);
}

const SemanticBundle *SourceContext::girGraphSemantic(camel::runtime::GraphId graph) const {
    auto it = girGraphs_.find(graph);
    return it == girGraphs_.end() ? nullptr : &it->second;
}

const SemanticBundle *SourceContext::girNodeSemantic(const std::string &entityId) const {
    if (entityId.empty()) {
        return nullptr;
    }
    return girNodes_.bundle(entityId);
}

void SourceContext::cloneGirGraphDebugInfo(
    camel::runtime::GraphId from, camel::runtime::GraphId to) {
    if (!camel::runtime::isValid(from) || !camel::runtime::isValid(to) || from == to) {
        return;
    }
    if (auto origin = debugMap_.graphOrigin(from); origin != kInvalidOriginId) {
        debugMap_.registerGraphOrigin(to, origin);
    }
    if (const auto *bundle = girGraphSemantic(from)) {
        girGraphs_[to] = *bundle;
    }
}

void SourceContext::bindGirNodeDraftDebug(
    gir_draft_node_key_t draftNodeKey, origin_id_t origin, SemanticBundle bundle) {
    if (draftNodeKey == 0 || origin == kInvalidOriginId) {
        return;
    }
    girDraftNodeOrigins_[draftNodeKey] = origin;
    girNodeByOrigin_.registerBundle(origin, std::move(bundle));
}

void SourceContext::unbindGirNodeDraftDebug(gir_draft_node_key_t draftNodeKey) {
    if (draftNodeKey == 0) {
        return;
    }
    girDraftNodeOrigins_.erase(draftNodeKey);
}

origin_id_t SourceContext::girNodeDraftOrigin(gir_draft_node_key_t draftNodeKey) const {
    if (draftNodeKey == 0) {
        return kInvalidOriginId;
    }
    auto it = girDraftNodeOrigins_.find(draftNodeKey);
    return it == girDraftNodeOrigins_.end() ? kInvalidOriginId : it->second;
}

origin_id_t SourceContext::resolveGirNodeOrigin(
    gir_draft_node_key_t draftNodeKey, const std::string &entityId) const {
    if (origin_id_t o = girNodeDraftOrigin(draftNodeKey); o != kInvalidOriginId) {
        return o;
    }
    if (!entityId.empty()) {
        return debugMap_.nodeOrigin(entityId);
    }
    return kInvalidOriginId;
}

void SourceContext::sealPromoteGirNodeDebug(
    gir_draft_node_key_t draftNodeKey, std::string entityId) {
    if (draftNodeKey == 0) {
        return;
    }
    origin_id_t o = girNodeDraftOrigin(draftNodeKey);
    if (o == kInvalidOriginId) {
        return;
    }
    debugMap_.registerNodeOrigin(entityId, o);
    if (const auto *bundle = girNodeByOrigin_.bundle(o)) {
        girNodes_.registerBundle(entityId, *bundle);
    }
}

void SourceContext::cloneGirNodeDebugBinding(
    gir_draft_node_key_t fromKey, const std::string &fromEntityId, gir_draft_node_key_t toKey) {
    if (fromKey == 0 || toKey == 0 || fromKey == toKey) {
        return;
    }
    origin_id_t o = resolveGirNodeOrigin(fromKey, fromEntityId);
    if (o == kInvalidOriginId) {
        return;
    }
    const SemanticBundle *bundlePtr = girNodeDraftOrigin(fromKey) != kInvalidOriginId
                                          ? girNodeByOrigin_.bundle(o)
                                          : girNodeSemantic(fromEntityId);
    SemanticBundle bundle;
    if (bundlePtr != nullptr) {
        bundle = *bundlePtr;
    } else {
        bundle.mainOrigin = o;
    }
    bindGirNodeDraftDebug(toKey, o, std::move(bundle));
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
