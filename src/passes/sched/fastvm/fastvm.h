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
#include "camel/execute/observer.h"
#include "camel/execute/pass/runtime_sched.h"
#include "camel/runtime/graph.h"
#include "compile.h"
#include <array>
#include <cstdlib>
#include <memory>
#include <string_view>
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
 * What a call-boundary safepoint of the interpreter does on the calling thread when a host
 * scheduler runs the interpreter on several threads (hosted.cpp). The allocator collects on
 * whichever thread reaches a safepoint; with other threads running, a collection may only
 * start once they have all stopped at a safepoint of their own, and that agreement belongs to
 * the host. So the host installs a hook for the duration of a hosted call, and the interpreter
 * calls it in place of the allocator's safepoint. The hook returns when the thread may go on
 * (it may have collected itself, or waited for another thread to). nullptr: the thread is the
 * only one running the program (std::fvm, std::jit).
 */
struct FastVmSafepointHook {
    void (*poll)(void *host, std::string_view reason) = nullptr;
    void *host                                         = nullptr;
};

/// The hook of the calling thread (thread-local; hosted.cpp).
FastVmSafepointHook &fastVmSafepointHook() noexcept;
/// Out of line: the hook, or the allocator's own safepoint without one.
void fastVmSafepointSlowPath();

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
        fastVmSafepointSlowPath();
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
        // Not null when the activation ended in a tail call of compiled code instead of
        // returning: `rootFrame` is then the frame prepared for that code (this entry point of
        // it), and running it yields the result. The interpreter hands the call back rather
        // than making it, so that a tail call never nests on the native stack.
        void *tailFn = nullptr;
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

    // The bytecode this interpreter runs. It is the pass's own (ownBytecodes_, compiled by
    // apply) unless the pass was constructed over the image of a host (hosted.cpp): then it is
    // the host's vector, shared with the interpreters of the host's other threads and never
    // written by any of them.
    bytecode_vec_t ownBytecodes_;
    bytecode_vec_t &bytecodes_;
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
    /// What the back end is given to compile `graph`, whose bytecode starts at `entryPc`.
    jit::CompilationUnit jitUnitFor(
        camel::runtime::GCGraph *graph, size_t entryPc,
        const jit::CompilationDebugOptions *debug) const;
    bool jitEnabled() const { return jitConfig_.policy != jit::JitPolicy::Disabled; }

    Bytecode *materializeCallTarget(size_t pc, Bytecode *bc);
    jit::JitEntryFn jitFnOf(camel::runtime::GCGraph *graph) const;
    void setJitFnOf(camel::runtime::GCGraph *graph, jit::JitEntryFn fn);
    bool jitCompileFailedOf(camel::runtime::GCGraph *graph) const;
    void
    setJitCompileFailedOf(camel::runtime::GCGraph *graph, bool failed, bool resetReport = false);
    bool jitFailureReportedOf(camel::runtime::GCGraph *graph) const;
    void setJitFailureReportedOf(camel::runtime::GCGraph *graph, bool reported);
#else
    // A build without the JIT (ENABLE_FASTVM_JIT=0: every processor but x86-64). The two
    // interpreter loops name these in their indirect call outside any #if; with them the loops
    // compile unchanged and the call goes to the interpreter (see invokeCallOrJit below).
    void *currentJitCtx_{};
    constexpr bool jitEnabled() const { return false; }
#endif

    // Coverage statistics (fastvm_stats.cpp), off unless CAMEL_FVM_STATS names a file. While
    // they are on, opCounts_ points to one counter per bytecode pc and the interpreter counts
    // every bytecode it runs; otherwise it is null and the interpreter is not slowed.
    struct Stats;
    std::shared_ptr<Stats> stats_;
    uint64_t *opCounts_ = nullptr;
    size_t graphLengthOf(camel::runtime::GCGraph *graph) const; // in bytecode units; 0 unknown
    void statsBegin();
    void statsNoteGraph(camel::runtime::GCGraph *graph, size_t pc);
    void statsNoteCompiled(size_t pc);
    void statsWrite();

    // The interpreter's own call stack: a direct call (FUNC) pushes its return pc and frame
    // here and does not nest on the native stack. The two arrays start with room for the
    // default limit and grow (outside the hot path) up to the limit in effect.
    std::vector<size_t> pcStack_ = std::vector<size_t>(
        camel::execute::kDefaultMaxRecursionDepth + camel::execute::kEntryCalls);
    std::vector<ctx::Frame *> frameStack_ = std::vector<ctx::Frame *>(
        camel::execute::kDefaultMaxRecursionDepth + camel::execute::kEntryCalls);
    size_t stackDepth_ = 0;
#if ENABLE_FASTVM_JIT
    // For an entry of pcStack_ pushed by the driver of framed code: where the caller's code
    // continues (JitContext::resumeAddr). Grown with pcStack_ by the driver.
    std::vector<uint64_t> resumeStack_;
