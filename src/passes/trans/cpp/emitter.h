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
 * Created: Mar. 11, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "bridge.h"
#include "camel/core/context/context.h"
#include "lower.h"
#include "naming.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class CppEmitter {
  public:
    CppEmitter(const camel::core::context::context_ptr_t &ctx, CppEmitMode mode)
        : context_(ctx), mode_(mode) {}

    std::string emit(const GIR::graph_ptr_t &root);

  private:
    using expr_cache_t = std::unordered_map<GIR::Node *, std::string>;

    GIR::graph_ptr_t resolveTargetGraph(const GIR::graph_ptr_t &root) const;
    std::vector<GIR::Graph *> collectEmissionRoots(GIR::Graph *entry) const;
    std::vector<GIR::Graph *> collectEmissionOrder(const std::vector<GIR::Graph *> &entries) const;
    bool isExecutableMode() const;
    bool isInspectMode() const;
    bool shouldEmitStatementBody(const GraphLoweringPlan &plan) const;
    std::string emitDirectFunction(GIR::Graph *graph, const GraphLoweringPlan &plan);
    std::string emitForwardDeclarations(const std::vector<GraphLoweringPlan> &plans);
    std::string emitFunctionSignature(GIR::Graph *graph, bool forceInline);
    std::string emitBindingSymbol(GIR::Node *node, std::optional<size_t> stmtIndex = std::nullopt);
    std::string emitExpr(GIR::Node *node, expr_cache_t &cache);
    std::string emitOperatorExpr(GIR::OperNode *node, expr_cache_t &cache);
    std::string emitRuntimeFallbackExpr(GIR::OperNode *node, expr_cache_t &cache);
    std::string emitTypeAccessorRef(camel::core::type::Type *type) const;
    std::string emitCommentary(const GraphLoweringPlan &plan) const;
    std::string emitProgramMain(GIR::Graph *graph, const CppBridgePlan *bridgePlan = nullptr);
    std::string emitPlainUnit(GIR::Graph *graph, const std::vector<GraphLoweringPlan> &plans);
    std::string emitModuleUnit(GIR::Graph *graph, const std::vector<GraphLoweringPlan> &plans);

    camel::core::context::context_ptr_t context_;
    CppEmitMode mode_;
    CppSymbolNamer namer_;
    const CppBridgePlan *bridgePlan_ = nullptr;
};
