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
 * Created: Oct. 05, 2025
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/limits.h"
#include "camel/execute/pass/runtime_sched.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/taskflow.hpp>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::core::context {

// Taskflow-specific concurrent frame allocator.
// Unlike the generic FramePool, this allocator tolerates concurrent
// acquire/release operations and out-of-order recycling across workers.
// Frames are kept per graph; every thread has its own short list of free frames of the graphs
// it runs, so a call takes and returns its frame without a lock (taskflow_frame_pool.cpp).
class TaskflowFramePool {
  public:
    explicit TaskflowFramePool(
        size_t chunkBytes = 1 * camel::core::mm::MB, size_t minChunkFrames = 8);
    ~TaskflowFramePool();

    TaskflowFramePool(const TaskflowFramePool &)            = delete;
    TaskflowFramePool &operator=(const TaskflowFramePool &) = delete;

    Frame *acquire(camel::runtime::GCGraph *graph);
    void release(Frame *frame);
    void warmup(camel::runtime::GCGraph *graph, size_t count);

  private:
    struct GraphArena {
        camel::runtime::GCGraph *runtimeGraph{nullptr};
        size_t frameSize{0};
        const camel::core::type::TupleType *runtimeDataType{nullptr};
        ::Tuple *staticArea{nullptr};
        size_t chunkFrames{0};     // Frames of a full chunk.
        size_t nextChunkFrames{0}; // Frames of the next chunk: doubles up to chunkFrames.
        // Guards the two lists below. Taken when a thread's own list of the graph runs empty
        // or over (LocalArena), not per call.
        std::mutex mutex;
        // Free frames that are in no thread's list.
        std::vector<Frame *> freeFrames;
        struct Chunk {
            std::byte *base = nullptr;
            size_t frames   = 0;
        };
        std::vector<Chunk> chunks;
    };
    struct LocalArena;
    struct ThreadCache;

    static ThreadCache &threadCache();
    static void giveBack(LocalArena &local);
    static bool isActive(Frame *frame);
    static void setActive(Frame *frame, bool active);
    LocalArena &localArena(camel::runtime::GCGraph *graph);
    GraphArena &getOrCreateArena(camel::runtime::GCGraph *graph);
    void allocateChunk(GraphArena &arena, size_t minFrameCount);
    void clearGcSlots(Frame *frame) const;
    void
    traceActiveFrames(const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate);
    static uintptr_t arenaKey(camel::runtime::GCGraph *graph);

    static uint64_t nextPoolId();

    size_t chunkBytes_;
    size_t minChunkFrames_;
    // Distinguishes this pool in the per-thread frame lists (localArena); never 0.
    uint64_t poolId_;
    std::mutex arenasMutex_;
    std::unordered_map<uintptr_t, std::unique_ptr<GraphArena>> arenas_;
};

} // namespace camel::core::context

namespace ctx = camel::core::context;

// Stop-the-world handshake between the threads of the parallel scheduler and the collector.
//
// The allocator collects on whichever thread polls a safepoint. With one thread that is safe:
// between two nodes every live value sits in a frame, and frames are GC roots. With several
// workers it is not: a worker in the middle of an operator holds values (a freshly allocated
// result, the return value of a call on its way into the caller frame) that no root reaches.
//
// So every piece of work that can hold such a value runs inside a *region*, and a deferred
// collection runs only while no other thread is inside one. Regions are leaf work (a few
// operators, a frame fill, a result store): a thread never waits for another task while it
// holds one, so the collector waits no longer than the longest operator. If an operator
// blocks (a sleep, a rendezvous) the collector gives up after a short wait and the collection
// is retried at a later poll, so the tasks such an operator may be waiting for keep running.
// After a collection the next one starts no sooner than twice the time it took.
//
// Not covered (as before graphs ran as tasks): the collection the allocator runs inside an
// allocation that finds the heap full; it does not go through a poll.
// The part of a run that one fault ends (docs/technical/33_fault_isolation.md, 3.6). The run has
// a root domain; every fault-isolation node opens one for the call it makes. Tasks belong to
// the domain of the thread that started them (ThreadState::faultDomain). A fault is carried
// to the end of its domain as an exception; which of several concurrent faults that is, is
// decided where their operator waited for them (ElementFaults in taskflow.cpp).
struct TaskflowFaultDomain {
    // Fault-isolation boundaries open around the domain (0: the root domain).
    uint32_t isolationDepth = 0;
};

