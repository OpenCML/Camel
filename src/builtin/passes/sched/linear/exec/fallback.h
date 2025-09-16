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
 * Created: Sep. 08, 2025
 * Updated: Sep. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../linear.h"
#include "core/context/frame.h"

#include <stack>

class FallbackExecSchedPass : public LinearSchedPass {
    std::stack<std::pair<size_t, GIR::node_ptr_t>> brInfoStack_;
    std::unordered_map<GIR::Graph *, std::shared_ptr<GIR::node_vec_t>> graphTopoNodesCache_;

    data_ptr_t evalGraph(const GIR::graph_ptr_t &graph, frame_ptr_t &frame);
    std::shared_ptr<GIR::node_vec_t> getTopoNodes(const GIR::graph_ptr_t &graph);

  public:
    FallbackExecSchedPass(const context_ptr_t &ctx) : LinearSchedPass(ctx) {};
    virtual ~FallbackExecSchedPass() = default;

    virtual std::any apply(GIR::graph_ptr_t &graph) override {
        return apply(const_cast<const GIR::graph_ptr_t &>(graph));
    };
    virtual std::any apply(const GIR::graph_ptr_t &graph) override;
};