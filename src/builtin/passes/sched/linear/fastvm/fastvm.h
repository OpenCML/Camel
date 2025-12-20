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
 * Updated: Dec. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../linear.h"
#include "core/context/frame.h"
#include "precompile.h"

#define ENABLE_COMPUTED_GOTO

class FastVMSchedPass : public LinearSchedPass {
    inline static const size_t maxRecursionDepth_ = 256; // default max recursion depth

    // 栈帧池
    FramePool framePool_{1 * MB};

    bytecode_vec_t bytecodes_;
    std::unordered_map<GraphIR::Graph *, size_t> offsetMap_;

    // 程序计数器栈和栈帧栈
    std::vector<size_t> pcStack_{maxRecursionDepth_};
    std::vector<Frame *> frameStack_{maxRecursionDepth_};

    void precompile(GraphIR::Graph *graph) {
        auto [bytecodes, _, offsetMap] = compileAndLink(
            context_,
            graph,
            {
                .enableTailCallDetection = true,
                .enableInlineOperators   = true,
                .optimizationStrategies  = OptimizationStrategyCode::All,
            });
        bytecodes_ = std::move(bytecodes);
        offsetMap_ = std::move(offsetMap);
    }

    slot_t call(size_t pc, Frame *rootFrame);

    inline void push(size_t pc, Frame *frame) {
        pcStack_.push_back(pc);
        frameStack_.push_back(frame);
        if (frameStack_.size() >= maxRecursionDepth_) {
            context_->rtmDiags()
                ->of(RuntimeDiag::MaxRecursionDepthExceeded)
                .commit(frame->graph()->name(), maxRecursionDepth_);
        }
    }
    inline std::pair<size_t, Frame *> pop() {
        size_t pc = pcStack_.back();
        pcStack_.pop_back();
        Frame *frame = frameStack_.back();
        frameStack_.pop_back();
        return {pc, frame};
    }

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