class TaskflowGcGate {
  public:
    struct alignas(64) ThreadState {
        // The fault domain of what this thread is running (nullptr: the root domain).
        TaskflowFaultDomain *faultDomain = nullptr;
        std::atomic<bool> active{false};
        uint32_t depth = 0;
        // Calls of graphs now open on this thread's native stack (for the diagnostic of a
        // recursion that is too deep).
        uint32_t callDepth = 0;
        // Bounds of this thread's native stack (0: unknown).
        uintptr_t stackLow  = 0;
        uintptr_t stackHigh = 0;
    };

    static TaskflowGcGate &instance();
    // The calling thread's state, registered on first use and kept for the process lifetime.
    ThreadState &self();

    void enter(ThreadState &ts) {
        if (ts.depth++ != 0) {
            return;
        }
        for (;;) {
            ts.active.store(true, std::memory_order_seq_cst);
            if (!stopRequested_.load(std::memory_order_seq_cst)) [[likely]] {
                return;
            }
            ts.active.store(false, std::memory_order_seq_cst);
            waitForCollector();
        }
    }
    void leave(ThreadState &ts) {
        if (--ts.depth == 0) {
            ts.active.store(false, std::memory_order_release);
        }
    }
    // True when a collection is pending or a collector is waiting for the regions to empty:
    // a thread inside a region should leave it at its next safepoint. Two plain loads.
    bool collectorWanted() const {
        return stopRequested_.load(std::memory_order_relaxed) ||
               camel::core::mm::detail::autoSpaceSafepointSlowPath.load(std::memory_order_relaxed);
    }
    // Safepoint poll; the calling thread must be outside every region.
    void poll(ThreadState &ts, std::string_view reason) {
        if (camel::core::mm::detail::autoSpaceSafepointSlowPath.load(std::memory_order_acquire))
            [[unlikely]] {
            collect(ts, reason);
        }
    }

    // What the gate did since the last resetStats(). Printed at the end of a run of the
    // scheduler when CAMEL_TF_GC_STATS is set (see reportStats), so that a test can check that
    // collections really ran while flows were running.
    struct Stats {
        std::atomic<uint64_t> pendingPolls{0};      // polls that found work for the collector
        std::atomic<uint64_t> collections{0};       // collections the allocator performed
        std::atomic<uint64_t> collectionsInFlow{0}; // ... while a flow was running
        std::atomic<uint64_t> emptySafepoints{0};   // world stopped, allocator had nothing to do
        std::atomic<uint64_t> cappedPolls{0};       // skipped: too soon after the last collection
        std::atomic<uint64_t> gaveUp{0};            // another thread stayed in a region for 500 ms
        std::atomic<uint64_t> backoffPolls{0};      // skipped: backing off after giving up
        std::atomic<int64_t> waitNs{0};             // time spent waiting for the other threads
        std::atomic<int64_t> maxWaitNs{0};
        std::atomic<int64_t> collectNs{0}; // time spent inside the allocator's safepoint
    };
    void resetStats();
    // One line: "[taskflow] gc: pending_polls=.. collections=.. in_flow=.. ...".
    void reportStats(std::ostream &os) const;
    // A flow is running on the executor (nested flows count once each). Statistics only: the
    // gate does not decide by it.
    void flowStarted() { flowsRunning_.fetch_add(1, std::memory_order_relaxed); }
    void flowEnded() { flowsRunning_.fetch_sub(1, std::memory_order_relaxed); }

  private:
    void collect(ThreadState &ts, std::string_view reason);
    void waitForCollector();

