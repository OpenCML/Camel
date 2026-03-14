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
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/execute/pass/sched.h"
#include "compile.h"

#include "jit/jit_config.h"

#if ENABLE_FASTVM_JIT
#include "jit/jit.h"
#include "jit/runtime/trampoline.h"
#include "jit/tier/tier_policy.h"
#include <mutex>
namespace jit = camel::jit;
#endif

namespace ctx = camel::core::context;

struct FastVMConfig {
    enum class JitMode {
        Disabled,
        OnDemand,
        Always,
    };

    JitMode jitMode        = JitMode::Disabled;
    size_t jitHotThreshold = 1;
    bool enableJitTraceMir = false;
};

class FastVMSchedPass : public GraphSchedulePass {
  public:
    struct CallResult {
        slot_t result;
        ctx::Frame *rootFrame;
    };

  private:
    inline static const size_t maxRecursionDepth_ = 256; // default max recursion depth

    // 栈帧池
    ctx::FramePool framePool_{1 * camel::core::mm::MB};

    bytecode_vec_t bytecodes_;
    std::unordered_map<GIR::Graph *, size_t> offsetMap_;

#if ENABLE_FASTVM_JIT
    std::unique_ptr<jit::IJitBackend> jitBackend_;
    std::unordered_map<jit::JitEntryFn, GIR::Graph *> jitFnToGraph_;
    std::mutex jitCacheMutex_;
    jit::JitConfig jitConfig_{};
    jit::TierPolicy tierPolicy_{jitConfig_};
    bool enableJitTraceMir_ = false;
    void *currentJitCtx_{}; // 供解释器 FUNC/TAIL 调用 invokeCallOrJit 时使用
    void compileAndCacheGraph(GIR::Graph *graph, size_t entryPc);

    template <typename ReadSlotFn>
    void populateCallFrame(
        ctx::Frame *frame, const data_idx_t *args, size_t argsCnt, ReadSlotFn &&readSlot) {
        for (size_t i = 0; i < argsCnt; ++i) {
            frame->set(i + 1, readSlot(args[i]));
        }
    }

    template <typename ReadSlotFn>
    ctx::Frame *acquireCallFrameWithArgs(
        GIR::Graph *graph, const data_idx_t *args, size_t argsCnt, ReadSlotFn &&readSlot) {
        ctx::Frame *frame = framePool_.acquire(graph);
        populateCallFrame(frame, args, argsCnt, std::forward<ReadSlotFn>(readSlot));
        return frame;
    }

    template <typename ReadSlotFn>
    ctx::Frame *acquireTailFrameWithArgs(
        GIR::Graph *graph, const data_idx_t *args, size_t argsCnt, ReadSlotFn &&readSlot) {
        ctx::Frame *frame = framePool_._acquire(graph);
        populateCallFrame(frame, args, argsCnt, std::forward<ReadSlotFn>(readSlot));
        framePool_._resetTop();
        return frame;
    }

    Bytecode *materializeCallTarget(size_t pc, Bytecode *bc);
#endif

    // 程序计数器栈和栈帧栈
    std::vector<size_t> pcStack_{maxRecursionDepth_};
    std::vector<ctx::Frame *> frameStack_{maxRecursionDepth_};

    void precompile(GIR::Graph *graph);

    void push(size_t pc, ctx::Frame *frame);
    std::pair<size_t, ctx::Frame *> pop();

    void evalMarkedOperator(
        const MarkOpCode op, data_idx_t self, data_arr_t nargs, data_arr_t wargs,
        ctx::Frame &currFrame);

    void evalMarkedOperator_map_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, ctx::Frame &currFrame);
    void evalMarkedOperator_apply_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, ctx::Frame &currFrame);
    void evalMarkedOperator_filter_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, ctx::Frame &currFrame);
    void evalMarkedOperator_reduce_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, ctx::Frame &currFrame);
    void evalMarkedOperator_foreach_arr(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, ctx::Frame &currFrame);

  public:
    FastVMSchedPass(const ctx::context_ptr_t &ctx, const FastVMConfig &config = {})
        : GraphSchedulePass(ctx)
#if ENABLE_FASTVM_JIT
          ,
          jitConfig_([&config]() {
              jit::JitConfig c{};
              switch (config.jitMode) {
              case FastVMConfig::JitMode::Disabled:
                  c.policy = jit::JitPolicy::Disabled;
                  break;
              case FastVMConfig::JitMode::OnDemand:
                  c.policy = jit::JitPolicy::OnDemand;
                  break;
              case FastVMConfig::JitMode::Always:
                  c.policy = jit::JitPolicy::Always;
                  break;
              }
              c.hotThreshold = config.jitHotThreshold;
              return c;
          }()),
          enableJitTraceMir_(config.enableJitTraceMir)
#endif
    {
    }
    virtual ~FastVMSchedPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;

    CallResult callBorrowed(size_t pc, ctx::Frame *rootFrame);
    slot_t call(size_t pc, ctx::Frame *rootFrame);
    size_t graphEntryPc(GIR::Graph *graph) const;

#if ENABLE_FASTVM_JIT
    inline ctx::Frame *acquireFrameForCall(GIR::Graph *graph) { return framePool_.acquire(graph); }
    inline void releaseFrameForCall(ctx::Frame *frame) {
        if (framePool_.isActive(frame))
            framePool_.release(frame);
    }
    inline void releaseFrameUnchecked(ctx::Frame *frame) { framePool_.release(frame); }
    inline void releaseFrameForCall(ctx::Frame *frame, GIR::Graph *owner) {
        if (framePool_.isActive(frame, owner))
            framePool_.release(frame);
    }
    inline ctx::Frame *acquireFrameForTail(GIR::Graph *graph) {
        ctx::Frame *f = framePool_._acquire(graph);
        framePool_._resetTop();
        return f;
    }
    inline void releaseFrameForTail(ctx::Frame *frame) {
        if (framePool_.isActive(frame))
            framePool_.release(frame);
    }
    slot_t invokeOwnedJitFrame(jit::JitEntryFn fn, ctx::Frame *frame, void *jitCtx);
    inline ctx::Context &context() { return *context_; }
    GIR::Graph *jitFnToGraph(jit::JitEntryFn fn) const;
    slot_t invokeCallOrJit(
        size_t pc, GIR::Graph *graph, ctx::Frame *frame, void *jitCtx, uint32_t callCount = 0);
#endif
};
