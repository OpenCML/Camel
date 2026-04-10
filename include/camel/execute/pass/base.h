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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"

class GraphIRPass {
  protected:
    camel::core::context::context_ptr_t context_;

  public:
    GraphIRPass(const camel::core::context::context_ptr_t &ctx) : context_(ctx) {};
    virtual ~GraphIRPass() = default;

    // Compile finishes before pass execution starts. The post-compile pipeline
    // therefore carries runtime GCGraph objects exclusively.
    virtual camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) = 0;
};

class NullGraphIRPass : public GraphIRPass {
  public:
    NullGraphIRPass(const camel::core::context::context_ptr_t &ctx) : GraphIRPass(ctx) {};
    virtual ~NullGraphIRPass() = default;

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

enum class PassApplyStatus {
    Transformed,
    Consumed,
    Failed,
};

struct PassApplyResult {
    camel::runtime::GCGraph *graph = nullptr;
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
    camel::runtime::GCGraph *graph, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);

/** Backward-compatible wrapper: only returns the graph payload, without status. */
camel::runtime::GCGraph *applyPasses(
    camel::runtime::GCGraph *graph, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);