    std::atomic<bool> stopRequested_{false};
    // No poll before this time: backing off after the collector gave up.
    std::atomic<int64_t> retryAfterNs_{0};
    // No collection before this time: the cap after a collection.
    std::atomic<int64_t> capUntilNs_{0};
    std::atomic<int64_t> flowsRunning_{0};
    Stats stats_;
    std::mutex collectorMutex_;
    std::mutex registryMutex_;
    std::vector<std::unique_ptr<ThreadState>> threads_;
};

// A region of the constructing thread, entered and left any number of times; left on
// destruction.
class TaskflowGcRegion {
  public:
    TaskflowGcRegion() : gate_(TaskflowGcGate::instance()), ts_(gate_.self()) {}
    ~TaskflowGcRegion() { leave(); }
    TaskflowGcRegion(const TaskflowGcRegion &)            = delete;
    TaskflowGcRegion &operator=(const TaskflowGcRegion &) = delete;

    void enter() {
        if (!held_) {
            gate_.enter(ts_);
            held_ = true;
        }
    }
    void leave() {
        if (held_) {
            gate_.leave(ts_);
            held_ = false;
        }
    }
    void poll(std::string_view reason) {
        if (!held_ && ts_.depth == 0) {
            gate_.poll(ts_, reason);
        }
    }
    // A safepoint between two pieces of leaf work, where every live value is in a frame. The
    // region stays held unless a collector wants in: entering and leaving it are sequentially
    // consistent stores, and a graph that does a few built-in operators between two
    // safepoints paid for them at every call and every branch. The collector waits no longer
    // than before for a thread that runs calls and branches (it gives up after 500 ms).
    void safepoint(std::string_view reason) {
        if (!held_) {
            poll(reason);
        } else if (gate_.collectorWanted()) [[unlikely]] {
            leave();
            poll(reason);
        }
    }
    TaskflowGcGate::ThreadState &thread() { return ts_; }

  private:
    TaskflowGcGate &gate_;
    TaskflowGcGate::ThreadState &ts_;
    bool held_ = false;
};

class TaskflowBytecodeHandoff;

class TaskflowExecSchedPass : public RuntimeGraphSchedulePass {
  public:
    /// `max_concurrent_tasks` = 0: the thread limit of the context (`-t`), else the number of
    /// hardware threads.
    // Constructor and destructor are out of line (handoff.cpp): the pass owns a
    // TaskflowBytecodeHandoff, which is an incomplete type here.
    TaskflowExecSchedPass(const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks = 0);
    ~TaskflowExecSchedPass() override;

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
    void buildAndDump(camel::runtime::GCGraph *graph, std::ostream &os);

    tf::Taskflow mainFlow_;
    ctx::TaskflowFramePool framePool_{1 * camel::core::mm::MB};

  private:
    struct RuntimeBuildInfo {
        std::vector<uint8_t> skipNodes;
        std::vector<std::vector<camel::execute::RuntimeBranchArmRegion>> branchArms;
    };

    static size_t workerCount(const ctx::context_ptr_t &ctx, size_t requested) {
        if (requested == 0 && ctx) {
            requested = ctx->maxThreads();
        }
        if (requested == 0) {
            requested = std::thread::hardware_concurrency();
        }
        return std::max<size_t>(1, requested);
    }
    /// Runs `flow` to completion on the pass's executor, from any thread.
    void runOnExecutor(tf::Taskflow &flow);

    tf::Executor executor_;
    // Per graph and BRCH: where each arm starts executing in the linear order
    // (resolveRuntimeBranchArmEntry, shared with NodeVM and FastVM). Used by the node-by-node
    // fallback only; a structured plan resolves the entries when it is built.
    std::unordered_map<
        camel::runtime::GCGraph *,
        std::unordered_map<
            camel::runtime::gc_node_ref_t, std::vector<camel::runtime::gc_node_ref_t>>>
        linearArmEntryCache_;
    // Filled on first use by whichever worker gets there first (the elements of one `map`
    // call the same graph at once).
    std::mutex linearCacheMutex_;

