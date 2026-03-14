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
 * Created: Oct. 21, 2024
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/compile/gir.h"
#include "camel/core/context/context.h"

class GraphIRPass {
  protected:
    camel::core::context::context_ptr_t context_;

  public:
    GraphIRPass(const camel::core::context::context_ptr_t &ctx) : context_(ctx) {};
    virtual ~GraphIRPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) = 0;
};

class NullGraphIRPass : public GraphIRPass {
  protected:
    camel::core::context::context_ptr_t context_;

  public:
    NullGraphIRPass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~NullGraphIRPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;
};

enum class PassApplyStatus {
    Transformed,
    Consumed,
    Failed,
};

struct PassApplyResult {
    GIR::graph_ptr_t graph;
    PassApplyStatus status{PassApplyStatus::Transformed};

    bool failed() const { return status == PassApplyStatus::Failed; }
    bool consumed() const { return status == PassApplyStatus::Consumed; }
};

/**
 * Applies passes to the given graph.
 *
 * Return value contract:
 * - `PassApplyStatus::Failed`: pass execution failed; caller should inspect `ctx->rtmDiags()`
 * - `PassApplyStatus::Consumed`: execution/scheduling finished successfully and consumed the graph
 * - non-null graph: pass transformed the graph and left it for later passes
 */
PassApplyResult applyPassesDetailed(
    GIR::graph_ptr_t graph, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);

/** Backward-compatible wrapper: only returns the graph payload, without status. */
GIR::graph_ptr_t applyPasses(
    GIR::graph_ptr_t graph, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);
