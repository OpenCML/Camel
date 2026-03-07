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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/source/manager.h"

#include <algorithm>

namespace camel::source {

namespace {

// 预计算每一行的起始偏移，后续把任意 offset 转回 line/column 时只需要做一次 upper_bound。
std::vector<size_t> buildLineStarts(const std::string &content) {
    std::vector<size_t> lineStarts = {0};
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lineStarts.push_back(i + 1);
        }
    }
    return lineStarts;
}

// 这里把 token 半开区间转换成源码字节区间。
// GCT/GIR 不再直接依赖 token，因此 token 只在前端边界被消费一次。
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
    // 若派生节点未显式指定 span，则沿主派生链继承父节点的主区间。
    // 这就是当前 GCT/GIR 在“有损 lowering”下仍能回到源码的大前提。
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
    // 当前实现把 origin 的“实际源码区间”定义为 primarySpan。
    // 即使 inputs 中有多源输入，对外展示仍优先返回主区间。
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

void SourceContext::setCurrentRuntimeOrigin(origin_id_t origin) {
    std::lock_guard<std::mutex> lock(runtimeOriginMutex_);
    currentRuntimeOrigin_ = origin;
}

origin_id_t SourceContext::currentRuntimeOrigin() const {
    std::lock_guard<std::mutex> lock(runtimeOriginMutex_);
    return currentRuntimeOrigin_;
}

} // namespace camel::source