    // ---- Graph-level task execution -------------------------------------------------------
    //
    // A graph is evaluated as the task graph its edges describe: two nodes run concurrently
    // unless a path of NORM, WITH or CTRL edges orders them. The static shape is a GraphPlan,
    // built once per graph from the same linear order and the same branch-arm ranges that the
    // node-by-node evaluator (and NodeVM) execute:
    //
    //   scope   the top level of a graph, or one arm of a branch
    //   item    one executable node of a scope, or a whole BRCH..JOIN region (the BRCH picks
    //           an arm at run time and that arm's scope runs inside the item)
    //   chain   a maximal run of items between which no concurrency is possible; one task
    //
    // An edge between nodes of different items orders the two items in their innermost common
    // scope. A scope whose items are totally ordered runs inline on the calling thread; any
    // other scope runs as a Taskflow graph (one task per chain) on the shared executor.
    enum class ItemKind : uint8_t { Leaf, Call, Mark, Branch };
    struct PlanItem {
        camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
        uint32_t branch                    = 0; // index into GraphPlan::branches (Branch items)
        ItemKind kind                      = ItemKind::Leaf;
        // FUNC in tail position: evaluated by the loop of evalGraph, not by a nested call.
        bool tailCall = false;
        // Comes after the tail value in the linear order (forwarding GATEs), or is the tail
        // JOIN: skipped once a tail call has replaced the rest of the graph.
        bool afterTail = false;
        // An operator of a module (not one of the built-in `:...` operators): it can run for
        // long or block, so the region is left after it and a waiting collector gets in. After
        // a built-in operator the region stays held until the next safepoint.
        bool leaveAfter = false;
    };
    struct FlowInstance {
        tf::Taskflow flow;
        ctx::Frame *frame               = nullptr;
        std::atomic<uint32_t> *tailNode = nullptr;
        std::mutex errorMutex;
        std::exception_ptr error;
        // A task of the flow has failed: its tasks that have not started do not start (they
        // may depend on what the failed one did not produce).
        std::atomic<bool> failed{false};
        // Calls open on the thread that runs the scope; its tasks continue from there.
        uint32_t parentDepth = 0;
        // The fault domain of the thread that runs the scope.
        TaskflowFaultDomain *parentDomain = nullptr;
    };
    struct PlanScope {
        std::vector<PlanItem> items; // linear order
        // Runs inline on the calling thread, in linear order: its items are totally ordered,
        // or it has no two unordered items worth a task each (see the granularity rule at
        // graphIsHeavy).
        bool sequential = true;
        std::vector<std::vector<PlanItem>> chains;             // parallel scopes only
        std::vector<std::pair<uint32_t, uint32_t>> chainEdges; // chain -> chain
        int32_t parentScope = -1;
        uint32_t parentItem = 0;
        uint32_t depth      = 0;
        // Prebuilt task graphs of this scope, reused by successive runs.
        std::mutex poolMutex;
        std::vector<std::unique_ptr<FlowInstance>> idle;
    };
    struct PlanBranch {
        camel::runtime::gc_node_ref_t brch = camel::runtime::kInvalidNodeRef;
        camel::runtime::gc_node_ref_t join = camel::runtime::kInvalidNodeRef;
        std::vector<int32_t> armScopes; // -1: the arm runs nothing
    };
    // An operator node: where its with and norm arguments are in GraphPlan::operArgs.
    struct PreparedOper {
        uint32_t withBegin = 0;
        uint32_t normBegin = 0;
        uint32_t end       = 0;
        bool prepared      = false;
    };
    static constexpr size_t kNoBytecode = std::numeric_limits<size_t>::max();
    struct GraphPlan {
        camel::runtime::GCGraph *graph = nullptr;
        // False: the graph does not fit the scope model (or task graphs are switched off); it
        // is evaluated node by node in linear order, its calls still as task graphs.
        bool structured = false;
        std::vector<std::unique_ptr<PlanScope>> scopes; // [0]: top level
        std::vector<PlanBranch> branches;
        std::vector<camel::runtime::gc_node_ref_t> topo;
        camel::runtime::gc_node_ref_t lastNode = camel::runtime::kInvalidNodeRef;
        bool lastNodeIsJoin                    = false;
        bool anchorOk                          = false;
        // The FUNC nodes in tail position (sorted): the tail value itself, or a call whose
        // result reaches the tail JOIN only through forwarding GATEs and nested JOINs, with
        // nothing of its arm running after it (the rule NodeVM and FastVM use).
        std::vector<camel::runtime::gc_node_ref_t> tailCalls;
        // Per node ref. The function of an operator is stored when the operator first runs
        // (resolving it can import a module, and an unknown operator is a fault of the
        // execution, not of the plan); nullptr until then.
        std::vector<PreparedOper> opers;
        std::vector<camel::runtime::gc_data_idx_t> operArgs;
        std::unique_ptr<std::atomic<operator_t>[]> resolvedOpers;
        // As an element function of a higher-order operator: 1 when everything the graph can
        // reach is built-in operators and direct calls without recursion (the work of an
        // element is then bounded by the size of the graphs), 0 when not, -1 until asked
        // (boundedLight).
        std::atomic<int8_t> boundedLightState{-1};
        // Where the bytecode of the graph starts when the graph is *serial* and is handed to
        // the bytecode interpreter ("Bytecode hand-off" below); kNoBytecode when the scheduler
        // evaluates it. Written once before the run starts, never after.
        size_t bytecodeEntry = kNoBytecode;
        bool isTailCall(camel::runtime::gc_node_ref_t ref) const {
            return std::binary_search(tailCalls.begin(), tailCalls.end(), ref);
        }
    };

