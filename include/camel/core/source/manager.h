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

namespace camel::source {

/// 标记一个 origin 来自哪个编译阶段，用于诊断与 debugger 展示派生路径。
enum class OriginStage : uint8_t {
    Parser,
    AST,
    GCT,
    GIR,
    Bytecode,
    Runtime,
    Synthetic,
};

/// 标记 origin 代表的实体类别。一个实体的“位置”与“语义身份”由 stage + kind 共同描述。
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

/// 语义部件角色。
/// 这些角色不替代 primarySpan，而是为“主区间 + 语义部件”逆映射提供统一命名。
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

/// 单个源文件的规范化缓存。这里保存完整文本和行起始偏移，供 offset -> line/column 查询。
struct SourceFile {
    source_file_id_t id = kInvalidSourceFileId;
    std::string path;                     // 逻辑路径或绝对路径，作为 debugger/LSP 的文件标识。
    std::string content;                  // 源文件原文，span 的 offset 一律相对它计算。
    std::vector<size_t> lineStarts = {0}; // 每行首字符的字节偏移，用于快速定位行列。
};

/// 规范化后的源码区间。所有阶段都通过 SpanId 间接引用它，而不是复制 CharRange。
struct SourceSpan {
    span_id_t id            = kInvalidSpanId;
    source_file_id_t fileId = kInvalidSourceFileId;
    size_t startOffset      = 0; // 半开区间起点。
    size_t endOffset        = 0; // 半开区间终点。

    bool valid() const { return id != kInvalidSpanId && fileId != kInvalidSourceFileId; }
};

/// 跨阶段保留“这个实体从哪段源码而来”的统一记录。
/// parent 表示主派生链；inputs 为可选的多源输入集合。
struct OriginRecord {
    origin_id_t id        = kInvalidOriginId;
    span_id_t primarySpan = kInvalidSpanId;   // 当前实体对外展示的主源码区间。
    origin_id_t parent    = kInvalidOriginId; // 主派生来源，如 GCT 节点来自某个 AST 节点。
    OriginStage stage     = OriginStage::Synthetic;
    OriginKind kind       = OriginKind::Synthetic;
    bool synthetic        = false;   // true 表示该实体不是源码中直接出现的结构。
    std::string label;               // 便于调试的阶段标签，如 gct.data / gir.cast。
    std::vector<origin_id_t> inputs; // 多源降糖或合并场景下的附加来源。
};

/// 某个语义部件的命名来源。
/// origin 可以指向 token 级锚点、子表达式来源，或更低层的 lowering 产物来源。
struct SemanticPart {
    SemanticRole role  = SemanticRole::Whole;
    origin_id_t origin = kInvalidOriginId;
    int32_t slot       = -1; // 参数位、分支位等按序编号；无编号时为 -1。
    std::string label;       // 例如 "lhs"、"rhs"、"else"、"kwargs"。
};

/// 一个执行实体的语义来源包。
/// mainOrigin 继续提供稳定 fallback，parts / mergedInputs 则用于更细的调试与逆映射。
struct SemanticBundle {
    origin_id_t mainOrigin = kInvalidOriginId;
    std::vector<SemanticPart> parts;
    std::vector<origin_id_t> mergedInputs;
    bool synthetic = false;
    std::string syntheticReason;
};

/// 源文件注册表与行列查询器。
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

/// Span 的集中存储区。Span 本身不带阶段语义，只表达“哪一段源码”。
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

/// Origin 的集中存储区。create 用于创建根来源，derive 用于沿编译链派生新实体。
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

/// debugger 查询用的轻量索引。
/// 这里不保存 span，只保存稳定执行实体 -> OriginId 的映射。
class DebugMap {
  public:
    void registerGraphOrigin(const std::string &graphId, origin_id_t origin);
    void registerNodeOrigin(const std::string &nodeId, origin_id_t origin);
    void registerPcOrigin(size_t pc, origin_id_t origin);

    origin_id_t graphOrigin(const std::string &graphId) const;
    origin_id_t nodeOrigin(const std::string &nodeId) const;
    origin_id_t pcOrigin(size_t pc) const;

  private:
    std::unordered_map<std::string, origin_id_t> graphOrigins_; // graph stableId -> origin
    std::unordered_map<std::string, origin_id_t> nodeOrigins_;  // node stableId -> origin
    std::unordered_map<size_t, origin_id_t> pcOrigins_;         // bytecode pc -> origin
};

/// 以 OriginId 为键的语义 side table，主要服务 AST/GCT 这类树阶段。
class OriginSemanticMap {
  public:
    void registerBundle(origin_id_t origin, SemanticBundle bundle);
    const SemanticBundle *bundle(origin_id_t origin) const;

  private:
    std::unordered_map<origin_id_t, SemanticBundle> bundles_;
};

/// 以稳定执行实体 id 为键的语义 side table，主要服务 GIR graph/node 与 debugger。
class EntitySemanticMap {
  public:
    void registerBundle(const std::string &entityId, SemanticBundle bundle);
    const SemanticBundle *bundle(const std::string &entityId) const;

  private:
    std::unordered_map<std::string, SemanticBundle> bundles_;
};

/// 编译期和运行时共享的源码映射上下文。
/// 这是整套系统的统一入口：文件、span、origin、debug map 和当前执行点都挂在这里。
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
    void registerGirNodeSemantic(const std::string &nodeId, SemanticBundle bundle);

    const SemanticBundle *astSemantic(origin_id_t origin) const;
    const SemanticBundle *gctSemantic(origin_id_t origin) const;
    const SemanticBundle *girGraphSemantic(const std::string &graphId) const;
    const SemanticBundle *girNodeSemantic(const std::string &nodeId) const;

    void setCurrentRuntimeOrigin(origin_id_t origin);
    origin_id_t currentRuntimeOrigin() const;

  private:
    SourceManager manager_;         // 负责源文件与行列换算。
    SpanArena spans_;               // 负责源码区间存储。
    OriginTable origins_;           // 负责跨阶段派生关系。
    DebugMap debugMap_;             // 负责运行时/调试器快速反查。
    OriginSemanticMap astSemantic_; // AST 语义锚点 side table。
    OriginSemanticMap gctSemantic_; // GCT lowering 语义来源 side table。
    EntitySemanticMap girGraphs_;   // GIR graph 级语义来源 side table。
    EntitySemanticMap girNodes_;    // GIR node 级语义来源 side table。

    mutable std::mutex runtimeOriginMutex_;
    origin_id_t currentRuntimeOrigin_ = kInvalidOriginId; // 当前线程最近一次执行到的源码来源。
};

using source_context_ptr_t = std::shared_ptr<SourceContext>;

} // namespace camel::source
