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
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/execute/limits.h"
#include "camel/execute/pass/runtime_sched.h"
#include "camel/runtime/graph.h"
#include "compile.h"
#include <array>
#include <memory>
#include <vector>

#include "jit/jit_config.h"

#if ENABLE_FASTVM_JIT
#include "jit/jit.h"
#include "jit/runtime/trampoline.h"
#include "jit/tier/tier_policy.h"
#include <mutex>
namespace jit = camel::jit;
#endif

namespace ctx = camel::core::context;

/**
 * GC safepoint for FVM call transitions (CALL / FUNC / TAIL). FVM does not poll
 * per bytecode; at a call boundary every live value sits in a FramePool frame
 * (a registered root), so a pending collection can run here. Every call checks
 * the global slow-path flag (one load): tensor-heavy code allocates most of its
 * memory in a few calls, and a collection delayed by even a handful of them lets
 * the heap grow to several times what NodeVM needs for the same program.
 */
inline void fastVmCallBoundarySafepoint() {
    if (camel::core::mm::autoSpaceSafepointSlowPathEnabled()) [[unlikely]] {
        camel::core::mm::autoSpace().safepoint("fastvm call boundary");
    }
}

/// The graph and entry pc a higher-order operator (map, foreach, ...) calls.
struct HigherOrderCallSite {
    camel::runtime::GCGraph *runtimeGraph = nullptr;
    size_t entryPc                        = 0;
};

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

class FastVMSchedPass : public RuntimeGraphSchedulePass {
  public:
    struct CallResult {
        slot_t result;
        ctx::Frame *rootFrame;
    };

  private:
    // Calls that may be open at once without being tail calls (camel/execute/limits.h).
    size_t maxRecursionDepth_;

    void populateDirectCallFrame(
        ctx::Frame *callerFrame, ctx::Frame *calleeFrame, data_arr_t srcArgs, data_arr_t dstSlots);
    void
    captureCallArgValues(ctx::Frame *callerFrame, data_arr_t srcArgs, std::vector<slot_t> &out);
    void populateDirectCallFrameFromValues(
        ctx::Frame *calleeFrame, data_arr_t dstSlots, std::span<const slot_t> argValues);
    void populateMarkedCallFrame(
        ctx::Frame *calleeFrame, Function *func, std::span<const slot_t> normArgs,
        std::span<const slot_t> withArgs = {});
    void populateFunctionClosureFrame(ctx::Frame *calleeFrame, Function *func);

  public:
    void populateIndirectCallFrame(
        ctx::Frame *callerFrame, ctx::Frame *calleeFrame, Function *func, data_arr_t nargs,
        data_arr_t wargs);

  private:
    // Frame pool used by the interpreter and JIT trampolines.
    ctx::FramePool framePool_;

    bytecode_vec_t bytecodes_;
    std::unordered_map<camel::runtime::GCGraph *, size_t> offsetMap_;
    std::vector<slot_t> tailArgValuesScratch_;

#if ENABLE_FASTVM_JIT
    std::unique_ptr<jit::IJitBackend> jitBackend_;
    std::unordered_map<jit::JitEntryFn, camel::runtime::GCGraph *> jitFnToGraph_;
    std::mutex jitCacheMutex_;
    jit::JitConfig jitConfig_{};
    jit::TierPolicy tierPolicy_{jitConfig_};
    bool enableJitTraceMir_ = false;
    // Shared JIT call context consumed by interpreter-side FUNC/TAIL slow paths.
    void *currentJitCtx_{};
    void compileAndCacheGraph(camel::runtime::GCGraph *graph, size_t entryPc);
    bool jitEnabled() const { return jitConfig_.policy != jit::JitPolicy::Disabled; }

    Bytecode *materializeCallTarget(size_t pc, Bytecode *bc);
    jit::JitEntryFn jitFnOf(camel::runtime::GCGraph *graph) const;
    void setJitFnOf(camel::runtime::GCGraph *graph, jit::JitEntryFn fn);
    bool jitCompileFailedOf(camel::runtime::GCGraph *graph) const;
    void
    setJitCompileFailedOf(camel::runtime::GCGraph *graph, bool failed, bool resetReport = false);
    bool jitFailureReportedOf(camel::runtime::GCGraph *graph) const;
    void setJitFailureReportedOf(camel::runtime::GCGraph *graph, bool reported);
#endif

