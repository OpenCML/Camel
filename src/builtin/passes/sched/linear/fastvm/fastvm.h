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
 * Updated: Nov. 02, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../linear.h"
#include "builtin/passes/sched/common/bytecode.h"
#include "core/context/frame.h"

#include <stack>

class FastVMSchedPass : public LinearSchedPass {
    inline static const size_t maxRecursionDepth_ = 128; // default max recursion depth
    size_t currRecursionDepth_ = 0;
    std::unordered_map<GraphIR::Graph *, std::shared_ptr<bytecode_vec_t>> compiledCodesMap_;

    data_ptr_t call(GraphIR::Graph *graph, Frame &frame);
    std::shared_ptr<bytecode_vec_t> getCodeOfGraph(GraphIR::Graph *graph);

    void evalMarkedOperator(
        const MarkOpCode op, data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame);

    void evalMarkedOperator_map_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame);
    void evalMarkedOperator_apply_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame);
    void evalMarkedOperator_filter_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame);
    void evalMarkedOperator_reduce_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame);
    void evalMarkedOperator_foreach_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame);

  public:
    FastVMSchedPass(const context_ptr_t &ctx) : LinearSchedPass(ctx) {};
    virtual ~FastVMSchedPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