    std::shared_mutex plansMutex_;
    std::unordered_map<camel::runtime::GCGraph *, std::unique_ptr<GraphPlan>> plans_;
    // Tells the plans of this run apart from those of an earlier run (or another pass) in the
    // per-thread plan caches of planFor; never 0.
    uint64_t planEpoch_ = nextPlanEpoch();
    static uint64_t nextPlanEpoch();
    // The fault domain of everything outside a fault-isolation boundary. A failure in it ends
    // the run.
    TaskflowFaultDomain rootDomain_;
    TaskflowFaultDomain &domainOf(const TaskflowGcGate::ThreadState &thread) {
        return thread.faultDomain ? *thread.faultDomain : rootDomain_;
    }
    // What a failure stops, for a fault that is isolated and for one that ends the run alike
    // (design principles 2.10): tasks that have started run to their end (with everything
    // they start), tasks that have not started do not start, and whoever started the flow
    // waits for all that started before the failure goes on. There is no preemption and no
    // flag polled by running code: a scope skips its remaining chains (FlowInstance::failed),
    // and an unordered operator skips the elements after the smallest faulting index it
    // knows (ElementFaults).

    // The limit on calls open at once that are not tail calls: the option, the environment
    // or the default, as on the other schedulers (camel/execute/limits.h). 0 only without a
    // context: the native stack alone then bounds a recursion. The count follows a call
    // across threads: a task continues from the open calls of the thread that started it
    // (ThreadState::callDepth).
    size_t maxRecursionDepth_ = 0;
    // Describes `fault` at node `nodeRef` of `graph` (at the graph when the node has no source
    // position) and throws the diagnostic. Nothing is recorded here: element functions that
    // run concurrently can fault together, and of their faults the one of the smallest
    // element index is reported (design principles 2.10; ElementFaults), by the outer index
    // first for nested operators. The fault that arrives at the entry of the run is recorded
    // there; the one that arrives at a fault-isolation node becomes its error value.
    [[noreturn]] void raiseFault(
        const camel::core::error::RuntimeFault &fault, camel::runtime::GCGraph *graph,
        camel::runtime::gc_node_ref_t nodeRef);
    // Raises MaxRecursionDepthExceeded at the call `callNode` of `callerGraph` when one more
    // open call would exceed the limit or the native stack of the thread.
    void requireCallRoom(
        TaskflowGcGate::ThreadState &thread, camel::runtime::GCGraph *callerGraph,
        camel::runtime::gc_node_ref_t callNode);
    // CAMEL_TF_LINEAR=1: evaluate every graph node by node (the behaviour between commit
    // 1559559a and the restore of graph-level tasks). For comparison and diagnosis.
    bool forceLinear_ = false;

