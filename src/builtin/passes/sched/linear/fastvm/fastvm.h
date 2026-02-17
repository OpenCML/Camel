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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../linear.h"
#include "compile.h"
#include "core/context/frame.h"

#include "jit/jit_config.h"

#if ENABLE_FASTVM_JIT
#include "jit/jit.h"
#include "jit/runtime/trampoline.h"
#include "jit/tier/tier_policy.h"
#include <mutex>
#endif

struct FastVMConfig {
    bool enableJit = false;
};

class FastVMSchedPass : public LinearSchedPass {
    inline static const size_t maxRecursionDepth_ = 256; // default max recursion depth

    // 栈帧池
    FramePool framePool_{1 * MB};

    bytecode_vec_t bytecodes_;
    std::unordered_map<GraphIR::Graph *, size_t> offsetMap_;

#if ENABLE_FASTVM_JIT
    std::unique_ptr<camel::jit::IJitBackend> jitBackend_;
    std::unordered_map<GraphIR::Graph *, camel::jit::JitEntryFn> jitCache_;
    std::unordered_map<camel::jit::JitEntryFn, GraphIR::Graph *> jitFnToGraph_;
    std::mutex jitCacheMutex_;
    camel::jit::JitConfig jitConfig_{};
    camel::jit::TierPolicy tierPolicy_{jitConfig_};
    void *currentJitCtx_{}; // 供解释器 FUNC/TAIL 调用 invokeCallOrJit 时使用
    void compileAndCacheGraph(GraphIR::Graph *graph, size_t entryPc);
#endif

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
    FastVMSchedPass(const context_ptr_t &ctx, const FastVMConfig &config = {})
        : LinearSchedPass(ctx)
#if ENABLE_FASTVM_JIT
          ,
          jitConfig_([&config]() {
              camel::jit::JitConfig c{};
              c.policy = config.enableJit ? camel::jit::JitPolicy::OnDemand
                                          : camel::jit::JitPolicy::Disabled;
              return c;
          }())
#endif
    {
    }
    virtual ~FastVMSchedPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;

    slot_t call(size_t pc, Frame *rootFrame);

#if ENABLE_FASTVM_JIT
    Frame *acquireFrameForCall(GraphIR::Graph *graph) { return framePool_.acquire(graph); }
    void releaseFrameForCall(Frame *frame) { framePool_.release(frame); }
    Frame *acquireFrameForTail(GraphIR::Graph *graph) {
        Frame *f = framePool_._acquire(graph);
        framePool_._resetTop();
        return f;
    }
    void releaseFrameForTail(Frame *frame) { framePool_.release(frame); }
    Context &context() { return *context_; }
    GraphIR::Graph *jitFnToGraph(camel::jit::JitEntryFn fn) const {
        auto it = jitFnToGraph_.find(fn);
        return it != jitFnToGraph_.end() ? it->second : nullptr;
    }
    slot_t invokeCallOrJit(
        size_t pc, GraphIR::Graph *graph, Frame *frame, void *jitCtx, uint32_t callCount = 0);
#endif
};
