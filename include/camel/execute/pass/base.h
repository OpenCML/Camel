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
 * Updated: Mar. 09, 2026
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

/** Applies passes to the given graph. Returns result graph, or nullptr on error (check ctx
 * rtmDiags). */
GIR::graph_ptr_t applyPasses(
    GIR::graph_ptr_t graph, const std::vector<std::string> &passes,
    const camel::core::context::context_ptr_t &ctx, std::ostream &os);