    // ---- Granularity ----------------------------------------------------------------------
    //
    // A scope runs as a task graph only when it has at least two items with no path between
    // them that are each worth a task: a higher-order operator, a call of a heavy graph, or a
    // branch one of whose arms contains such an item. Every other scope runs inline.
    //
    // A graph is heavy when its body, or the body of a graph it calls directly or indirectly,
    // contains a higher-order operator (`:mark/...`), an operator of a module (a URI with a
    // protocol, `tensor:matmul`, `algo:sort`: anything but the built-in `:...` operators), or
    // a call of a function value (the callee is not known when the plan is built). A graph
    // that only does built-in arithmetic, comparisons and calls of such graphs (the body of
    // `fib`) is light: a task per call costs more than the call. A recursive graph is
    // therefore heavy exactly when its body is heavy apart from the recursive calls.
    //
    // Decided once per graph when its plan is built; nothing is measured at run time.
    // CAMEL_TF_FANOUT_ALL=1 switches the rule off (every scope that is not totally ordered
    // runs as a task graph), for comparison.
    bool fanOutAll_ = false;
    std::mutex heavyMutex_;
    std::unordered_map<camel::runtime::GCGraph *, bool> heavyGraphs_;
    bool graphIsHeavy(camel::runtime::GCGraph *graph);
    bool boundedLight(GraphPlan &plan);
    // Task graphs run since the pass started: scopes, and unordered operators that asked for
    // helpers (reported with CAMEL_TF_GC_STATS).
    std::atomic<uint64_t> taskGraphsRun_{0};

    // ---- Bytecode hand-off (handoff.h, handoff.cpp) ---------------------------------------
    //
    // Design principles 2.11: the parallel scheduler hands subgraphs that have nothing to
    // parallelise to the bytecode interpreter. Walking a graph node by node costs 4 to 6 times
    // what its bytecode costs, and where no two things can run at once that is all the
    // scheduler adds.
    //
    // A graph is *serial* when the scheduler would run all of it, and everything it calls, on
    // the calling thread in the linear order. Precisely, G is serial when every graph H that G
    // reaches through direct calls (G included) satisfies all of:
    //
    //   1. H has no higher-order operator (`:mark/...`: map, apply, filter, reduce, foreach,
    //      their unordered forms, and the fault-isolation node). The unordered ones are where
    //      the scheduler runs elements concurrently; all of them call a function value.
    //   2. H has no call of a function value (CALL): the callee is not known before the run and
    //      may be a graph that is not serial.
    //   3. No scope of H's plan runs as a task graph (PlanScope::sequential holds for all):
    //      H has no two unordered items the granularity rule would fork.
    //   4. Every operator of H is found without importing a module, and H has a frame layout:
    //      what the bytecode compiler needs. (An operator of a module that is imported only
    //      when first used, like `exit`, keeps its graph on the scheduler.)
    //
    // The property is closed under direct calls by definition, so bytecode never has to call
    // back into the scheduler: a serial graph reaches serial graphs only, through FUNC and TAIL
    // bytecodes that the interpreter runs itself. It is decided for every graph reachable from
    // the entry graph before the run starts (prepareBytecodeHandoff), and the serial graphs are
    // compiled then, once, into one image that is read-only while the workers run. A graph
    // created during the run (a gradient built at run time) is not in the image and stays on
    // the scheduler.
    //
    // The scheduler reaches a graph in two places: a call (callGraph: FUNC, CALL, an element
    // call of a higher-order operator, the call of the fault-isolation node, the entry graph)
    // and a tail call (the loop of evalGraph). Both look at GraphPlan::bytecodeEntry and enter
    // the interpreter of the calling thread instead of evalGraph. What the two sides agree on
    // at that boundary is written at TaskflowBytecodeHandoff::call.
    //
    // CAMEL_TF_BYTECODE=0 switches the hand-off off (every graph is evaluated by the
    // scheduler, as before); it is also off under CAMEL_TF_LINEAR and while an execution
    // observer or the inspection mode is active. CAMEL_TF_DIAG=1 prints the decision per graph.
    std::unique_ptr<TaskflowBytecodeHandoff> handoff_;
    /// Classifies the graphs reachable from `root`, compiles the serial ones and records their
    /// entry in their plans. On the thread that starts the run, before any graph is evaluated.
    void prepareBytecodeHandoff(camel::runtime::GCGraph *root);
    /// Why `plan`'s graph cannot be serial by its own body (conditions 1 to 4 for H = the
    /// graph); empty when its body allows it.
    std::string_view serialObstacle(const GraphPlan &plan) const;
    /// Calls `target` on the calling thread and returns its value, inside `region`: on the
    /// bytecode interpreter when the graph is serial, else on a frame of the pool with
    /// evalGraph. `fill(frame)` writes the arguments into the callee's frame (inside the
    /// region). The caller has counted the call (OpenCall). `plan`: of `target`, when known.
    template <typename Fill>
    slot_t callGraph(
        camel::runtime::GCGraph *target, GraphPlan *plan, TaskflowGcRegion &region, Fill &&fill);

