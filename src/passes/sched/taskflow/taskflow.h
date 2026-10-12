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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
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
        std::mutex mutex;
        std::vector<Frame *> freeFrames;
        std::unordered_set<Frame *> activeFrames;
        std::vector<std::byte *> chunks;
    };

    GraphArena &getOrCreateArena(camel::runtime::GCGraph *graph);
    void allocateChunk(GraphArena &arena, size_t minFrameCount);
    void clearGcSlots(Frame *frame) const;
    void
    traceActiveFrames(const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate);
    static uintptr_t arenaKey(camel::runtime::GCGraph *graph);

    size_t chunkBytes_;
    size_t minChunkFrames_;
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
// The part of a run that one fault ends (docs/technical/32_fault_isolation.md, 3.6). The run has
// a root domain; every fault-isolation node opens one for the call it makes. A domain holds the
// first fault raised in it. Tasks belong to the domain of the thread that started them
// (ThreadState::faultDomain).
struct TaskflowFaultDomain {
    // Fault-isolation boundaries open around the domain (0: the root domain).
    uint32_t isolationDepth = 0;
    std::mutex mutex;
    // The first fault of the domain, the only one reported for it.
    std::exception_ptr firstFault;
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
    TaskflowGcGate::ThreadState &thread() { return ts_; }

  private:
    TaskflowGcGate &gate_;
    TaskflowGcGate::ThreadState &ts_;
    bool held_ = false;
};

class TaskflowExecSchedPass : public RuntimeGraphSchedulePass {
  public:
    /// `max_concurrent_tasks` = 0: the thread limit of the context (`-t`), else the number of
    /// hardware threads.
    TaskflowExecSchedPass(const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks = 0)
        : RuntimeGraphSchedulePass(ctx), executor_(workerCount(ctx, max_concurrent_tasks)) {
        if (ctx) {
            const auto limit   = camel::execute::recursionLimit(*ctx);
            maxRecursionDepth_ = limit.given ? limit.frames : 0;
        }
    }
    ~TaskflowExecSchedPass() override = default;

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
        bool isTailCall(camel::runtime::gc_node_ref_t ref) const {
            return std::binary_search(tailCalls.begin(), tailCalls.end(), ref);
        }
    };

    std::shared_mutex plansMutex_;
    std::unordered_map<camel::runtime::GCGraph *, std::unique_ptr<GraphPlan>> plans_;
    // The fault domain of everything outside a fault-isolation boundary. A failure in it ends
    // the run.
    TaskflowFaultDomain rootDomain_;
    TaskflowFaultDomain &domainOf(const TaskflowGcGate::ThreadState &thread) {
        return thread.faultDomain ? *thread.faultDomain : rootDomain_;
    }
    // What a failure stops, for a fault that is isolated and for one that ends the run alike:
    // DESIGN-QUESTION(attempt-parallel-siblings): tasks that have started run to their end
    // (with everything they start), tasks of the same flow that have not started do not
    // start, and whoever started the flow waits for all that started before the failure goes
    // on. There is no preemption and no flag polled by running code: the executor skips the
    // tasks of a flow one of whose tasks threw, and a scope skips its remaining chains
    // (FlowInstance::failed).

    // The limit on calls open at once that are not tail calls, when one was given by the
    // option or the environment; 0 when not: the native stack alone then bounds a recursion
    // (camel/execute/limits.h). The count follows a call across threads: a task continues
    // from the open calls of the thread that started it (ThreadState::callDepth).
    size_t maxRecursionDepth_ = 0;
    // Reports `fault` at node `nodeRef` of `graph` (at the graph when the node has no source
    // position) unless a fault was reported already in the fault domain of the calling thread,
    // and unwinds. Element functions that run concurrently can fault together: only the first
    // fault of a domain is reported (it ends the program, or becomes the error value of the
    // fault-isolation node), and every later one unwinds with what reporting the first threw.
    // DESIGN-QUESTION(attempt-parallel-first-fault): which is first is not defined.
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
    // Scopes run as task graphs since the pass started (reported with CAMEL_TF_GC_STATS).
    std::atomic<uint64_t> taskGraphsRun_{0};

    GraphPlan *planFor(camel::runtime::GCGraph *graph);
    std::unique_ptr<GraphPlan> buildGraphPlan(camel::runtime::GCGraph *graph);
    void buildStructuredPlan(GraphPlan &plan);

    slot_t evalGraphTF(camel::runtime::GCGraph *graph, ctx::Frame *frame);
    // Evaluates `graph` on `frame`, tail calls included. May be entered inside `region`;
    // returns inside it, so the caller stores the result before a collection can run.
    slot_t evalGraph(camel::runtime::GCGraph *graph, ctx::Frame *frame, TaskflowGcRegion &region);
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
    // `targetGraph`): `fillArgs(span)` reads the arguments and `sink(result)` stores the
    // result, both inside one region of the calling thread. The call is an open call like any
    // other.
    template <typename FillArgs, typename Sink>
    void callClosure(
        const ElementCallSite &site, camel::runtime::GCGraph *targetGraph, ctx::Frame *funcFrame,
        camel::runtime::gc_data_idx_t funcIdx, size_t argCount, FillArgs &&fillArgs,
        Sink &&sink);
    camel::runtime::gc_node_ref_t branchArmEntryForLinear(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef, size_t armIndex,
        std::span<const camel::runtime::gc_node_ref_t> topoNodes);
    slot_t runPreparedSubgraph(tf::Subflow &sf, camel::runtime::GCGraph *graph, ctx::Frame *frame);
    slot_t executeLinearNode(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
    slot_t executeLinearMarkedOperator(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);

    ctx::Frame *acquirePreparedRuntimeCallFrame(
        camel::runtime::GCGraph *targetGraph, camel::runtime::gc_node_ref_t callNodeRuntimeIndex,
        ctx::Frame *sourceFrame);
    ctx::Frame *acquirePreparedClosureCallFrame(
        camel::runtime::GCGraph *targetGraph, ::Tuple *closure, std::span<const slot_t> args);

    slot_t executePreparedOperator(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame);
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
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
    void mark_apply_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
    void mark_filter_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
    void mark_reduce_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
    void mark_foreach_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
    void mark_unordered_foreach_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
    void mark_unordered_reduce_arr(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t nodeRef, ctx::Frame *frame,
        tf::Subflow &sf);
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