#endif
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

    /// Called by the inspected loop before each instruction: records the origin of the
    /// instruction at `pc` as the current runtime origin and reports the instruction to the
    /// execution observer, if any.
    void inspectInstruction(size_t pc, ctx::Frame *frame);
    /// Called by the inspected loop where it catches a fault, before the frames of the stack
    /// are released (a debugger shows the stack and the values there): reports the fault to
    /// the execution observer, if any.
    void observeFault(const camel::core::error::RuntimeFault &fault);

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
    /// `hostImage`: the bytecode of a host scheduler to interpret in place of the pass's own
    /// (see "Hosted interpreters" below); nullptr for a pass that runs programs itself.
    FastVMSchedPass(
        const ctx::context_ptr_t &ctx, const FastVMConfig &config = {},
        bytecode_vec_t *hostImage = nullptr)
        : RuntimeGraphSchedulePass(ctx),
          maxRecursionDepth_(camel::execute::recursionLimit(*ctx).frames),
          framePool_(camel::execute::framePoolBytes(maxRecursionDepth_)),
          bytecodes_(hostImage ? *hostImage : ownBytecodes_)
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
              // A switch for testing the compiler: CAMEL_JIT_HOT_THRESHOLD=0 compiles a graph
              // at its first call instead of at the second from one call site, so that short
              // programs (the random ones of test/tools/progen) run as compiled code too.
              if (const char *threshold = std::getenv("CAMEL_JIT_HOT_THRESHOLD");
                  threshold != nullptr && threshold[0] >= '0' && threshold[0] <= '9') {
                  c.hotThreshold = static_cast<size_t>(std::strtoull(threshold, nullptr, 10));
              }
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
#if ENABLE_FASTVM_COMPUTED_GOTO
    /// The same loop compiled a second time with the hooks of the execution observer
    /// (inspectvm.cpp): what a program runs on while an observer is installed.
    CallResult callBorrowedInspected(size_t pc, ctx::Frame *rootFrame);
#endif
    slot_t call(size_t pc, ctx::Frame *rootFrame);

    // ---- Hosted interpreters (hosted.cpp) --------------------------------------------------
    //
    // A scheduler that evaluates graphs itself (the parallel scheduler) can run some of them
    // as bytecode: it compiles them once into an image (FastVMHostImage), constructs one pass
    // per thread over that image, and enters it with the two calls below. Such a pass never
    // runs apply(); it interprets only (the JIT stays off) and never compiles.
    //
    // One pass belongs to one thread: its frame pool, call stack and counters are not shared.
    // The image is shared and read-only while hosted calls run.

    /// A frame of `graph` from this interpreter's pool, for the host to fill with the
    /// arguments of a hosted call. It must be passed to callHosted next (the pool is a stack).
    ctx::Frame *acquireHostedFrame(camel::runtime::GCGraph *graph) {
        return framePool_.acquire(graph);
    }
    /// Runs the graph compiled at `pc` on `frame` (from acquireHostedFrame) and returns its
    /// value; the frame is released whether the call returns or throws. `hostOpenCalls`: the
    /// calls open on this thread in the host, the hosted one included; the interpreter counts
    /// its own calls on top of them, so the recursion limit is reached at the same call as in
    /// the host. A fault leaves as a located Diagnostic, as from call(); whether it is recorded
    /// is the host's choice (camel::core::error::DescribedFaultScope).
    slot_t callHosted(size_t pc, ctx::Frame *frame, size_t hostOpenCalls);

    /// Entry pc of `graph`, compiling it (and what it reaches) first when it was created after
    /// startup, e.g. a gradient built at run time.
    size_t graphEntryPc(camel::runtime::GCGraph *graph);
    uint32_t noteIndirectCall(camel::runtime::GCGraph *graph) const;

    // Frames of direct calls and tail calls: used by the interpreter loops with and without
    // the JIT, and by compiled code.
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
#if ENABLE_FASTVM_JIT
    slot_t invokeOwnedJitFrame(jit::JitEntryFn fn, ctx::Frame *frame, void *jitCtx);
    /// The driver of framed code (x64::JitExitKind): runs `fn` on `frame` and does, for every
    /// exit the code takes, what the interpreter does for that bytecode, until the activation
    /// entered here has returned. Calls between framed graphs stay in this loop, on the
    /// interpreter's call stack (push / pop), and do not nest on the native stack.
    slot_t runFramedJit(jit::JitEntryFn fn, ctx::Frame *frame, void *jitCtx);
    bool jitFramedOf(camel::runtime::GCGraph *graph) const;
    /// Raises the located diagnostic of the fault compiled code recorded in the JitContext
    /// before it returned (`frame` is the frame it was entered on).
    [[noreturn]] void raiseJitFault(void *jitCtx, ctx::Frame *frame);
    inline ctx::Context &context() { return *context_; }
    camel::runtime::GCGraph *jitFnToGraph(jit::JitEntryFn fn) const;
    slot_t invokeCallOrJit(
        size_t pc, camel::runtime::GCGraph *runtimeGraph, ctx::Frame *frame, void *jitCtx,
        uint32_t callCount = 0);
#else
    /// Without the JIT a call is a call of the interpreter.
    inline slot_t invokeCallOrJit(
        size_t pc, camel::runtime::GCGraph *, ctx::Frame *frame, void *, uint32_t = 0) {
        return call(pc, frame);
    }
#endif
};