    GraphPlan *planFor(camel::runtime::GCGraph *graph);
    std::unique_ptr<GraphPlan> buildGraphPlan(camel::runtime::GCGraph *graph);
    void buildStructuredPlan(GraphPlan &plan);

    slot_t evalGraphTF(camel::runtime::GCGraph *graph, ctx::Frame *frame);
    // Evaluates `graph` on `frame`, tail calls included. May be entered inside `region`;
    // returns inside it, so the caller stores the result before a collection can run.
    // `plan`: the plan of `graph` when the caller has it.
    slot_t evalGraph(
        camel::runtime::GCGraph *graph, ctx::Frame *frame, TaskflowGcRegion &region,
        GraphPlan *plan = nullptr);
    // The three below take the calling thread's region in whatever state it is and leave it in
    // whatever state their last step needed. `tailNode` receives the FUNC node of a tail call.
    void runScope(
        GraphPlan &plan, PlanScope &scope, ctx::Frame *frame, std::atomic<uint32_t> &tailNode,
        TaskflowGcRegion &region);
    void runItems(
        GraphPlan &plan, std::span<const PlanItem> items, ctx::Frame *frame,
        std::atomic<uint32_t> &tailNode, TaskflowGcRegion &region);
    void runLinearBody(
        GraphPlan &plan, ctx::Frame *frame, std::atomic<uint32_t> &tailNode,
        TaskflowGcRegion &region);
    void executeCallNode(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        TaskflowGcRegion &region);
    // Where the element calls of a higher-order operator come from: the operator node (a call
    // that is too deep is reported there) and the calls open on the thread that started the
    // operator, which every element continues from on the thread that runs it.
    struct ElementCallSite {
        camel::runtime::GCGraph *graph      = nullptr;
        camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
        uint32_t depth                      = 0;
        TaskflowFaultDomain *domain         = nullptr;
    };
    // Calls the function value held in slot `funcIdx` of `funcFrame` (its graph is
    // `targetGraph`, with plan `targetPlan`) on the calling thread: `fillArgs(span)` reads the
    // arguments and `sink(result)` stores the result, both inside `region`, which is held on
    // return. The call is an open call like any other. The thread must already count from
    // `site.depth` in `site.domain` (the thread that started the operator does; a task of the
    // operator sets them with InheritedDepth).
    template <typename FillArgs, typename Sink>
    void callClosure(
        TaskflowGcRegion &region, const ElementCallSite &site,
        camel::runtime::GCGraph *targetGraph, GraphPlan *targetPlan, ctx::Frame *funcFrame,
        camel::runtime::gc_data_idx_t funcIdx, size_t argCount, FillArgs &&fillArgs,
        Sink &&sink);
    // Runs `body(region, i)` once for every i in [0, n), in no particular order: the element
    // calls of an unordered higher-order operator (see "Higher-order operators" in
    // taskflow.cpp for how the elements are grouped and when other workers are asked to help).
    template <typename Body>
    void runElements(
        const ElementCallSite &site, GraphPlan *targetPlan, size_t n, Body &&body);
    camel::runtime::gc_node_ref_t branchArmEntryForLinear(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef, size_t armIndex,
        std::span<const camel::runtime::gc_node_ref_t> topoNodes);
    slot_t runPreparedSubgraph(tf::Subflow &sf, camel::runtime::GCGraph *graph, ctx::Frame *frame);
    // `plan`: the plan of `graph` when the caller has it (operators are then resolved once).
    slot_t executeLinearNode(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        const GraphPlan *plan = nullptr);
    slot_t executeLinearMarkedOperator(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);

