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
 * Updated: Oct. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../linear.h"
#include "core/context/frame.h"

#include <stack>

class NodeVMSchedPass : public LinearSchedPass {
    static const size_t maxRecursionDepth_ = 64; // default max recursion depth
    size_t currRecursionDepth_ = 0;
    std::stack<GraphIR::node_ptr_t> brInfoStack_;
    std::unordered_map<GraphIR::Graph *, std::shared_ptr<GraphIR::node_vec_t>> graphTopoNodesCache_;

    data_ptr_t evalGraph(GraphIR::Graph *graph, Frame &frame);
    std::shared_ptr<GraphIR::node_vec_t> getTopoNodes(GraphIR::Graph *graph);

    void
    evalMarkedOperator(const std::string uri, const GraphIR::node_ptr_t &node, Frame &currFrame);

    void evalMarkedOperator_map_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_apply_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_filter_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_reduce_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);
    void evalMarkedOperator_foreach_arr(const GraphIR::node_ptr_t &node, Frame &currFrame);

  public:
    NodeVMSchedPass(const context_ptr_t &ctx) : LinearSchedPass(ctx) {};
    virtual ~NodeVMSchedPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
