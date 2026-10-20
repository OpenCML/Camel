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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

// The bytecode side of the parallel scheduler: the image of the serial graphs of a run and one
// bytecode interpreter per thread that runs them.
//
// Which graphs are serial, and where the scheduler hands them over, is described at "Bytecode
// hand-off" in taskflow.h and decided in handoff.cpp. This class only holds what the hand-off
// runs on and makes one call.
//
// Ownership and lifetime
//   - One object per run, owned by the pass (TaskflowExecSchedPass::handoff_), created before
//     the run starts and replaced at the start of the next run.
//   - image_ is built once by the thread that starts the run and never written afterwards: the
//     workers read it without a lock.
//   - An interpreter belongs to the thread that created it, on that thread's first hosted call
//     of the run. The object owns all of them (interpreters_, guarded by a mutex that is taken
//     once per thread and run) and destroys them before the image. A thread finds its own
//     through a thread-local slot that names the run, so a slot left by an earlier run is never
//     used.
//   - The frames of an interpreter are GC roots through its frame pool (FramePool registers a
//     tracer with the allocator); they are only ever traced while their thread is stopped at a
//     safepoint (below).

#pragma once

#include "passes/sched/fastvm/hosted.h"
#include "taskflow.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class TaskflowBytecodeHandoff {
  public:
    explicit TaskflowBytecodeHandoff(ctx::context_ptr_t context);
    ~TaskflowBytecodeHandoff();
    TaskflowBytecodeHandoff(const TaskflowBytecodeHandoff &)            = delete;
    TaskflowBytecodeHandoff &operator=(const TaskflowBytecodeHandoff &) = delete;

    /// Compiles `serialGraphs` (closed under direct calls) into the image. Once, before the run.
    void build(
        const camel::runtime::Program &program,
        std::span<camel::runtime::GCGraph *const> serialGraphs);
    /// Graph -> entry pc, for the graphs build() compiled.
    const std::unordered_map<camel::runtime::GCGraph *, size_t> &entries() const {
        return image_.entries();
    }

    /// One call of the serial graph `graph` (compiled at `pc`) on the calling thread.
    ///
    /// The boundary between the scheduler and the interpreter:
    ///
    ///   Arguments   `fill(frame)` writes them into a frame of the interpreter's own pool,
    ///               inside the region. The value comes back as a slot, still inside the
    ///               region: the caller stores it before a collection can run, as after
    ///               evalGraph.
    ///   Collector   The interpreter runs inside `region`, like any leaf work of the
    ///               scheduler. Its safepoints (every FUNC and TAIL) do what the scheduler's
    ///               graph boundary does: when a collector wants in, leave the region, poll,
    ///               enter again (pollSafepoint). All live values of the thread are then in
    ///               frames: the scheduler's below the call, the interpreter's above it.
    ///   Depth       The interpreter counts its calls on top of the calls open on the thread
    ///               (ThreadState::callDepth, which includes this one), so the recursion limit
    ///               is reached at the same call as on the scheduler.
    ///   Faults      A fault leaves as a located Diagnostic that nothing has recorded
    ///               (DescribedFaultScope), exactly like one from raiseFault: the scheduler
    ///               chooses among concurrent faults and records the one that ends the run.
    ///               A fault-isolation boundary open on the thread is seen by the interpreter
    ///               too (it is thread-local state).
    ///   Calls back  None. A serial graph reaches serial graphs only, so the interpreter never
    ///               needs the scheduler, never waits for a task and is never entered twice on
    ///               a thread.
    template <typename Fill>
    slot_t call(size_t pc, camel::runtime::GCGraph *graph, TaskflowGcRegion &region, Fill &&fill) {
        Interpreter &interpreter = interpreterOfThisThread();
        region.enter();
        ctx::Frame *frame = interpreter.vm.acquireHostedFrame(graph);
        fill(frame);
        ++interpreter.calls;
        const FastVmSafepointScope safepoints({&pollSafepoint, &region});
        const camel::core::error::DescribedFaultScope describedFaults;
        return interpreter.vm.callHosted(pc, frame, region.thread().callDepth);
    }

    /// Hosted calls made and interpreters created so far. Exact only once the run has ended
    /// (the counters are per thread and unsynchronised).
    struct Stats {
        uint64_t calls        = 0;
        size_t interpreters   = 0;
        size_t graphs         = 0;
        size_t bytecodes      = 0;
    };
    Stats stats();

  private:
    // On a line of its own: `calls` is written at every hosted call of its thread.
    struct alignas(64) Interpreter {
        Interpreter(const ctx::context_ptr_t &context, bytecode_vec_t &image)
            : vm(context, FastVMConfig{}, &image) {}
        FastVMSchedPass vm;
        uint64_t calls = 0;
    };

    Interpreter &interpreterOfThisThread();
    /// FastVmSafepointHook::poll; `host` is the region of the thread's hosted call.
    static void pollSafepoint(void *host, std::string_view reason);
    static uint64_t nextRunId();

    ctx::context_ptr_t context_;
    // Tells this run's interpreters apart from an earlier run's in the thread-local slots.
    const uint64_t runId_ = nextRunId();
    FastVMHostImage image_;
    std::mutex interpretersMutex_;
    std::vector<std::unique_ptr<Interpreter>> interpreters_;
};