    ctx::Frame *acquirePreparedRuntimeCallFrame(
        camel::runtime::GCGraph *targetGraph, camel::runtime::gc_node_ref_t callNodeRuntimeIndex,
        ctx::Frame *sourceFrame);
    ctx::Frame *acquirePreparedClosureCallFrame(
        camel::runtime::GCGraph *targetGraph, ::Tuple *closure, std::span<const slot_t> args);
    /// Writes the arguments `args` and the captured values of `closure` into `dest`, a frame of
    /// `targetGraph` from any pool.
    static void fillClosureCallFrame(
        ctx::Frame *dest, camel::runtime::GCGraph *targetGraph, ::Tuple *closure,
        std::span<const slot_t> args);

    slot_t executePreparedOperator(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        const GraphPlan *plan = nullptr);
    slot_t executePreparedNode(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf, const RuntimeBuildInfo *buildInfo);
    slot_t executePreparedBranchArm(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef, size_t armIndex,
        ctx::Frame *frame, tf::Subflow &sf, const RuntimeBuildInfo *buildInfo);

    template <typename FlowT>
    void instantiate_graph_instance_generic(
        FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, ctx::Frame *frame);

    RuntimeBuildInfo buildRuntimeBuildInfo(camel::runtime::GCGraph *graph);

    template <typename FlowT>
    void buildRuntimeNodeTasks(
        FlowT &flowLike, camel::runtime::GCGraph *graph, ctx::Frame *frame,
        const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
        std::vector<uint8_t> &taskBuilt);

    template <typename FlowT>
    void connectRuntimeDependencies(
        FlowT &flowLike, camel::runtime::GCGraph *graph, ctx::Frame *frame,
        const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
        std::vector<uint8_t> &taskBuilt);

    template <typename FlowT>
    tf::Task buildDataTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildPortTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCopyTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCastTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildFillTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildAccsTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildFuncTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCallTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildOperTask(
        FlowT &flowLike, camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef,
        ctx::Frame *frame);

    template <typename FlowT>
    void buildRuntimeBranchJoinRegion(
        FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, ctx::Frame *frame,
        const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
        std::vector<uint8_t> &taskBuilt, camel::runtime::gc_node_ref_t brchRuntimeIndex);

    void mark_map_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    void mark_filter_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    void mark_reduce_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    void mark_foreach_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    void mark_unordered_foreach_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    void mark_unordered_reduce_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    // The fault-isolation node: calls the function value given first on the calling thread, in
    // a fault domain of its own, and turns a fault raised in it into the node's error value.
    slot_t executeIsolateNode(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
};

class TfDumpPass : public GraphIRPass {
  public:
    TfDumpPass(const ctx::context_ptr_t &ctx) : GraphIRPass(ctx) {}
    ~TfDumpPass() override = default;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