    // The interpreter's own call stack: a direct call (FUNC) pushes its return pc and frame
    // here and does not nest on the native stack. The two arrays start with room for the
    // default limit and grow (outside the hot path) up to the limit in effect.
    std::vector<size_t> pcStack_ = std::vector<size_t>(
        camel::execute::kDefaultMaxRecursionDepth + camel::execute::kEntryCalls);
    std::vector<ctx::Frame *> frameStack_ = std::vector<ctx::Frame *>(
        camel::execute::kDefaultMaxRecursionDepth + camel::execute::kEntryCalls);
    size_t stackDepth_ = 0;
    // Calls that do nest on the native stack: a call through a function value (CALL) and each
    // element call of a higher-order operator run in a nested call(). They are open calls like
    // the ones on pcStack_, and the limit is on the sum.
    size_t nestedCalls_ = 0;

    // Raises MaxRecursionDepthExceeded, before the call, when one more nested call would
    // exceed the limit or would not fit the native stack. `caller` is the frame the call is
    // made from.
    void requireNestedCallRoom(const ctx::Frame *caller) const;
    /// One element call of a higher-order operator: a nested, counted call.
    slot_t callElement(const HigherOrderCallSite &site, ctx::Frame *frame);

    void precompile(camel::runtime::GCGraph *runtimeRoot);
    /// Compiles and links a graph that has no bytecode yet (see graphEntryPc).
    void compileLate(camel::runtime::GCGraph *graph);
    HigherOrderCallSite higherOrderCallSite(Function *func);

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
    void evalMarkedOperator_isolate(
        data_idx_t self, data_arr_t nargs, data_arr_t wargs, ctx::Frame &currFrame);

  public:
    FastVMSchedPass(const ctx::context_ptr_t &ctx, const FastVMConfig &config = {})
        : RuntimeGraphSchedulePass(ctx),
          maxRecursionDepth_(camel::execute::recursionLimit(*ctx).frames),
          framePool_(camel::execute::framePoolBytes(maxRecursionDepth_))
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
        framePool_.registerGcTracer();
    }
    virtual ~FastVMSchedPass();

    virtual camel::runtime::GCGraph *
    apply(camel::runtime::GCGraph *graph, std::ostream &os) override;

    CallResult callBorrowed(size_t pc, ctx::Frame *rootFrame);
    slot_t call(size_t pc, ctx::Frame *rootFrame);
    /// Entry pc of `graph`, compiling it (and what it reaches) first when it was created after
    /// startup, e.g. a gradient built at run time.
    size_t graphEntryPc(camel::runtime::GCGraph *graph);
    uint32_t noteIndirectCall(camel::runtime::GCGraph *graph) const;

#if ENABLE_FASTVM_JIT
    inline ctx::Frame *acquireFrameForCall(camel::runtime::GCGraph *graph) {
        ASSERT(graph != nullptr, "Runtime graph is null.");
        return framePool_.acquire(graph);
    }
    inline void releaseFrameForCall(ctx::Frame *frame) {
        if (framePool_.isActive(frame))
            framePool_.release(frame);
    }
    inline void releaseFrameUnchecked(ctx::Frame *frame) { framePool_.release(frame); }
    inline ctx::Frame *acquireFrameForTail(camel::runtime::GCGraph *graph) {
        ASSERT(graph != nullptr, "Runtime graph is null.");
        ctx::Frame *f = framePool_._acquire(graph);
        framePool_._resetTop();
        return f;
    }
    inline void releaseFrameForTail(ctx::Frame *frame) {
        if (framePool_.isActive(frame))
            framePool_.release(frame);
    }
    slot_t invokeOwnedJitFrame(jit::JitEntryFn fn, ctx::Frame *frame, void *jitCtx);
    /// Raises the located diagnostic of the fault compiled code recorded in the JitContext
    /// before it returned (`frame` is the frame it was entered on).
    [[noreturn]] void raiseJitFault(void *jitCtx, ctx::Frame *frame);
    inline ctx::Context &context() { return *context_; }
    camel::runtime::GCGraph *jitFnToGraph(jit::JitEntryFn fn) const;
    slot_t invokeCallOrJit(
        size_t pc, camel::runtime::GCGraph *runtimeGraph, ctx::Frame *frame, void *jitCtx,
        uint32_t callCount = 0);
#endif
};
