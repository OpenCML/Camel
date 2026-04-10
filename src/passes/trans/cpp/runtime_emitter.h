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
 * Created: Apr. 10, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bridge.h"
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct RuntimeCppLoweringIssue {
    camel::runtime::GCGraph *graph        = nullptr;
    camel::runtime::gc_node_ref_t nodeRef = camel::runtime::kInvalidNodeRef;
    std::string reason;
};

struct RuntimeCppGraphPlan {
    camel::runtime::GCGraph *graph = nullptr;
    std::vector<camel::runtime::gc_node_ref_t> topoNodes;
    std::unordered_set<camel::runtime::gc_node_ref_t> inlineOnlyNodes;
    bool directCallable = false;
    std::vector<RuntimeCppLoweringIssue> issues;
};

class RuntimeCppEmitter {
  public:
    RuntimeCppEmitter(const camel::core::context::context_ptr_t &ctx, CppEmitMode mode)
        : context_(ctx), mode_(mode) {}

    std::string emit(camel::runtime::GCGraph *root);

  private:
    using expr_cache_t = std::unordered_map<camel::runtime::gc_node_ref_t, std::string>;

    void assignGraphSymbols(const std::vector<camel::runtime::GCGraph *> &graphs);
    camel::runtime::GCGraph *resolveTargetGraph(camel::runtime::GCGraph *root) const;
    std::vector<camel::runtime::GCGraph *>
    collectEmissionRoots(camel::runtime::GCGraph *entry) const;
    std::vector<camel::runtime::GCGraph *>
    collectEmissionOrder(const std::vector<camel::runtime::GCGraph *> &entries) const;
    RuntimeCppGraphPlan analyzeGraph(camel::runtime::GCGraph *graph) const;
    CppBridgePlan collectBridgePlan(const std::vector<RuntimeCppGraphPlan> &plans) const;

    bool isExecutableMode() const;
    bool isInspectMode() const;
    bool shouldEmitStatementBody(const RuntimeCppGraphPlan &plan) const;

    std::string emitForwardDeclarations(const std::vector<RuntimeCppGraphPlan> &plans);
    std::string emitDirectFunction(camel::runtime::GCGraph *graph, const RuntimeCppGraphPlan &plan);
    std::string emitFunctionSignature(camel::runtime::GCGraph *graph, bool forceInline);
    std::string emitBindingSymbol(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        std::optional<size_t> stmtIndex = std::nullopt);
    std::string emitExpr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, expr_cache_t &cache,
        const RuntimeCppGraphPlan &plan);
    std::string emitOperatorExpr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, expr_cache_t &cache,
        const RuntimeCppGraphPlan &plan);
    std::string emitRuntimeFallbackExpr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, expr_cache_t &cache,
        const RuntimeCppGraphPlan &plan);
    std::string emitCommentary(const RuntimeCppGraphPlan &plan) const;
    std::string emitProgramMain(
        camel::runtime::GCGraph *root, camel::runtime::GCGraph *graph,
        const CppBridgePlan *bridgePlan = nullptr);
    std::string emitPlainUnit(
        camel::runtime::GCGraph *root, camel::runtime::GCGraph *graph,
        const std::vector<RuntimeCppGraphPlan> &plans);
    std::string
    emitModuleUnit(camel::runtime::GCGraph *graph, const std::vector<RuntimeCppGraphPlan> &plans);
    std::string
    emitExecutableEntryCall(camel::runtime::GCGraph *root, camel::runtime::GCGraph *graph);
    std::vector<camel::runtime::gc_node_ref_t>
    collectStatementNodes(const RuntimeCppGraphPlan &plan) const;
    std::string graphSymbol(camel::runtime::GCGraph *graph) const;
    std::string joinReasons(const std::vector<RuntimeCppLoweringIssue> &issues) const;

    camel::core::context::context_ptr_t context_;
    CppEmitMode mode_;
    std::unordered_map<const camel::runtime::GCGraph *, std::string> graphNames_;
    std::unordered_map<uintptr_t, std::string> valueNames_;
    std::unordered_set<std::string> usedNames_;
    const CppBridgePlan *bridgePlan_ = nullptr;
};
