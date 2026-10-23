// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "taskflow.h"
#include "camel/execute/loop_frame.h"
#include "camel/execute/function_call.h"
#include "handoff.h"

#include "camel/core/effect_trace.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/execute/executor.h"
#include "camel/execute/fault_isolation.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/profile/operators.h"
#include "camel/profile/profile.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>

#if defined(__linux__) && defined(__GLIBC__)
#include <pthread.h>
#endif

using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using camel::core::OperatorTable;
namespace oper = camel::core::oper;

namespace {

using runtime_data_idx_t = camel::runtime::gc_data_idx_t;

using camel::execute::RuntimeBranchArmRegion;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

struct TaskflowHigherOrderCallSite {
    GCGraph *runtimeGraph = nullptr;
    Tuple *closure        = nullptr;
};

inline runtime_data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "Taskflow runtime node lookup resolved to null.");
    return node->dataIndex;
}

inline const GCNode *requireNode(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "Taskflow runtime node lookup resolved to null.");
    return node;
}

slot_t readForwardedRuntimeValue(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow forwarded-value read requires a graph.");
    ASSERT(frame != nullptr, "Taskflow forwarded-value read requires a frame.");

    const gc_node_ref_t current = camel::execute::resolveRuntimeForwardedValueRef(graph, nodeRef);
    if (current == kInvalidNodeRef) {
        return NullSlot;
    }
    const auto *node = requireNode(graph, current);
    ASSERT(node->dataIndex != 0, "Taskflow forwarded-value chain resolved to slot 0.");
    return frame->get<slot_t>(node->dataIndex);
}

// The collector moves objects. A task of a higher-order operator therefore does not keep the
// array, the function value or a scratch array by address: it reads them, inside its region,
// from where the collector updates them (the frame of the operator's node, a root handle).
inline Array *arrayAt(Frame *frame, runtime_data_idx_t idx) { return frame->get<Array *>(idx); }
inline Tuple *closureAt(Frame *frame, runtime_data_idx_t idx) {
    Function *func = frame->get<Function *>(idx);
    return func ? func->tuple() : nullptr;
}

TaskflowHigherOrderCallSite
makeHigherOrderCallSite(camel::execute::RunPrograms &programs, Function *func) {
    auto *runtimeGraph = func ? programs.graphOf(func) : nullptr;
    ASSERT(runtimeGraph != nullptr, "Taskflow higher-order call requires a runtime graph target.");
    return {
        .runtimeGraph = runtimeGraph,
        .closure      = func ? func->tuple() : nullptr,
    };
}

} // namespace

TaskflowExecSchedPass::RuntimeBuildInfo
TaskflowExecSchedPass::buildRuntimeBuildInfo(GCGraph *graph) {
    ASSERT(graph != nullptr, "Taskflow build info requires a runtime graph.");

    RuntimeBuildInfo info;
    info.skipNodes.resize(graph->nodeCount(), 0);
    info.branchArms.resize(graph->nodeCount());

    for (gc_node_ref_t nodeRef : graph->execPlan().topo) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind != GCNodeKind::Brch) {
            continue;
        }
        const size_t armCount = graph->armCountOf(nodeRef);
        auto &arms            = info.branchArms[nodeRef];
        arms.reserve(armCount);
        for (size_t armIndex = 0; armIndex < armCount; ++armIndex) {
            auto arm = camel::execute::collectRuntimeBranchArmRegion(graph, nodeRef, armIndex);
            for (gc_node_ref_t bodyRef : arm.topoIndices) {
                if (bodyRef < info.skipNodes.size()) {
                    info.skipNodes[bodyRef] = 1;
                }
            }
            if (arm.joinIndex < info.skipNodes.size()) {
                info.skipNodes[arm.joinIndex] = 1;
            }
            arms.push_back(std::move(arm));
        }
    }

    framePool_.warmup(graph, 1);
    return info;
}

// ---------------------------------------------------------------------------------------------
// Stop-the-world gate
// ---------------------------------------------------------------------------------------------

namespace {

void nativeStackBounds(uintptr_t &low, uintptr_t &high) {
    camel::execute::nativeStackBounds(low, high);
}

// True once the calling thread has used a quarter of its native stack. A scope that would run
// as a nested task graph is then run inline in linear order: a nested task graph costs several
// times the stack of an inline call (the executor's own frames, and whatever other task the
// waiting worker picks up meanwhile), and deep non-tail recursion must not end in a stack
// overflow that the node-by-node evaluator would not have had.
bool nativeStackUsedBeyond(const TaskflowGcGate::ThreadState &ts, unsigned num, unsigned den) {
    if (ts.stackLow == 0) {
        return false; // stack bounds are not read on this platform: no guard
    }
    volatile char marker = 0;
    const uintptr_t here = reinterpret_cast<uintptr_t>(&marker);
    if (here <= ts.stackLow || here > ts.stackHigh) {
        return false;
    }
    return (ts.stackHigh - here) > (ts.stackHigh - ts.stackLow) / den * num;
}

bool nativeStackIsTight(const TaskflowGcGate::ThreadState &ts) {
    return nativeStackUsedBeyond(ts, 1, 4);
}

// The shared native stack guard keeps space for reporting and fault unwinding on each platform.
// This is apart from the count of open calls (requireCallRoom): what a thread can nest
// depends on its stack and on what else the worker picked up while waiting. The depth the
// diagnostic reports is the number of calls open on that thread (a recursion that started on
// another worker is deeper than that).
bool nativeStackIsExhausted(const TaskflowGcGate::ThreadState &) {
    return camel::execute::nativeStackNearlyExhausted();
}

// One more call open on a thread, for as long as the object lives.
struct OpenCall {
    explicit OpenCall(uint32_t &depth) : depth_(depth) { ++depth_; }
    ~OpenCall() { --depth_; }
    uint32_t &depth_;
};

// A task continues from the calls open on the thread that started it, in that thread's fault
// domain: while the object lives, the running thread counts from `depth` and works in `domain`;
// afterwards it has what it had (a worker that picked the task up while waiting inside another
// call).
struct InheritedDepth {
    InheritedDepth(TaskflowGcGate::ThreadState &thread, uint32_t depth, TaskflowFaultDomain *domain)
        : thread_(thread), savedDepth_(thread.callDepth), savedDomain_(thread.faultDomain),
          isolation_(domain ? domain->isolationDepth : 0) {
        thread.callDepth   = depth;
        thread.faultDomain = domain;
    }
    ~InheritedDepth() {
        thread_.callDepth   = savedDepth_;
        thread_.faultDomain = savedDomain_;
    }
    TaskflowGcGate::ThreadState &thread_;
    uint32_t savedDepth_;
    TaskflowFaultDomain *savedDomain_;
    camel::core::error::InheritedFaultIsolation isolation_;
};

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct PlanMismatch {
    std::string reason;
};

} // namespace

namespace {

// std::tf gc_stats=2: one line per collection, and per time the collector gave up. Set by the
// run in progress; the collector's gate is one per process.
std::atomic<bool> &gcTraceFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}
bool gcTraceEnabled() { return gcTraceFlag().load(std::memory_order_relaxed); }

// Resident set of the process in MB (0 where /proc is not available).
long residentMegabytes() {
    long pages = 0;
#if defined(__linux__)
    if (std::FILE *statm = std::fopen("/proc/self/statm", "r")) {
        long total = 0;
        if (std::fscanf(statm, "%ld %ld", &total, &pages) != 2) {
            pages = 0;
        }
        std::fclose(statm);
    }
#endif
    return pages * 4096 / (1024 * 1024);
}

} // namespace

void TaskflowGcGate::resetStats() {
    for (auto *counter :
         {&stats_.pendingPolls,
          &stats_.collections,
          &stats_.collectionsInFlow,
          &stats_.emptySafepoints,
          &stats_.cappedPolls,
          &stats_.gaveUp,
          &stats_.backoffPolls}) {
        counter->store(0, std::memory_order_relaxed);
    }
    for (auto *counter : {&stats_.waitNs, &stats_.maxWaitNs, &stats_.collectNs}) {
        counter->store(0, std::memory_order_relaxed);
    }
}

void TaskflowGcGate::reportStats(std::ostream &os) const {
    const auto heap = camel::core::mm::autoSpace().stats();
    os << std::format(
        "[taskflow] gc: pending_polls={} collections={} in_flow={} empty={} capped={} "
        "gave_up={} backoff={} wait_ms={:.1f} max_wait_ms={:.1f} collect_ms={:.1f} "
        "heap_major={} heap_requested={} heap_deferred={} heap_elder_overflow={} rss_mb={}\n",
        stats_.pendingPolls.load(),
        stats_.collections.load(),
        stats_.collectionsInFlow.load(),
        stats_.emptySafepoints.load(),
        stats_.cappedPolls.load(),
        stats_.gaveUp.load(),
        stats_.backoffPolls.load(),
        static_cast<double>(stats_.waitNs.load()) / 1e6,
        static_cast<double>(stats_.maxWaitNs.load()) / 1e6,
        static_cast<double>(stats_.collectNs.load()) / 1e6,
        heap.majorCollections,
        heap.requestedCollections,
        heap.deferredCollections,
        heap.elderOverflowAllocations,
        residentMegabytes());
}

TaskflowGcGate &TaskflowGcGate::instance() {
    // Never destroyed: worker threads may still hold their state while statics are torn down.
    static TaskflowGcGate *gate = new TaskflowGcGate();
    return *gate;
}

TaskflowGcGate::ThreadState &TaskflowGcGate::self() {
    thread_local ThreadState *state = nullptr;
    if (state == nullptr) {
        auto owned = std::make_unique<ThreadState>();
        nativeStackBounds(owned->stackLow, owned->stackHigh);
        std::lock_guard lock(registryMutex_);
        state = owned.get();
        threads_.push_back(std::move(owned));
    }
    return *state;
}

void TaskflowGcGate::waitForCollector() {
    // The collector holds this mutex for as long as it keeps the world stopped.
    std::lock_guard lock(collectorMutex_);
}

void TaskflowGcGate::collect(ThreadState &ts, std::string_view reason) {
    using namespace std::chrono_literals;
    // How long the collector waits for the other threads to leave their regions, and how long
    // polls are ignored after it gave up (doubling up to the maximum while it keeps failing).
    constexpr auto kPatience          = 500ms;
    constexpr int64_t kFirstBackoffNs = 10'000'000;
    constexpr int64_t kMaxBackoffNs   = 1'000'000'000;
    static std::atomic<int64_t> backoffNs{kFirstBackoffNs};

    stats_.pendingPolls.fetch_add(1, std::memory_order_relaxed);
    const auto tooSoon = [this]() {
        const int64_t now = steadyNowNs();
        if (now < capUntilNs_.load(std::memory_order_relaxed)) {
            stats_.cappedPolls.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (now < retryAfterNs_.load(std::memory_order_relaxed)) {
            stats_.backoffPolls.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    };
    if (tooSoon()) {
        return;
    }
    std::lock_guard collector(collectorMutex_);
    if (!camel::core::mm::detail::autoSpaceSafepointSlowPath.load(std::memory_order_acquire) ||
        tooSoon()) {
        return; // another thread collected, or gave up, while this one waited for the mutex
    }

    stopRequested_.store(true, std::memory_order_seq_cst);
    bool quiet              = false;
    const int64_t waitBegin = steadyNowNs();
    {
        std::lock_guard registry(registryMutex_);
        const auto deadline = std::chrono::steady_clock::now() + kPatience;
        for (unsigned spins = 0;; ++spins) {
            quiet = true;
            for (const auto &other : threads_) {
                if (other.get() != &ts && other->active.load(std::memory_order_seq_cst)) {
                    quiet = false;
                    break;
                }
            }
            if (quiet || std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            if (spins < 128) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(50us);
            }
        }
    }
    {
        const int64_t waited = steadyNowNs() - waitBegin;
        stats_.waitNs.fetch_add(waited, std::memory_order_relaxed);
        if (waited > stats_.maxWaitNs.load(std::memory_order_relaxed)) {
            stats_.maxWaitNs.store(waited, std::memory_order_relaxed);
        }
    }
    try {
        if (quiet) {
            const auto collectionsSoFar = []() {
                const auto heap = camel::core::mm::autoSpace().stats();
                return heap.minorCollections + heap.majorCollections;
            };
            const uint64_t before = collectionsSoFar();
            const int64_t begin   = steadyNowNs();
            camel::core::mm::safepoint(reason);
            const int64_t end = steadyNowNs();
            stats_.collectNs.fetch_add(end - begin, std::memory_order_relaxed);
            if (const uint64_t ran = collectionsSoFar() - before; ran != 0) {
                stats_.collections.fetch_add(ran, std::memory_order_relaxed);
                if (flowsRunning_.load(std::memory_order_relaxed) > 0) {
                    stats_.collectionsInFlow.fetch_add(ran, std::memory_order_relaxed);
                }
                if (gcTraceEnabled()) {
                    std::cerr << std::format(
                        "[taskflow] gc collection: flows={} waited_ms={:.2f} took_ms={:.2f} "
                        "rss_mb={}\n",
                        flowsRunning_.load(std::memory_order_relaxed),
                        static_cast<double>(begin - waitBegin) / 1e6,
                        static_cast<double>(end - begin) / 1e6,
                        residentMegabytes());
                }
            } else {
                stats_.emptySafepoints.fetch_add(1, std::memory_order_relaxed);
            }
            // The next collection starts no sooner than twice the time this one took, so the
            // collector takes at most a third of the run however often the workers poll
            // (every graph boundary of every worker: with a collection forced at every
            // safepoint, as the GC stress modes do, that was thousands of full collections
            // for a small program). A poll that found nothing to do costs nothing here.
            capUntilNs_.store(end + 2 * (end - begin), std::memory_order_relaxed);
            backoffNs.store(kFirstBackoffNs, std::memory_order_relaxed);
        } else {
            stats_.gaveUp.fetch_add(1, std::memory_order_relaxed);
            if (gcTraceEnabled()) {
                std::cerr << std::format(
                    "[taskflow] gc gave up: flows={} rss_mb={}\n",
                    flowsRunning_.load(std::memory_order_relaxed),
                    residentMegabytes());
            }
            const int64_t wait = backoffNs.load(std::memory_order_relaxed);
            retryAfterNs_.store(steadyNowNs() + wait, std::memory_order_relaxed);
            backoffNs.store(std::min(wait * 2, kMaxBackoffNs), std::memory_order_relaxed);
        }
    } catch (...) {
        stopRequested_.store(false, std::memory_order_seq_cst);
        throw;
    }
    stopRequested_.store(false, std::memory_order_seq_cst);
}

// ---------------------------------------------------------------------------------------------
// Graph plans
// ---------------------------------------------------------------------------------------------

bool TaskflowExecSchedPass::graphIsHeavy(GCGraph *graph) {
    if (graph == nullptr) {
        return true; // unknown callee
    }
    {
        std::lock_guard lock(heavyMutex_);
        if (auto it = heavyGraphs_.find(graph); it != heavyGraphs_.end()) {
            return it->second;
        }
    }
    // Is a graph with a heavy body reachable through direct calls? A plain search: a cycle of
    // light graphs (a recursion) stays light.
    std::vector<GCGraph *> work{graph};
    std::unordered_set<GCGraph *> seen{graph};
    bool heavy = false;
    while (!heavy && !work.empty()) {
        GCGraph *current = work.back();
        work.pop_back();
        for (auto it = current->nodes().begin(); it != current->nodes().end() && !heavy; ++it) {
            const auto *node = *it;
            if (node == nullptr) {
                continue;
            }
            if (node->kind == GCNodeKind::Call) {
                heavy = true;
            } else if (node->kind == GCNodeKind::Oper) {
                const auto *body = current->nodeBodyAs<GCOperBody>(it.ref());
                // Built-in operators have an empty protocol (":op/add_l"); the marked ones
                // (":mark/...") are the higher-order ones.
                const auto &table = OperatorTable::instance();
                heavy =
                    body == nullptr || !table.isBuiltin(body->oper) || table.isMarked(body->oper);
            } else if (node->kind == GCNodeKind::Func) {
                GCGraph *callee = programs_.of(current).calleeOf(*current, it.ref());
                if (callee == nullptr) {
                    heavy = true;
                } else if (seen.insert(callee).second) {
                    work.push_back(callee);
                }
            }
        }
    }
    std::lock_guard lock(heavyMutex_);
    heavyGraphs_[graph] = heavy;
    if (!heavy) {
        // Everything reachable from a light graph is light.
        for (GCGraph *reached : seen) {
            heavyGraphs_[reached] = false;
        }
    }
    return heavy;
}

uint64_t TaskflowExecSchedPass::nextPlanEpoch() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

TaskflowExecSchedPass::GraphPlan *TaskflowExecSchedPass::planFor(GCGraph *graph) {
    ASSERT(graph != nullptr, "Taskflow plan lookup requires a runtime graph.");
    // Every call looks the plan of its callee up. The plans a thread used last are remembered
    // per thread, so that the table and its lock, which all workers share, are only reached
    // the first time a thread meets a graph in a run.
    struct CachedPlan {
        uint64_t epoch  = 0;
        GCGraph *graph  = nullptr;
        GraphPlan *plan = nullptr;
    };
    thread_local std::array<CachedPlan, 128> cache{};
    const auto key     = reinterpret_cast<uintptr_t>(graph);
    CachedPlan &cached = cache[(key >> 4 ^ key >> 11 ^ key >> 18) % cache.size()];
    if (cached.epoch == planEpoch_ && cached.graph == graph) [[likely]] {
        return cached.plan;
    }
    GraphPlan *plan = nullptr;
    {
        std::shared_lock lock(plansMutex_);
        if (auto it = plans_.find(graph); it != plans_.end()) {
            plan = it->second.get();
        }
    }
    if (plan == nullptr) {
        // Built outside the lock; two workers that meet a graph for the first time may both
        // build it, and the first to publish wins. A published plan is never changed or
        // removed while the run lasts.
        auto built = buildGraphPlan(graph);
        std::unique_lock lock(plansMutex_);
        auto [it, inserted] = plans_.try_emplace(graph, std::move(built));
        (void)inserted;
        plan = it->second.get();
    }
    cached = {planEpoch_, graph, plan};
    return plan;
}

bool TaskflowExecSchedPass::boundedLight(GraphPlan &plan) {
    if (const int8_t known = plan.boundedLightState.load(std::memory_order_relaxed); known >= 0) {
        return known != 0;
    }
    // Light (graphIsHeavy) and without a cycle of calls. Two workers may both work it out;
    // they find the same.
    bool bounded = !graphIsHeavy(plan.graph);
    if (bounded) {
        // 1: being visited (reaching it again is a recursion), 2: done.
        std::unordered_map<GCGraph *, uint8_t> state;
        std::function<bool(GCGraph *)> acyclic = [&](GCGraph *current) {
            if (state.size() > 256) {
                return false; // not worth more: taken as unbounded
            }
            state[current] = 1;
            for (auto it = current->nodes().begin(); it != current->nodes().end(); ++it) {
                const auto *node = *it;
                if (node == nullptr || node->kind != GCNodeKind::Func) {
                    continue;
                }
                GCGraph *callee = programs_.of(current).calleeOf(*current, it.ref());
                if (callee == nullptr) {
                    return false;
                }
                const auto found = state.find(callee);
                if (found == state.end() ? !acyclic(callee) : found->second == 1) {
                    return false;
                }
            }
            state[current] = 2;
            return true;
        };
        bounded = acyclic(plan.graph);
    }
    plan.boundedLightState.store(bounded ? 1 : 0, std::memory_order_relaxed);
    return bounded;
}

std::unique_ptr<TaskflowExecSchedPass::GraphPlan>
TaskflowExecSchedPass::buildGraphPlan(GCGraph *graph) {
    auto plan   = std::make_unique<GraphPlan>();
    plan->graph  = graph;
    plan->shared = &graph->execPlan();
    plan->topo   = plan->shared->topo;

    // Tail position, by the rule every back end shares (ExecPlan::tailCalls).
    plan->lastNode       = plan->shared->tailValueRef;
    plan->lastNodeIsJoin = plan->shared->tailValueIsJoin;
    plan->anchorOk       = plan->shared->tailAnchorOk;
    plan->tailCalls      = plan->shared->tailCalls;

    // Where each operator of the graph reads its arguments, and room for the operator itself
    // once it has been resolved (executePreparedOperator).
    plan->opers.resize(graph->nodeCount());
    for (gc_node_ref_t ref : plan->topo) {
        if (ref >= plan->opers.size() || requireNode(graph, ref)->kind != GCNodeKind::Oper) {
            continue;
        }
        PreparedOper &oper = plan->opers[ref];
        oper.withBegin     = static_cast<uint32_t>(plan->operArgs.size());
        for (gc_node_ref_t inputRef : graph->withInputsOf(ref)) {
            plan->operArgs.push_back(dataIndexOf(graph, inputRef));
        }
        oper.normBegin = static_cast<uint32_t>(plan->operArgs.size());
        for (gc_node_ref_t inputRef : graph->normInputsOf(ref)) {
            plan->operArgs.push_back(dataIndexOf(graph, inputRef));
        }
        oper.end      = static_cast<uint32_t>(plan->operArgs.size());
        oper.prepared = true;
    }

    if (!forceLinear_) {
        try {
            buildStructuredPlan(*plan);
            plan->structured = true;
        } catch (const PlanMismatch &mismatch) {
            plan->scopes.clear();
            plan->branches.clear();
            plan->structured = false;
            if (options_.diag) {
                std::cerr << std::format(
                    "[taskflow] graph '{}' is evaluated node by node: {}\n",
                    graph->name(),
                    mismatch.reason);
            }
        }
    }
    if (plan->structured && options_.diag) {
        size_t parallelScopes = 0;
        size_t chains         = 0;
        for (const auto &scope : plan->scopes) {
            if (!scope->sequential) {
                ++parallelScopes;
                chains += scope->chains.size();
            }
        }
        std::cerr << std::format(
            "[taskflow] graph '{}': {} scope(s), {} run as task graphs ({} tasks)\n",
            graph->name(),
            plan->scopes.size(),
            parallelScopes,
            chains);
    }
    return plan;
}

void TaskflowExecSchedPass::buildStructuredPlan(GraphPlan &plan) {
    GCGraph *graph         = plan.graph;
    const auto &topo       = plan.topo;
    const size_t blocks    = graph->nodeCount();
    constexpr uint32_t kNo = std::numeric_limits<uint32_t>::max();

    // The rounds of a loop run in the frame, in the order of the plan: a graph with one is
    // evaluated node by node (and, when it is serial, as bytecode).
    if (!plan.shared->loops.empty()) {
        throw PlanMismatch{"it has a loop"};
    }
    std::vector<uint32_t> pos(blocks, kNo);
    for (size_t i = 0; i < topo.size(); ++i) {
        if (topo[i] >= blocks) {
            throw PlanMismatch{"a node ref lies outside the node table"};
        }
        pos[topo[i]] = static_cast<uint32_t>(i);
    }
    const uint32_t anchorPos = plan.anchorOk && plan.lastNode < blocks ? pos[plan.lastNode] : kNo;

    const auto isElided = [](GCNodeKind kind) {
        return kind == GCNodeKind::Data || kind == GCNodeKind::Port || kind == GCNodeKind::Sync;
    };

    // Where each executable node lives: its scope and its item there. BRCH and JOIN map to the
    // item of their region.
    struct Loc {
        int32_t scope = -1;
        uint32_t item = 0;
    };
    std::vector<Loc> loc(blocks);

    const auto newScope = [&](int32_t parent, uint32_t parentItem) {
        auto scope         = std::make_unique<PlanScope>();
        scope->parentScope = parent;
        scope->parentItem  = parentItem;
        scope->depth       = parent < 0 ? 0 : plan.scopes[static_cast<size_t>(parent)]->depth + 1;
        plan.scopes.push_back(std::move(scope));
        return static_cast<int32_t>(plan.scopes.size() - 1);
    };
    const auto place = [&](gc_node_ref_t ref, int32_t scope, uint32_t item) {
        if (loc[ref].scope >= 0) {
            throw PlanMismatch{"a node belongs to two branch arms"};
        }
        loc[ref] = Loc{scope, item};
    };

    // The scope tree, read off the linear order exactly as the node-by-node evaluator walks
    // it: an arm is the range from its entry to its tail, the JOIN follows the arms, and what
    // lies between a BRCH and its JOIN outside every arm range never runs.
    std::function<void(int32_t, size_t, size_t)> parse =
        [&](int32_t scopeIdx, size_t lo, size_t hi) {
            for (size_t i = lo; i < hi;) {
                const gc_node_ref_t ref = topo[i];
                const auto *node        = requireNode(graph, ref);
                if (isElided(node->kind)) {
                    ++i;
                    continue;
                }
                PlanItem item;
                item.node          = ref;
                auto &items        = plan.scopes[static_cast<size_t>(scopeIdx)]->items;
                const auto itemIdx = static_cast<uint32_t>(items.size());

                if (node->kind == GCNodeKind::Join) {
                    throw PlanMismatch{"a JOIN is reached outside its branch region"};
                }
                if (node->kind == GCNodeKind::Brch) {
                    const auto *body         = graph->nodeBodyAs<camel::runtime::GCBrchBody>(ref);
                    const gc_node_ref_t join = body ? body->join : kInvalidNodeRef;
                    if (join >= blocks || pos[join] == kNo) {
                        throw PlanMismatch{"a BRCH has no reachable JOIN"};
                    }
                    const size_t joinPos = pos[join];
                    if (joinPos <= i || joinPos >= hi) {
                        throw PlanMismatch{"a JOIN lies outside the range of its BRCH"};
                    }
                    const auto branchIdx = static_cast<uint32_t>(plan.branches.size());
                    plan.branches.push_back(PlanBranch{.brch = ref, .join = join, .armScopes = {}});
                    item.kind      = ItemKind::Branch;
                    item.branch    = branchIdx;
                    item.afterTail = plan.anchorOk && join == plan.lastNode;
                    items.push_back(item);
                    place(ref, scopeIdx, itemIdx);
                    place(join, scopeIdx, itemIdx);

                    const size_t armCount = graph->armCountOf(ref);
                    std::vector<int32_t> armScopes;
                    armScopes.reserve(armCount);
                    for (size_t arm = 0; arm < armCount; ++arm) {
                        const gc_node_ref_t entry =
                            graph->execPlan().branchArmEntriesOfNode(ref)[arm];
                        if (entry == join) {
                            armScopes.push_back(-1);
                            continue;
                        }
                        const gc_node_ref_t tail =
                            graph->execPlan().branchArmTailsOfNode(ref)[arm];
                        if (entry >= blocks || tail >= blocks || pos[entry] == kNo ||
                            pos[tail] == kNo) {
                            throw PlanMismatch{"a branch arm is not in the linear order"};
                        }
                        const size_t entryPos = pos[entry];
                        const size_t tailPos  = pos[tail];
                        if (!(i < entryPos && entryPos <= tailPos && tailPos < joinPos)) {
                            throw PlanMismatch{"a branch arm is not a range between BRCH and JOIN"};
                        }
                        const int32_t armScope = newScope(scopeIdx, itemIdx);
                        parse(armScope, entryPos, tailPos + 1);
                        armScopes.push_back(armScope);
                    }
                    plan.branches[branchIdx].armScopes = std::move(armScopes);
                    i                                  = joinPos + 1;
                    continue;
                }

                switch (node->kind) {
                case GCNodeKind::Func:
                    item.kind     = ItemKind::Call;
                    item.tailCall = plan.isTailCall(ref);
                    break;
                case GCNodeKind::Call:
                    item.kind = ItemKind::Call;
                    break;
                case GCNodeKind::Oper: {
                    const auto *body  = graph->nodeBodyAs<GCOperBody>(ref);
                    const auto &table = OperatorTable::instance();
                    item.kind         = table.isMarked(body->oper)      ? ItemKind::Mark
                                        : table.callsGraphs(body->oper) ? ItemKind::NativeCall
                                                                        : ItemKind::Leaf;
                    item.leaveAfter   = !table.isBuiltin(body->oper);
                    break;
                }
                default:
                    item.kind = ItemKind::Leaf;
                    break;
                }
                item.afterTail = anchorPos != kNo && i > anchorPos;
                items.push_back(item);
                place(ref, scopeIdx, itemIdx);
                ++i;
            }
        };
    (void)newScope(-1, 0);
    parse(0, 0, topo.size());

    // What lies behind a tail call (the GATEs that forward its value, the JOINs of the nested
    // branches it sits in) is skipped once that call has been taken: the frame is refilled for
    // the callee and there is no value to forward or join.
    {
        std::vector<gc_node_ref_t> work(plan.tailCalls.begin(), plan.tailCalls.end());
        std::vector<uint8_t> seen(blocks, 0);
        while (!work.empty()) {
            const gc_node_ref_t ref = work.back();
            work.pop_back();
            for (auto outputs :
                 {graph->normOutputsOf(ref),
                  graph->withOutputsOf(ref),
                  graph->ctrlOutputsOf(ref)}) {
                for (gc_node_ref_t out : outputs) {
                    if (out >= blocks || seen[out] != 0) {
                        continue;
                    }
                    seen[out] = 1;
                    work.push_back(out);
                    if (loc[out].scope >= 0) {
                        plan.scopes[static_cast<size_t>(loc[out].scope)]
                            ->items[loc[out].item]
                            .afterTail = true;
                    }
                }
            }
        }
    }

    // The executable nodes a node waits for, seen through the nodes that execute nothing.
    std::vector<std::vector<gc_node_ref_t>> waitsFor(blocks);
    std::vector<uint8_t> waitsForDone(blocks, 0);
    std::function<const std::vector<gc_node_ref_t> &(gc_node_ref_t)> collectWaits =
        [&](gc_node_ref_t ref) -> const std::vector<gc_node_ref_t> & {
        if (waitsForDone[ref] != 0) {
            return waitsFor[ref];
        }
        waitsForDone[ref] = 1;
        std::vector<gc_node_ref_t> out;
        for (auto inputs :
             {graph->ctrlInputsOf(ref), graph->normInputsOf(ref), graph->withInputsOf(ref)}) {
            for (gc_node_ref_t input : inputs) {
                if (input >= blocks) {
                    continue;
                }
                if (loc[input].scope >= 0) {
                    out.push_back(input);
                } else if (graph->node(input) != nullptr) {
                    // A node that executes nothing: one of the elided kinds, or a node in the
                    // range of a branch that no arm covers. The second kind matters: an arm
                    // reads a value of the enclosing scope through a GATE placed there (the
                    // GATE never runs; its reader resolves the forwarded value), so the
                    // reader waits for whatever the GATE waits for.
                    const auto &inner = collectWaits(input);
                    out.insert(out.end(), inner.begin(), inner.end());
                }
            }
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        waitsFor[ref] = std::move(out);
        return waitsFor[ref];
    };

    // Every edge orders two items of the innermost scope that contains both of its ends.
    std::vector<std::vector<std::vector<uint32_t>>> succs(plan.scopes.size());
    for (size_t s = 0; s < plan.scopes.size(); ++s) {
        succs[s].resize(plan.scopes[s]->items.size());
    }
    const auto parentOf = [&](Loc at) {
        const auto &scope = *plan.scopes[static_cast<size_t>(at.scope)];
        return Loc{scope.parentScope, scope.parentItem};
    };
    const auto depthOf = [&](Loc at) { return plan.scopes[static_cast<size_t>(at.scope)]->depth; };
    for (gc_node_ref_t ref : topo) {
        if (loc[ref].scope < 0) {
            continue;
        }
        for (gc_node_ref_t input : collectWaits(ref)) {
            Loc from = loc[input];
            Loc to   = loc[ref];
            while (depthOf(from) > depthOf(to)) {
                from = parentOf(from);
            }
            while (depthOf(to) > depthOf(from)) {
                to = parentOf(to);
            }
            while (from.scope != to.scope) {
                from = parentOf(from);
                to   = parentOf(to);
            }
            if (from.item == to.item) {
                continue; // inside one item: BRCH -> arm, arm -> JOIN, or two arms of one branch
            }
            if (from.item > to.item) {
                throw PlanMismatch{"an edge runs against the linear order"};
            }
            succs[static_cast<size_t>(from.scope)][from.item].push_back(to.item);
        }
    }

    // Granularity (see graphIsHeavy in the header): which items are worth a task. Arm scopes
    // have larger indices than the scope of their branch, so they are decided first.
    std::vector<std::vector<uint8_t>> worthTask(plan.scopes.size());
    std::vector<uint8_t> scopeHasWorthyItem(plan.scopes.size(), 0);
    for (size_t s = plan.scopes.size(); s-- > 0;) {
        const PlanScope &scope = *plan.scopes[s];
        worthTask[s].resize(scope.items.size(), 0);
        for (size_t i = 0; i < scope.items.size(); ++i) {
            const PlanItem &item = scope.items[i];
            bool worth           = false;
            switch (item.kind) {
            case ItemKind::Mark:
            case ItemKind::NativeCall:
                worth = true;
                break;
            case ItemKind::Call:
                worth = requireNode(graph, item.node)->kind != GCNodeKind::Func ||
                        graphIsHeavy(programs_.of(graph).calleeOf(*graph, item.node));
                break;
            case ItemKind::Branch:
                for (int32_t armScope : plan.branches[item.branch].armScopes) {
                    worth = worth ||
                            (armScope >= 0 && scopeHasWorthyItem[static_cast<size_t>(armScope)]);
                }
                break;
            case ItemKind::Leaf:
                break;
            }
            worthTask[s][i]       = worth ? 1 : 0;
            scopeHasWorthyItem[s] = scopeHasWorthyItem[s] || worth;
        }
    }

    for (size_t s = 0; s < plan.scopes.size(); ++s) {
        PlanScope &scope = *plan.scopes[s];
        auto &next       = succs[s];
        const size_t n   = scope.items.size();
        for (auto &list : next) {
            std::sort(list.begin(), list.end());
            list.erase(std::unique(list.begin(), list.end()), list.end());
        }
        // Totally ordered: each item is ordered before the one that follows it.
        scope.sequential = true;
        for (size_t i = 0; i + 1 < n; ++i) {
            if (!std::binary_search(next[i].begin(), next[i].end(), static_cast<uint32_t>(i + 1))) {
                scope.sequential = false;
                break;
            }
        }
        if (scope.sequential) {
            continue;
        }

        // Transitive reduction, so that a chain is recognised as one even when the graph also
        // carries the edges its path implies.
        bool twoUnorderedWorthyItems = n > 4096 || fanOutAll_;
        if (n <= 4096) {
            const size_t words = (n + 63) / 64;
            std::vector<uint64_t> reach(n * words, 0);
            for (size_t i = n; i-- > 0;) {
                uint64_t *mine = reach.data() + i * words;
                std::vector<uint32_t> kept;
                for (uint32_t succ : next[i]) {
                    if ((mine[succ / 64] >> (succ % 64)) & 1U) {
                        continue;
                    }
                    kept.push_back(succ);
                    mine[succ / 64] |= uint64_t{1} << (succ % 64);
                    const uint64_t *theirs = reach.data() + static_cast<size_t>(succ) * words;
                    for (size_t w = 0; w < words; ++w) {
                        mine[w] |= theirs[w];
                    }
                }
                next[i] = std::move(kept);
            }
            // reach[i] now holds everything ordered after item i.
            std::vector<uint32_t> worthy;
            for (size_t i = 0; i < n && !twoUnorderedWorthyItems; ++i) {
                if (worthTask[s][i] == 0) {
                    continue;
                }
                for (uint32_t earlier : worthy) {
                    const uint64_t *after = reach.data() + static_cast<size_t>(earlier) * words;
                    if (((after[i / 64] >> (i % 64)) & 1U) == 0) {
                        twoUnorderedWorthyItems = true;
                        break;
                    }
                }
                worthy.push_back(static_cast<uint32_t>(i));
            }
        }
        if (!twoUnorderedWorthyItems) {
            // Nothing here is worth a task of its own next to another: run inline.
            scope.sequential = true;
            continue;
        }

        std::vector<uint32_t> predCount(n, 0);
        std::vector<uint32_t> onlyPred(n, kNo);
        for (size_t i = 0; i < n; ++i) {
            for (uint32_t succ : next[i]) {
                ++predCount[succ];
                onlyPred[succ] = static_cast<uint32_t>(i);
            }
        }
        std::vector<uint32_t> chainOf(n, kNo);
        for (size_t i = 0; i < n; ++i) {
            if (predCount[i] == 1 && next[onlyPred[i]].size() == 1) {
                chainOf[i] = chainOf[onlyPred[i]];
            } else {
                chainOf[i] = static_cast<uint32_t>(scope.chains.size());
                scope.chains.emplace_back();
            }
            scope.chains[chainOf[i]].push_back(scope.items[i]);
        }
        for (size_t i = 0; i < n; ++i) {
            for (uint32_t succ : next[i]) {
                if (chainOf[i] != chainOf[succ]) {
                    scope.chainEdges.emplace_back(chainOf[i], chainOf[succ]);
                }
            }
        }
        std::sort(scope.chainEdges.begin(), scope.chainEdges.end());
        scope.chainEdges.erase(
            std::unique(scope.chainEdges.begin(), scope.chainEdges.end()),
            scope.chainEdges.end());
    }
}

// ---------------------------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------------------------

const camel::runtime::Program *
TaskflowExecSchedPass::apply(const camel::runtime::Program *program, std::ostream & /*os*/) {
    camel::runtime::GCGraph *graph = program->root();
    ASSERT(graph != nullptr, "Taskflow requires a non-null runtime root graph.");
    if (camel::profile::enabled()) {
        camel::profile::setBackend("std::taskflow");
    }
    camel::execute::RunScope runScope(programs_, *context_, program);

    forceLinear_ = options_.linear;
    fanOutAll_   = options_.fanOutAll;
    gcTraceFlag().store(options_.gcStats >= 2, std::memory_order_relaxed);
    taskGraphsRun_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(heavyMutex_);
        heavyGraphs_.clear();
    }
    {
        std::unique_lock lock(plansMutex_);
        plans_.clear();
        planEpoch_ = nextPlanEpoch();
    }
    linearArmEntryCache_.clear();
    const bool gcStats = options_.gcStats > 0;
    TaskflowGcGate::instance().resetStats();
    prepareBytecodeHandoff(graph);
    // The bytecode side of the run, on the line after the collector's.
    const auto reportHandoff = [this]() {
        if (handoff_) {
            const auto stats = handoff_->stats();
            std::cerr << std::format(
                "[taskflow] bytecode: graphs={} bytecodes={} calls={} interpreters={}\n",
                stats.graphs,
                stats.bytecodes,
                stats.calls,
                stats.interpreters);
        } else {
            std::cerr << "[taskflow] bytecode: off\n";
        }
    };
    try {
        slot_t result = NullSlot;
        {
            // The entry graph is called like any other graph: a program with nothing to
            // parallelise runs as bytecode from here.
            TaskflowGcRegion region;
            result = callGraph(graph, nullptr, region, [](Frame *) {});
        }
        context_->captureProcessExitCode(graph, result);
    } catch (Diagnostic &fault) {
        if (gcStats) {
            TaskflowGcGate::instance().reportStats(std::cerr);
            reportHandoff();
        }
        // The one fault that ends the run is recorded here, after everything that had started
        // has ended (raiseFault only describes a fault). Recording gives the diagnostic back
        // to be thrown, or throws itself when the sink stops at the first error.
        auto reporter = context_->runtimeErrorReporter();
        if (!fault.persisted && reporter && reporter->sink()) {
            throw reporter->sink()->add(std::move(fault));
        }
        throw;
    } catch (...) {
        if (gcStats) {
            TaskflowGcGate::instance().reportStats(std::cerr);
            reportHandoff();
        }
        throw;
    }
    if (gcStats) {
        TaskflowGcGate::instance().reportStats(std::cerr);
        reportHandoff();
        std::cerr << std::format(
            "[taskflow] run: task_graphs={} workers={}\n",
            taskGraphsRun_.load(std::memory_order_relaxed),
            executor_.num_workers());
    }
    return nullptr;
}

slot_t TaskflowExecSchedPass::evalGraphTF(GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow evaluation requires a runtime graph.");
    TaskflowGcRegion region;
    return evalGraph(graph, frame, region);
}

slot_t TaskflowExecSchedPass::evalGraph(
    GCGraph *graph, Frame *frame, TaskflowGcRegion &region, GraphPlan *plan) {
    ASSERT(graph != nullptr, "Taskflow evaluation requires a runtime graph.");
    Frame *currFrame = frame;
    if (plan == nullptr) {
        plan = planFor(graph);
    }
    std::atomic<uint32_t> tailNode{kInvalidNodeRef};

    // The frames this activation runs tail calls in, one per graph, with the plan of the graph:
    // [0] is the caller's frame, the others were taken here and go back to the pool when the
    // activation ends. Graphs that tail-call each other in a cycle (a recursive function whose
    // `if` arms are graphs of their own makes a cycle of three or four) then alternate between
    // the frames they already have. With a single spare frame every step of a cycle of three
    // gave a frame back and took another, and looked its plan up again: two mutexes of the
    // pool and the lock of the plan table per tail call, shared by all workers.
    struct TailFrames {
        struct Entry {
            GCGraph *graph  = nullptr;
            Frame *frame    = nullptr;
            GraphPlan *plan = nullptr;
        };
        TaskflowFramePool &pool;
        std::array<Entry, 4> entries{};
        size_t count  = 0;
        size_t victim = 0;
        ~TailFrames() {
            for (size_t i = 1; i < count; ++i) {
                pool.release(entries[i].frame);
            }
        }
    } tailFrames{.pool = framePool_};
    tailFrames.entries[0] = {graph, frame, plan};
    tailFrames.count      = 1;

    for (;;) {
        // Nothing of this graph instance is in flight here: every live value is in a frame.
        region.safepoint("taskflow graph boundary");

        if (plan->structured) {
            runScope(*plan, *plan->scopes.front(), currFrame, tailNode, region);
        } else {
            runLinearBody(*plan, currFrame, tailNode, region);
        }

        const gc_node_ref_t tailRef = tailNode.load(std::memory_order_acquire);
        if (tailRef == kInvalidNodeRef) {
            break;
        }
        tailNode.store(kInvalidNodeRef, std::memory_order_relaxed);

        // Tail call: the frame protocol of the node-by-node evaluator. A self call refills
        // the frame in place; two graphs calling each other alternate between two frames.
        region.enter();
        GCGraph *callerGraph = plan->graph;
        GCGraph *target      = programs_.of(callerGraph).calleeOf(*callerGraph, tailRef);
        ASSERT(target != nullptr, "Taskflow tail FUNC target graph is null.");
        GraphPlan *targetPlan = planFor(target);
        target                = targetPlan->graph;
        Frame *lastFrame      = currFrame;
        if (target == callerGraph) {
            camel::execute::fillFrameForDirectInvoke(lastFrame, currFrame, callerGraph, tailRef);
            continue;
        }
        TailFrames::Entry *entry = nullptr;
        for (size_t i = 0; i < tailFrames.count; ++i) {
            if (tailFrames.entries[i].graph == target) {
                entry = &tailFrames.entries[i];
                break;
            }
        }
        if (entry == nullptr) {
            if (targetPlan->bytecodeEntry != kNoBytecode) {
                // A tail call of a serial graph: the rest of this activation is the callee, and
                // the callee runs as bytecode. Its value is this activation's value. The call
                // replaces the one open for this activation, so the count is the thread's as
                // it stands. (A serial graph tail-calls serial graphs only: nothing comes back
                // here.)
                return handoff_->call(targetPlan->bytecodeEntry, target, region, [&](Frame *dest) {
                    camel::execute::fillFrameForDirectInvoke(lastFrame, dest, callerGraph, tailRef);
                });
            }
            if (tailFrames.count < tailFrames.entries.size()) {
                entry = &tailFrames.entries[tailFrames.count];
            } else {
                // Full: replace one that is neither the caller's frame nor the frame the
                // arguments are about to be read from.
                do {
                    tailFrames.victim = tailFrames.victim % (tailFrames.entries.size() - 1) + 1;
                    entry             = &tailFrames.entries[tailFrames.victim];
                } while (entry->frame == lastFrame);
                framePool_.release(entry->frame);
                // Keep the table consistent should the two calls below throw.
                *entry = tailFrames.entries[tailFrames.count - 1];
                --tailFrames.count;
                entry = &tailFrames.entries[tailFrames.count];
            }
            *entry = {target, framePool_.acquire(target), targetPlan};
            ++tailFrames.count;
        }
        plan      = entry->plan;
        currFrame = entry->frame;
        camel::execute::fillFrameForDirectInvoke(lastFrame, currFrame, callerGraph, tailRef);
    }

    region.enter();
    return camel::execute::readRuntimeGraphReturn(plan->graph, currFrame);
}

void TaskflowExecSchedPass::runScope(
    GraphPlan &plan, PlanScope &scope, Frame *frame, std::atomic<uint32_t> &tailNode,
    TaskflowGcRegion &region) {
    if (scope.sequential || nativeStackIsTight(region.thread())) {
        runItems(plan, scope.items, frame, tailNode, region);
        return;
    }

    std::unique_ptr<FlowInstance> instance;
    {
        std::lock_guard lock(scope.poolMutex);
        if (!scope.idle.empty()) {
            instance = std::move(scope.idle.back());
            scope.idle.pop_back();
        }
    }
    if (!instance) {
        instance           = std::make_unique<FlowInstance>();
        FlowInstance *self = instance.get();
        std::vector<tf::Task> tasks;
        tasks.reserve(scope.chains.size());
        for (size_t chain = 0; chain < scope.chains.size(); ++chain) {
            tasks.push_back(instance->flow
                                .emplace([this, self, planPtr = &plan, scopePtr = &scope, chain]() {
                                    // Nothing may escape into the executor: the first failure is
                                    // kept for the thread that runs this scope, and the chains of
                                    // the scope that have not started are skipped.
                                    if (self->failed.load(std::memory_order_acquire)) [[unlikely]] {
                                        return;
                                    }
                                    try {
                                        TaskflowGcRegion taskRegion;
                                        InheritedDepth inherited(
                                            taskRegion.thread(),
                                            self->parentDepth,
                                            self->parentDomain);
                                        runItems(
                                            *planPtr,
                                            scopePtr->chains[chain],
                                            self->frame,
                                            *self->tailNode,
                                            taskRegion);
                                    } catch (...) {
                                        self->failed.store(true, std::memory_order_release);
                                        std::lock_guard lock(self->errorMutex);
                                        if (!self->error) {
                                            self->error = std::current_exception();
                                        }
                                    }
                                })
                                .name("CHAIN"));
        }
        for (const auto &[from, to] : scope.chainEdges) {
            tasks[from].precede(tasks[to]);
        }
    }

    taskGraphsRun_.fetch_add(1, std::memory_order_relaxed);
    instance->frame        = frame;
    instance->tailNode     = &tailNode;
    instance->parentDepth  = region.thread().callDepth;
    instance->parentDomain = region.thread().faultDomain;
    region.leave(); // the calling thread waits for (and helps with) the tasks from here on
    runOnExecutor(instance->flow);
    std::exception_ptr error = std::exchange(instance->error, nullptr);
    instance->failed.store(false, std::memory_order_relaxed);
    {
        std::lock_guard lock(scope.poolMutex);
        scope.idle.push_back(std::move(instance));
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

void TaskflowExecSchedPass::runItems(
    GraphPlan &plan, std::span<const PlanItem> items, Frame *frame, std::atomic<uint32_t> &tailNode,
    TaskflowGcRegion &region) {
    GCGraph *graph = plan.graph;
    for (const PlanItem &item : items) {
        switch (item.kind) {
        case ItemKind::Leaf: {
            if (item.afterTail && tailNode.load(std::memory_order_acquire) != kInvalidNodeRef) {
                continue;
            }
            region.enter();
            (void)executeLinearNode(graph, item.node, frame, &plan);
            if (item.leaveAfter) {
                // An operator of a module can run for long; do not keep a collector waiting
                // for the next one as well.
                region.leave();
            }
            break;
        }
        case ItemKind::Call:
            if (item.tailCall) {
                tailNode.store(item.node, std::memory_order_release);
                break;
            }
            executeCallNode(graph, item.node, frame, region);
            break;
        case ItemKind::NativeCall:
            if (item.afterTail && tailNode.load(std::memory_order_acquire) != kInvalidNodeRef) {
                continue;
            }
            region.leave();
            (void)executePreparedOperator(graph, item.node, frame, &plan);
            break;
        case ItemKind::Mark:
            region.leave();
            (void)executeLinearMarkedOperator(graph, item.node, frame);
            break;
        case ItemKind::Branch: {
            const PlanBranch &branch = plan.branches[item.branch];
            region.safepoint("taskflow branch boundary");
            region.enter();
            const size_t arm = camel::execute::selectRuntimeBranchArm(graph, branch.brch, frame);
            frame->set(dataIndexOf(graph, branch.brch), static_cast<Int32>(arm));
            if (arm >= branch.armScopes.size()) {
                throw std::runtime_error("Taskflow BRCH arm index is out of range.");
            }
            if (const int32_t armScope = branch.armScopes[arm]; armScope >= 0) {
                runScope(
                    plan,
                    *plan.scopes[static_cast<size_t>(armScope)],
                    frame,
                    tailNode,
                    region);
            }
            if (item.afterTail && tailNode.load(std::memory_order_acquire) != kInvalidNodeRef) {
                break; // the arm ended in a tail call: there is no value to join
            }
            region.enter();
            (void)executeLinearNode(graph, branch.join, frame);
            break;
        }
        }
    }
}

void TaskflowExecSchedPass::runLinearBody(
    GraphPlan &plan, Frame *frame, std::atomic<uint32_t> &tailNode, TaskflowGcRegion &region) {
    GCGraph *graph = plan.graph;
    const std::span<const gc_node_ref_t> nodes(plan.topo);

    gc_node_ref_t tillNode = kInvalidNodeRef;
    gc_node_ref_t skipNode = kInvalidNodeRef;
    gc_node_ref_t joinNode = kInvalidNodeRef;
    // Same as NodeVM: the (tail, join) pairs of the arms that enclose a nested branch.
    std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> enclosingArms;

    const auto &shared = *plan.shared;
    uint32_t resumedLoop = camel::execute::kNoLoop;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const gc_node_ref_t nodeRef = nodes[i];
        const auto *node            = requireNode(graph, nodeRef);

        if (tillNode != kInvalidNodeRef) {
            if (tillNode == nodeRef) {
                tillNode = kInvalidNodeRef;
            } else {
                continue;
            }
        }
        // A loop of the graph starts, tests its round, or ends one here (stepLoops).
        if (!shared.loops.empty()) {
            const size_t resume = camel::execute::stepLoops(
                shared,
                i,
                *frame,
                resumedLoop,
                camel::execute::loopRoundSafepoint([&] {
                    region.leave();
                    region.poll("taskflow loop round");
                    region.enter();
                }));
            if (resume != camel::execute::kNoLoopJump) {
                i = resume - 1;
                continue;
            }
        }
        if (skipNode == kInvalidNodeRef && !enclosingArms.empty() && nodeRef == joinNode) {
            skipNode = enclosingArms.back().first;
            joinNode = enclosingArms.back().second;
            enclosingArms.pop_back();
        }
        if (skipNode != kInvalidNodeRef && skipNode == nodeRef) {
            skipNode = kInvalidNodeRef;
            tillNode = joinNode;
        }

        if (node->kind == GCNodeKind::Brch) {
            region.leave();
            region.poll("taskflow branch boundary");
            region.enter();
            const size_t jumpIdx = camel::execute::selectRuntimeBranchArm(graph, nodeRef, frame);
            frame->set(node->dataIndex, static_cast<Int32>(jumpIdx));

            const auto *body = graph->nodeBodyAs<camel::runtime::GCBrchBody>(nodeRef);
            ASSERT(
                jumpIdx < graph->armCountOf(nodeRef),
                "Taskflow linear BRCH arm index is out of range.");
            // Same arm range as NodeVM: from the arm's first executable node in the linear
            // order (nodes of an inlined arm can precede its recorded head) through its tail.
            if (skipNode != kInvalidNodeRef) {
                enclosingArms.emplace_back(skipNode, joinNode);
            }
            joinNode = body->join;
            tillNode = branchArmEntryForLinear(graph, nodeRef, jumpIdx, nodes);
            skipNode = tillNode == joinNode
                           ? kInvalidNodeRef
                           : graph->execPlan().branchArmTailsOfNode(nodeRef)[jumpIdx];
            continue;
        }

        switch (node->kind) {
        case GCNodeKind::Func:
            if (plan.isTailCall(nodeRef)) {
                tailNode.store(nodeRef, std::memory_order_release);
                return;
            }
            executeCallNode(graph, nodeRef, frame, region);
            break;
        case GCNodeKind::Call:
            executeCallNode(graph, nodeRef, frame, region);
            break;
        case GCNodeKind::Data:
        case GCNodeKind::Port:
        case GCNodeKind::Sync:
            break;
        case GCNodeKind::Loop:
            // The rounds ran at the events of the plan; the state is the value.
            break;
        case GCNodeKind::Oper:
            if (OperatorTable::instance().isMarked(graph->nodeBodyAs<GCOperBody>(nodeRef)->oper)) {
                region.leave();
                (void)executeLinearMarkedOperator(graph, nodeRef, frame);
                break;
            }
            if (OperatorTable::instance().callsGraphs(
                    graph->nodeBodyAs<GCOperBody>(nodeRef)->oper)) {
                region.leave();
                (void)executePreparedOperator(graph, nodeRef, frame, &plan);
                break;
            }
            region.enter();
            (void)executeLinearNode(graph, nodeRef, frame, &plan);
            region.leave();
            break;
        default:
            region.enter();
            (void)executeLinearNode(graph, nodeRef, frame, &plan);
            break;
        }
    }
}

void TaskflowExecSchedPass::raiseFault(
    const RuntimeFault &fault, GCGraph *graph, gc_node_ref_t nodeRef) {
    auto site = makeGraphExecutionSite(context_->sourceContext(), graph, 0, "taskflow");
    if (nodeRef != kInvalidNodeRef) {
        if (const uint64_t origin = graph->nodeOrigin(nodeRef)) {
            site.cachedOrigin = origin;
        }
    }
    // The fault is described and thrown, not recorded: element functions that run
    // concurrently can fault together, and which of their faults is *the* fault is decided
    // where their operator has waited for them (ElementFaults). What arrives at a
    // fault-isolation node becomes its error value; what arrives at the entry of the run is
    // recorded there (apply) and ends the program.
    if (auto reporter = context_->runtimeErrorReporter()) {
        throw reporter->describe(fault, site);
    }
    throw DiagnosticBuilder::of(fault.diag()).commit(fault.what());
}

// Out of line: the fault is a large temporary, and the frame of the caller is what every level
// of a recursion pays for.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void TaskflowExecSchedPass::requireCallRoom(
    TaskflowGcGate::ThreadState &thread, GCGraph *callerGraph, gc_node_ref_t callNode) {
    // callDepth includes the entry graph's call of main, which is not counted (kEntryCalls).
    const bool overLimit = maxRecursionDepth_ != 0 &&
                           thread.callDepth >= maxRecursionDepth_ + camel::execute::kEntryCalls;
    // Under the limit the native stack of the thread can still run out: what a thread can
    // nest depends on its stack and on what else the worker picked up while waiting.
    if (!overLimit && !nativeStackIsExhausted(thread)) [[likely]] {
        return;
    }
    try {
        if (overLimit) {
            camel::execute::throwRecursionLimitReached(callerGraph->name(), maxRecursionDepth_);
        }
        camel::execute::throwNativeStackExhausted(callerGraph->name(), thread.callDepth);
    } catch (const RuntimeFault &fault) {
        raiseFault(fault, callerGraph, callNode);
    }
}

void TaskflowExecSchedPass::executeCallNode(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, TaskflowGcRegion &region) {
    const auto *node = requireNode(graph, nodeRef);
    // The arguments move from the caller frame into the callee frame, and the result from the
    // callee frame into the caller frame, each inside one region.
    region.enter();
    GCGraph *targetGraph = nullptr;
    if (node->kind == GCNodeKind::Func) {
        targetGraph = programs_.of(graph).calleeOf(*graph, nodeRef);
    } else {
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!withInputs.empty(), "Taskflow CALL node must have function input.");
        Function *func = frame->get<Function *>(dataIndexOf(graph, withInputs.front()));
        targetGraph    = func ? programs_.graphOf(func) : nullptr;
    }
    ASSERT(targetGraph != nullptr, "Taskflow call target graph is null.");
    auto &thread = region.thread();
    requireCallRoom(thread, graph, nodeRef);
    OpenCall openCall(thread.callDepth);
    const bool indirect = node->kind == GCNodeKind::Call;
    const slot_t result = callGraph(targetGraph, nullptr, region, [&](Frame *dest) {
        if (indirect) {
            camel::execute::fillFrameForIndirectCall(frame, dest, graph, nodeRef);
        } else {
            camel::execute::fillFrameForDirectInvoke(frame, dest, graph, nodeRef);
        }
    });
    frame->set(node->dataIndex, result);
}

template <typename Fill>
slot_t TaskflowExecSchedPass::callGraph(
    GCGraph *target, GraphPlan *plan, TaskflowGcRegion &region, Fill &&fill) {
    if (plan == nullptr) {
        plan = planFor(target);
    }
    target = plan->graph;
    // The arguments move into the callee frame, and the value comes back to the caller, each
    // inside the region.
    region.enter();
    if (plan->bytecodeEntry != kNoBytecode) {
        return handoff_->call(plan->bytecodeEntry, target, region, fill);
    }
    Frame *callee = framePool_.acquire(target);
    fill(callee);
    slot_t result = NullSlot;
    try {
        result = evalGraph(target, callee, region, plan);
    } catch (...) {
        framePool_.release(callee);
        throw;
    }
    // evalGraph returns inside the region and giving the frame back polls nothing: the value
    // is still safe when the caller stores it.
    framePool_.release(callee);
    return result;
}

template <typename FillArgs, typename Sink>
void TaskflowExecSchedPass::callClosure(
    TaskflowGcRegion &region, const ElementCallSite &site, GCGraph *targetGraph,
    GraphPlan *targetPlan, Frame *funcFrame, camel::runtime::gc_data_idx_t funcIdx, size_t argCount,
    FillArgs &&fillArgs, Sink &&sink) {
    std::array<slot_t, 2> args{NullSlot, NullSlot};
    ASSERT(argCount <= args.size(), "Taskflow closure call takes at most two arguments.");
    requireCallRoom(region.thread(), site.graph, site.node);
    OpenCall openCall(region.thread().callDepth);
    region.enter();
    fillArgs(std::span<slot_t>(args.data(), argCount));
    const slot_t result = callGraph(targetGraph, targetPlan, region, [&](Frame *dest) {
        fillClosureCallFrame(
            dest,
            targetGraph,
            closureAt(funcFrame, funcIdx),
            std::span<const slot_t>(args.data(), argCount));
    });
    sink(result);
}

void TaskflowExecSchedPass::runOnExecutor(tf::Taskflow &flow) {
    // One executor for the whole run. A flow started from inside one of its tasks (a map in a
    // map element) is run cooperatively by the calling worker. A fresh executor per nested flow
    // meant a full set of worker threads for every inner map (thousands of threads) and
    // ignored the thread limit.
    struct RunningFlow {
        RunningFlow() { TaskflowGcGate::instance().flowStarted(); }
        ~RunningFlow() { TaskflowGcGate::instance().flowEnded(); }
    } runningFlow;
    if (executor_.this_worker_id() >= 0) {
        executor_.corun(flow);
    } else {
        // get(), not wait(): a failure inside a task (a runtime fault in a `map` element) is
        // rethrown here instead of being dropped.
        executor_.run(flow).get();
    }
}

slot_t TaskflowExecSchedPass::runPreparedSubgraph(tf::Subflow &sf, GCGraph *graph, Frame *frame) {
    graph = frame->runtimeGraph();
    ASSERT(graph != nullptr, "Taskflow subgraph execution requires a runtime graph.");
    (void)sf;
    try {
        slot_t result = evalGraphTF(graph, frame);
        framePool_.release(frame);
        return result;
    } catch (...) {
        framePool_.release(frame);
        throw;
    }
}

std::optional<slot_t>
TaskflowExecSchedPass::runMarkedOperator(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    // How the operator calls its function is its declaration's (OperatorSemantics::
    // elementCalls); the property that lets the calls run at once is `independent`, the
    // operator's name does not matter.
    using camel::core::sem::ElementShape;
    const auto *calls =
        OperatorTable::instance().elementCalls(graph->nodeBodyAs<GCOperBody>(nodeRef)->oper);
    ASSERT(calls != nullptr, "A marked operator declares how it calls its function.");
    switch (calls->shape) {
    case ElementShape::Isolate:
        return executeIsolateNode(graph, nodeRef, frame);
    case ElementShape::Map: // `apply` returns a new array like `map` (design principles 11.3)
        mark_map_arr(graph, nodeRef, frame);
        break;
    case ElementShape::Filter:
        mark_filter_arr(graph, nodeRef, frame);
        break;
    case ElementShape::Fold:
        calls->independent ? mark_unordered_reduce_arr(graph, nodeRef, frame)
                           : mark_reduce_arr(graph, nodeRef, frame);
        break;
    case ElementShape::Foreach:
        calls->independent ? mark_unordered_foreach_arr(graph, nodeRef, frame)
                           : mark_foreach_arr(graph, nodeRef, frame);
        break;
    }
    return std::nullopt;
}

slot_t TaskflowExecSchedPass::executeLinearMarkedOperator(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    // On the calling thread: an operator decides itself whether its elements are worth
    // tasks (runElements). A task graph per operator, as before, cost more than a `reduce`
    // over a handful of elements.
    if (const auto value = runMarkedOperator(graph, nodeRef, frame)) {
        return *value;
    }
    const auto *node = requireNode(graph, nodeRef);
    return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);
}

slot_t TaskflowExecSchedPass::executeLinearNode(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, const GraphPlan *plan) {
    const auto *node = requireNode(graph, nodeRef);

    switch (node->kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Sync:
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);

    case GCNodeKind::Join: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear JOIN must have branch-index input.");
        const auto brIndex = frame->get<Int32>(dataIndexOf(graph, normInputs.front()));
        ASSERT(
            brIndex >= 0 && static_cast<size_t>(brIndex) < withInputs.size(),
            "Taskflow linear JOIN branch index is out of range.");
        if (node->dataIndex == 0) {
            return NullSlot;
        }
        if (node->dataType() == Type::Void()) {
            frame->set(node->dataIndex, NullSlot);
            return NullSlot;
        }
        const slot_t result =
            frame->get<slot_t>(dataIndexOf(graph, withInputs[static_cast<size_t>(brIndex)]));
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Gate: {
        const slot_t value = readForwardedRuntimeValue(graph, nodeRef, frame);
        if (node->dataIndex != 0) {
            frame->set(node->dataIndex, value);
        }
        return value;
    }

    case GCNodeKind::Copy: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear COPY node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const TypeCode srcCode          = frame->codeAt(srcIdx);
        if (camel::core::type::isGCTraced(srcCode)) {
            Object *srcData  = frame->get<Object *>(srcIdx);
            Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
            auto *copy       = srcData->clone(camel::core::mm::autoSpace(), srcTypePtr, false);
            frame->set(node->dataIndex, copy);
            return toSlot(copy);
        }
        const slot_t value = frame->get<slot_t>(srcIdx);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Cast: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear CAST node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        Type *srcType                   = frame->typeAt<Type>(srcIdx);
        Type *tgtType                   = node->dataType();
        slot_t value                    = frame->get<slot_t>(srcIdx);
        slot_t result                   = NullSlot;
        try {
            result = tgtType->castSlotFrom(value, srcType);
        } catch (const RuntimeFault &fault) {
            // A checked conversion (`x as T` into a module type) that fails is an error of the
            // program, reported at the conversion like the fault of an operator.
            raiseFault(fault, graph, nodeRef);
        }
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Fill: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear FILL node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        Type *srcType                   = node->dataType();
        ASSERT(
            camel::core::type::isGCTraced(srcType->code()),
            "Taskflow linear FILL target must be GC-traced.");
        Object *sourceObj = frame->get<Object *>(srcIdx);
        ASSERT(sourceObj != nullptr, "Taskflow linear FILL source object is null.");
        Object *srcObj = sourceObj->clone(camel::core::mm::autoSpace(), srcType, false);
        ASSERT(srcObj != nullptr, "Taskflow linear FILL target clone is null.");
        std::vector<slot_t> fillValues;
        fillValues.reserve(withInputs.size());
        for (auto input : withInputs) {
            fillValues.push_back(frame->get<slot_t>(dataIndexOf(graph, input)));
        }
        camel::execute::writeRuntimeFillSlots(
            srcObj,
            srcType,
            graph->nodeBodyAs<camel::runtime::GCFillBody>(nodeRef),
            fillValues);

        frame->set(node->dataIndex, srcObj);
        return toSlot(srcObj);
    }

    case GCNodeKind::Accs: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow linear ACCS node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const auto *body                = graph->nodeBodyAs<GCAccsBody>(nodeRef);
        if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
            Tuple *tuple = frame->get<Tuple *>(srcIdx);
            ASSERT(body->value < tuple->size(), "Taskflow linear ACCS tuple index out of bounds.");
            const slot_t value = tuple->get<slot_t>(body->value);
            frame->set(node->dataIndex, value);
            return value;
        }
        Struct *st                 = frame->get<Struct *>(srcIdx);
        Type *stType               = frame->typeAt<Type>(srcIdx);
        const std::string_view key = body->key();
        slot_t value               = st->get<slot_t>(std::string(key.data(), key.size()), stType);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Func:
    case GCNodeKind::Call: {
        // The schedulers below run calls themselves (executeCallNode); this path serves the
        // task-graph builder kept for the dump pass.
        TaskflowGcRegion region;
        executeCallNode(graph, nodeRef, frame, region);
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);
    }

    case GCNodeKind::Oper: {
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        if (OperatorTable::instance().isMarked(body->oper)) {
            return executeLinearMarkedOperator(graph, nodeRef, frame);
        }
        return executePreparedOperator(graph, nodeRef, frame, plan);
    }

    case GCNodeKind::Brch:
        return NullSlot;

    default: {
        tf::Taskflow flow;
        slot_t result = NullSlot;
        flow.emplace([this, graph, nodeRef, frame, &result](tf::Subflow &sf) {
            result = executePreparedNode(graph, nodeRef, frame, sf, nullptr);
        });
        runOnExecutor(flow);
        return result;
    }
    }
}

Frame *TaskflowExecSchedPass::acquirePreparedRuntimeCallFrame(
    GCGraph *targetGraph, gc_node_ref_t callNodeRuntimeIndex, Frame *sourceFrame) {
    ASSERT(targetGraph != nullptr, "Taskflow runtime call target graph is null.");
    ASSERT(sourceFrame != nullptr, "Taskflow runtime source frame is null.");
    ASSERT(sourceFrame->runtimeGraph() != nullptr, "Taskflow runtime source graph is null.");

    targetGraph            = planFor(targetGraph)->graph;
    Frame *dest            = framePool_.acquire(targetGraph);
    const auto *callRecord = requireNode(sourceFrame->runtimeGraph(), callNodeRuntimeIndex);
    if (callRecord->kind == GCNodeKind::Call) {
        camel::execute::fillFrameForIndirectCall(
            sourceFrame,
            dest,
            sourceFrame->runtimeGraph(),
            callNodeRuntimeIndex);
    } else {
        camel::execute::fillFrameForDirectInvoke(
            sourceFrame,
            dest,
            sourceFrame->runtimeGraph(),
            callNodeRuntimeIndex);
    }
    return dest;
}

Frame *TaskflowExecSchedPass::acquirePreparedClosureCallFrame(
    GCGraph *targetGraph, Tuple *closure, std::span<const slot_t> args) {
    ASSERT(targetGraph != nullptr, "Taskflow closure call requires a runtime graph target.");
    targetGraph = planFor(targetGraph)->graph;
    Frame *dest = framePool_.acquire(targetGraph);
    fillClosureCallFrame(dest, targetGraph, closure, args);
    return dest;
}

void TaskflowExecSchedPass::fillClosureCallFrame(
    Frame *dest, GCGraph *targetGraph, Tuple *closure, std::span<const slot_t> args) {
    // A dynamic source graph may have been privately lowered before allocating
    // this frame. Bind through the actual destination graph, not its source IDs.
    targetGraph          = dest->graph();
    const auto normPorts = targetGraph->normPorts();
    ASSERT(args.size() == normPorts.size(), "Taskflow closure call norm-arg count mismatch.");
    for (size_t i = 0; i < args.size(); ++i) {
        dest->set(dataIndexOf(targetGraph, normPorts[i]), args[i]);
    }

    const auto closureNodes = targetGraph->closureNodes();
    if (closureNodes.empty()) {
        return;
    }

    ASSERT(closure != nullptr, "Taskflow closure tuple is null.");
    ASSERT(closure->size() == closureNodes.size(), "Taskflow closure size mismatch.");
    for (size_t i = 0; i < closureNodes.size(); ++i) {
        dest->set(dataIndexOf(targetGraph, closureNodes[i]), closure->get<slot_t>(i));
    }
}

slot_t TaskflowExecSchedPass::executePreparedOperator(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, const GraphPlan *plan) {
    ASSERT(graph != nullptr, "Taskflow operator execution requires a graph.");
    ASSERT(frame != nullptr, "Taskflow operator execution requires a frame.");
    // Graph-calling operators own the region so the callee can reach safepoints.
    TaskflowGcRegion region;
    region.enter();
    camel::execute::FunctionInvoker invoker;
    const auto *operatorBody = graph->nodeBodyAs<GCOperBody>(nodeRef);
    if (OperatorTable::instance().callsGraphs(operatorBody->oper)) {
        invoker =
            [&](Function *function, std::span<const slot_t> with, std::span<const slot_t> normal) {
                requireCallRoom(region.thread(), graph, nodeRef);
                OpenCall openCall(region.thread().callDepth);
                auto *target = programs_.graphOf(function);
                try {
                    return callGraph(target, nullptr, region, [&](Frame *dest) {
                        fillClosureCallFrame(dest, target, function->tuple(), normal);
                        auto *executionTarget = dest->graph();
                        const auto ports      = executionTarget->withPorts();
                        for (size_t i = 0; i < ports.size(); ++i) {
                            dest->set(dataIndexOf(executionTarget, ports[i]), with[i]);
                        }
                    });
                } catch (...) {
                    // Native code may catch and inspect its rooted arguments before invoking again.
                    region.enter();
                    throw;
                }
            };
    }
    camel::execute::FunctionCallScope invocation(*context_, std::move(invoker));
    auto invoke = [&](operator_t function, ArgsView &with, ArgsView &normal) {
        with.setResultType(graph->node(nodeRef)->dataType());
        normal.setResultType(with.resultType());
        const slot_t result = function(with, normal, *context_);
        // Publish before this region is released, including on the task-graph path.
        frame->set(dataIndexOf(graph, nodeRef), result);
        return result;
    };

    // An operator that ran before on this plan: its function and the places of its arguments
    // are known. Looking the operator up by URI on every execution (two string copies, two
    // hash lookups and the read lock of the executor table, which all workers share) cost more
    // than a built-in operator itself.
    const bool prepared =
        plan != nullptr && nodeRef < plan->opers.size() && plan->opers[nodeRef].prepared;
    if (prepared) {
        if (const operator_t resolved =
                plan->shared->resolvedOpers[nodeRef].load(std::memory_order_acquire)) {
            const PreparedOper &oper = plan->opers[nodeRef];
            const data_arr_t wargs{
                plan->operArgs.data() + oper.withBegin,
                oper.normBegin - oper.withBegin};
            const data_arr_t nargs{
                plan->operArgs.data() + oper.normBegin,
                oper.end - oper.normBegin};
            FrameArgsView withView(*frame, wargs);
            FrameArgsView normView(*frame, nargs);
            // What a pass recorded about this node (design principles 9.6).
            const auto attrs = graph->nodeAttrs(nodeRef);
            withView.setAttrs(attrs);
            normView.setAttrs(attrs);
            if (camel::core::effect_trace::enabled()) [[unlikely]] {
                camel::core::effect_trace::setSite(graph->nodeOrigin(nodeRef));
            }
            try {
                return invoke(resolved, withView, normView);
            } catch (const RuntimeFault &fault) {
                raiseFault(fault, graph, nodeRef);
            }
        }
    }

    const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
    const std::string uri(body->uri());
    auto opFunc = context_->execMgr().find(uri);
    if (!opFunc) {
        // The graph names an operator no loaded module implements: a compiler defect.
        raiseFault(
            RuntimeFault::make(
                RuntimeDiag::PassInternalError,
                "std::prl",
                "the operator '" + uri + "' has no implementation"),
            graph,
            nodeRef);
    }
    if (prepared) {
        plan->shared->resolvedOpers[nodeRef].store(
            camel::profile::profiledOperator(uri, *opFunc), std::memory_order_release);
    }

    std::vector<runtime_data_idx_t> withIndices;
    std::vector<runtime_data_idx_t> normIndices;
    withIndices.reserve(graph->withInputsOf(nodeRef).size());
    normIndices.reserve(graph->normInputsOf(nodeRef).size());
    for (gc_node_ref_t inputRef : graph->withInputsOf(nodeRef)) {
        withIndices.push_back(dataIndexOf(graph, inputRef));
    }
    for (gc_node_ref_t inputRef : graph->normInputsOf(nodeRef)) {
        normIndices.push_back(dataIndexOf(graph, inputRef));
    }

    data_arr_t wargs{withIndices.data(), withIndices.size()};
    data_arr_t nargs{normIndices.data(), normIndices.size()};
    FrameArgsView withView(*frame, wargs);
    FrameArgsView normView(*frame, nargs);
    const auto attrs = graph->nodeAttrs(nodeRef);
    withView.setAttrs(attrs);
    normView.setAttrs(attrs);

    if (camel::core::effect_trace::enabled()) [[unlikely]] {
        camel::core::effect_trace::setSite(graph->nodeOrigin(nodeRef));
    }
    try {
        return invoke(camel::profile::profiledOperator(uri, *opFunc), withView, normView);
    } catch (const RuntimeFault &fault) {
        // Reported at the operator, as on the other back ends (it used to be the start of
        // the enclosing graph).
        raiseFault(fault, graph, nodeRef);
    }
}

slot_t TaskflowExecSchedPass::executePreparedBranchArm(
    GCGraph *graph, gc_node_ref_t brchRef, size_t armIndex, Frame *frame, tf::Subflow &sf,
    const RuntimeBuildInfo *buildInfo) {
    ASSERT(graph != nullptr, "Taskflow branch execution requires a graph.");
    RuntimeBranchArmRegion ownedArm;
    const RuntimeBranchArmRegion *arm = nullptr;
    if (buildInfo != nullptr && brchRef < buildInfo->branchArms.size()) {
        const auto &arms = buildInfo->branchArms[brchRef];
        ASSERT(armIndex < arms.size(), "Taskflow branch arm index is out of range.");
        arm = &arms[armIndex];
    } else {
        ownedArm = camel::execute::collectRuntimeBranchArmRegion(graph, brchRef, armIndex);
        arm      = &ownedArm;
    }
    ASSERT(arm != nullptr, "Taskflow branch arm resolution failed.");
    for (gc_node_ref_t nodeRef : arm->topoIndices) {
        (void)executePreparedNode(graph, nodeRef, frame, sf, buildInfo);
    }

    ASSERT(arm->tailIndex != kInvalidNodeRef, "Taskflow branch arm has no tail.");
    return frame->get<slot_t>(dataIndexOf(graph, arm->tailIndex));
}

slot_t TaskflowExecSchedPass::executePreparedNode(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf,
    const RuntimeBuildInfo *buildInfo) {
    const auto *node = requireNode(graph, nodeRef);

    switch (node->kind) {
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Sync:
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);

    case GCNodeKind::Join: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow JOIN must have branch-index input.");
        const auto brIndex = frame->get<Int32>(dataIndexOf(graph, normInputs.front()));
        ASSERT(
            brIndex >= 0 && static_cast<size_t>(brIndex) < withInputs.size(),
            "Taskflow JOIN branch index is out of range.");
        if (node->dataIndex == 0) {
            return NullSlot;
        }
        if (node->dataType() == Type::Void()) {
            frame->set(node->dataIndex, NullSlot);
            return NullSlot;
        }
        const slot_t result =
            frame->get<slot_t>(dataIndexOf(graph, withInputs[static_cast<size_t>(brIndex)]));
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Gate: {
        const slot_t value = readForwardedRuntimeValue(graph, nodeRef, frame);
        if (node->dataIndex != 0) {
            frame->set(node->dataIndex, value);
        }
        return value;
    }

    case GCNodeKind::Copy: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow COPY node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const TypeCode srcCode          = frame->codeAt(srcIdx);
        if (camel::core::type::isGCTraced(srcCode)) {
            Object *srcData  = frame->get<Object *>(srcIdx);
            Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
            auto *copy       = srcData->clone(camel::core::mm::autoSpace(), srcTypePtr, false);
            frame->set(node->dataIndex, copy);
            return toSlot(copy);
        }
        const slot_t value = frame->get<slot_t>(srcIdx);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Cast: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow CAST node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        Type *srcType                   = frame->typeAt<Type>(srcIdx);
        Type *tgtType                   = node->dataType();
        slot_t value                    = frame->get<slot_t>(srcIdx);
        slot_t result                   = NullSlot;
        try {
            result = tgtType->castSlotFrom(value, srcType);
        } catch (const RuntimeFault &fault) {
            // A checked conversion (`x as T` into a module type) that fails is an error of the
            // program, reported at the conversion like the fault of an operator.
            raiseFault(fault, graph, nodeRef);
        }
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Fill: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow FILL node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        Type *srcType                   = node->dataType();
        ASSERT(
            camel::core::type::isGCTraced(srcType->code()),
            "Taskflow FILL target must be GC-traced.");
        Object *sourceObj = frame->get<Object *>(srcIdx);
        ASSERT(sourceObj != nullptr, "Taskflow FILL source object is null.");
        Object *srcObj = sourceObj->clone(camel::core::mm::autoSpace(), srcType, false);
        ASSERT(srcObj != nullptr, "Taskflow FILL target clone is null.");
        std::vector<slot_t> fillValues;
        fillValues.reserve(withInputs.size());
        for (auto input : withInputs) {
            fillValues.push_back(frame->get<slot_t>(dataIndexOf(graph, input)));
        }
        camel::execute::writeRuntimeFillSlots(
            srcObj,
            srcType,
            graph->nodeBodyAs<camel::runtime::GCFillBody>(nodeRef),
            fillValues);
        frame->set(node->dataIndex, srcObj);
        return toSlot(srcObj);
    }

    case GCNodeKind::Accs: {
        const auto normInputs = graph->normInputsOf(nodeRef);
        ASSERT(!normInputs.empty(), "Taskflow ACCS node must have one norm input.");
        const runtime_data_idx_t srcIdx = dataIndexOf(graph, normInputs.front());
        const auto *body                = graph->nodeBodyAs<GCAccsBody>(nodeRef);
        if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
            Tuple *tuple = frame->get<Tuple *>(srcIdx);
            ASSERT(body->value < tuple->size(), "Taskflow ACCS tuple index out of bounds.");
            const slot_t value = tuple->get<slot_t>(body->value);
            frame->set(node->dataIndex, value);
            return value;
        }
        Struct *st                 = frame->get<Struct *>(srcIdx);
        Type *stType               = frame->typeAt<Type>(srcIdx);
        const std::string_view key = body->key();
        slot_t value               = st->get<slot_t>(std::string(key.data(), key.size()), stType);
        frame->set(node->dataIndex, value);
        return value;
    }

    case GCNodeKind::Func: {
        auto *targetGraph = programs_.of(graph).calleeOf(*graph, nodeRef);
        ASSERT(targetGraph != nullptr, "Taskflow FUNC target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = runPreparedSubgraph(sf, targetGraph, calleeFrame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Call: {
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!withInputs.empty(), "Taskflow CALL node must have function input.");
        Function *func    = frame->get<Function *>(dataIndexOf(graph, withInputs.front()));
        auto *targetGraph = func ? programs_.graphOf(func) : nullptr;
        ASSERT(targetGraph != nullptr, "Taskflow CALL target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = runPreparedSubgraph(sf, targetGraph, calleeFrame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Oper: {
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        if (!OperatorTable::instance().isMarked(body->oper)) {
            return executePreparedOperator(graph, nodeRef, frame);
        }
        if (const auto value = runMarkedOperator(graph, nodeRef, frame)) {
            return *value;
        }
        return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);
    }

    case GCNodeKind::Brch: {
        const size_t jumpIdx = camel::execute::selectRuntimeBranchArm(graph, nodeRef, frame);
        frame->set(node->dataIndex, static_cast<Int32>(jumpIdx));
        return executePreparedBranchArm(graph, nodeRef, jumpIdx, frame, sf, buildInfo);
    }

    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
    case GCNodeKind::Dead:
        // Reserved kinds: never part of a frozen graph (see GCNodeKind). Listed only to keep
        // this switch exhaustive.
        return NullSlot;
    }

    return NullSlot;
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, GCGraph *runtimeGraph, Frame *frame) {
    ASSERT(runtimeGraph != nullptr, "Taskflow graph instantiation requires a runtime graph.");
    auto buildInfo = std::make_shared<RuntimeBuildInfo>(buildRuntimeBuildInfo(runtimeGraph));
    std::vector<tf::Task> taskMap(runtimeGraph->nodeCount());
    std::vector<uint8_t> taskBuilt(runtimeGraph->nodeCount(), 0);
    buildRuntimeNodeTasks(flowLike, runtimeGraph, frame, buildInfo, taskMap, taskBuilt);
    connectRuntimeDependencies(flowLike, runtimeGraph, frame, buildInfo, taskMap, taskBuilt);
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeNodeTasks(
    FlowT &flowLike, GCGraph *graph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt) {
    for (gc_node_ref_t nodeRef : graph->execPlan().topo) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind == GCNodeKind::Brch) {
            buildRuntimeBranchJoinRegion(
                flowLike,
                graph,
                frame,
                buildInfo,
                taskMap,
                taskBuilt,
                nodeRef);
            continue;
        }
        if (nodeRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[nodeRef] != 0) {
            continue;
        }

        tf::Task task;
        switch (node->kind) {
        case GCNodeKind::Data:
            task = buildDataTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Port:
            task = buildPortTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Copy:
            task = buildCopyTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Cast:
            task = buildCastTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Fill:
            task = buildFillTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Accs:
            task = buildAccsTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Func:
            task = buildFuncTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Call:
            task = buildCallTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Oper:
            task = buildOperTask(flowLike, graph, nodeRef, frame);
            break;
        case GCNodeKind::Sync:
            task = flowLike.emplace([]() {}).name("SYNC");
            break;
        case GCNodeKind::Gate:
            task = flowLike
                       .emplace([graph, nodeRef, frame]() {
                           const auto *node   = requireNode(graph, nodeRef);
                           const slot_t value = readForwardedRuntimeValue(graph, nodeRef, frame);
                           if (node->dataIndex != 0) {
                               frame->set(node->dataIndex, value);
                           }
                       })
                       .name("GATE");
            break;
        case GCNodeKind::Join:
            task = flowLike.emplace([]() {}).name("JOIN");
            break;
        default:
            continue;
        }
        taskMap[nodeRef]   = task;
        taskBuilt[nodeRef] = 1;
    }
}

template <typename FlowT>
void TaskflowExecSchedPass::connectRuntimeDependencies(
    FlowT &flowLike, GCGraph *graph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt) {
    (void)flowLike;
    (void)frame;
    auto addEdges = [&](std::span<const gc_node_ref_t> inputs, tf::Task task) {
        for (gc_node_ref_t inputRef : inputs) {
            if (inputRef >= taskBuilt.size() || taskBuilt[inputRef] == 0) {
                continue;
            }
            if (inputRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[inputRef] != 0 &&
                requireNode(graph, inputRef)->kind != GCNodeKind::Join) {
                continue;
            }
            taskMap[inputRef].precede(task);
        }
    };

    for (gc_node_ref_t nodeRef : graph->execPlan().topo) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind == GCNodeKind::Brch) {
            continue;
        }
        if (nodeRef < taskBuilt.size() && taskBuilt[nodeRef] == 0) {
            continue;
        }
        if (nodeRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[nodeRef] != 0) {
            continue;
        }
        addEdges(graph->normInputsOf(nodeRef), taskMap[nodeRef]);
        addEdges(graph->withInputsOf(nodeRef), taskMap[nodeRef]);
        addEdges(graph->ctrlInputsOf(nodeRef), taskMap[nodeRef]);
    }
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildDataTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    (void)graph;
    (void)nodeRef;
    (void)frame;
    return flowLike.emplace([]() {}).name("DATA");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildPortTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    (void)graph;
    (void)nodeRef;
    (void)frame;
    return flowLike.emplace([]() {}).name("PORT");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCopyTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("COPY");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCastTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("CAST");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFillTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("FILL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildAccsTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("ACCS");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildFuncTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("FUNC");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildCallTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("CALL");
}

template <typename FlowT>
tf::Task TaskflowExecSchedPass::buildOperTask(
    FlowT &flowLike, GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    return flowLike
        .emplace(
            [this, graph, nodeRef, frame]() { (void)executeLinearNode(graph, nodeRef, frame); })
        .name("OPER");
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeBranchJoinRegion(
    FlowT &flowLike, GCGraph *runtimeGraph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt, gc_node_ref_t brchRuntimeIndex) {
    ASSERT(runtimeGraph != nullptr, "Taskflow BRCH region requires a runtime graph.");
    ASSERT(brchRuntimeIndex < buildInfo->branchArms.size(), "Taskflow BRCH ref is out of range.");
    const auto &arms = buildInfo->branchArms[brchRuntimeIndex];
    ASSERT(!arms.empty(), "Taskflow BRCH region has no branch arms.");
    const gc_node_ref_t joinRef = arms.front().joinIndex;
    ASSERT(joinRef != kInvalidNodeRef, "Taskflow BRCH region has no JOIN.");

    auto selector =
        flowLike
            .emplace([this, runtimeGraph, brchRuntimeIndex, frame](tf::Subflow &sf) {
                const auto brchSlot = dataIndexOf(runtimeGraph, brchRuntimeIndex);
                const size_t jumpIdx =
                    camel::execute::selectRuntimeBranchArm(runtimeGraph, brchRuntimeIndex, frame);
                frame->set(brchSlot, static_cast<Int32>(jumpIdx));
                (void)executePreparedBranchArm(
                    runtimeGraph,
                    brchRuntimeIndex,
                    jumpIdx,
                    frame,
                    sf,
                    nullptr);
            })
            .name("BRCH");

    auto joiner = flowLike
                      .emplace([this, runtimeGraph, joinRef, frame](tf::Subflow &sf) {
                          (void)executePreparedNode(runtimeGraph, joinRef, frame, sf, nullptr);
                      })
                      .name("JOIN");

    auto precedeFromInputs = [&](std::span<const gc_node_ref_t> inputs, tf::Task task) {
        for (gc_node_ref_t inputRef : inputs) {
            if (inputRef < buildInfo->skipNodes.size() && buildInfo->skipNodes[inputRef] != 0 &&
                requireNode(runtimeGraph, inputRef)->kind != GCNodeKind::Join) {
                continue;
            }
            if (inputRef < taskBuilt.size() && taskBuilt[inputRef] != 0) {
                taskMap[inputRef].precede(task);
            }
        }
    };

    precedeFromInputs(runtimeGraph->normInputsOf(brchRuntimeIndex), selector);
    precedeFromInputs(runtimeGraph->ctrlInputsOf(brchRuntimeIndex), selector);

    selector.precede(joiner);

    taskMap[brchRuntimeIndex]   = selector;
    taskBuilt[brchRuntimeIndex] = 1;
    taskMap[joinRef]            = joiner;
    taskBuilt[joinRef]          = 1;
}

gc_node_ref_t TaskflowExecSchedPass::branchArmEntryForLinear(
    GCGraph *graph, gc_node_ref_t brchRef, size_t armIndex,
    std::span<const gc_node_ref_t> topoNodes) {
    // The entry is built under the lock: a worker that found it just inserted would otherwise
    // read an empty list. Entries are never removed or changed while a pass runs.
    std::lock_guard lock(linearCacheMutex_);
    auto [it, inserted] = linearArmEntryCache_[graph].try_emplace(brchRef);
    if (inserted) {
        const size_t armCount = graph->armCountOf(brchRef);
        it->second.reserve(armCount);
        for (size_t arm = 0; arm < armCount; ++arm) {
            it->second.push_back(graph->execPlan().branchArmEntriesOfNode(brchRef)[arm]);
        }
    }
    ASSERT(armIndex < it->second.size(), "Taskflow linear BRCH arm index is out of range.");
    return it->second[armIndex];
}

// ---------------------------------------------------------------------------------------------
// Higher-order operators
//
// An operator runs on the thread that reaches it. What an element produces is stored where
// the collector finds it before the element's region is left: in the result array, in the
// node's own slot, or in a rooted scratch array.
//
// The ordered operators (`reduce`, `foreach`) call their function element after element on
// that thread: a chain of tasks, one per element, was the same order at the price of a task
// each.
//
// The unordered ones (`map`, `apply`, `filter`, `unordered_foreach`) go through runElements.
// A task per element is right when an element is a module operator of milliseconds and wrong
// when it is three built-in operators; which it is cannot be read off the graph (a light graph
// can recurse for seconds, a built-in operator can build a large array), so it is measured:
//
//   - The elements are claimed in chunks from a shared counter by whoever works on the
//     operator. A chunk starts at one element; every finished chunk sets the size of the next
//     ones to what fits kChunkNs at the pace just measured (at most four times the last size,
//     at most an eighth of an even share, so that the last chunks cannot leave one worker
//     with much more than the others).
//   - When the element function is bounded and light (boundedLight: built-in operators and
//     calls without recursion), the calling thread starts alone and asks for help only when
//     what it has measured says the rest is worth it (kSpawnNs). A `map` over a few small
//     elements then never leaves its thread.
//   - Otherwise the helpers start at once, so that elements of seconds each run side by side
//     from the first one on.
//   - With one worker, or one element, everything runs on the calling thread.
//
// Helpers are tasks of the shared executor; the thread that started the operator waits for
// them (running one itself when it is a worker).
// ---------------------------------------------------------------------------------------------

namespace {

// Whether the operator's declaration lets the calls on different elements run at once
// (OperatorSemantics::elementCalls).
bool elementsIndependent(GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *calls =
        OperatorTable::instance().elementCalls(graph->nodeBodyAs<GCOperBody>(nodeRef)->oper);
    return calls != nullptr && calls->independent;
}

// The faults of the element calls of one unordered operator (map, apply, filter,
// unordered_foreach; the chunks of unordered_reduce), which run concurrently and can fault
// together. Design principles 3.6: elements that have started run to their end, elements
// that have not started do not start, and of several faults the one of the smallest index is
// reported. So that the report does not depend on which element a worker happened to pick
// first, "do not start" is applied to the elements *after* the smallest faulting index known:
// an element before it still runs, as it would have run first on a back end that takes the
// elements in order. The fault reported is therefore the one the linear back ends report.
// DESIGN-QUESTION(parallel-fault-lower-elements): the alternative is to start nothing at all
// after the first fault met, and then which fault is reported differs from run to run.
//
// Nothing escapes into the executor (it would cancel the whole flow, lower elements included):
// the element's task keeps its failure here, and the thread that started the operator
// rethrows the chosen one after it has waited for all that started. A nested operator has
// chosen its own smallest index before its fault arrives here as the fault of the outer
// element, so the order is by the outer index first, then by the inner one.
struct ElementFaults {
    std::atomic<size_t> lowest{std::numeric_limits<size_t>::max()};
    std::mutex mutex;
    std::exception_ptr error;

    template <typename Body> void run(size_t index, Body &&body) {
        if (lowest.load(std::memory_order_acquire) < index) [[unlikely]] {
            return;
        }
        try {
            body();
        } catch (...) {
            std::lock_guard lock(mutex);
            if (index < lowest.load(std::memory_order_relaxed)) {
                error = std::current_exception();
                lowest.store(index, std::memory_order_release);
            }
        }
    }
    void rethrow() {
        if (error) {
            std::rethrow_exception(error);
        }
    }
};

// Aim for chunks of this long: long against the cost of claiming one (a shared counter and
// two clock reads), short against what one worker may be left with at the end.
constexpr int64_t kChunkNs = 50'000;
// A bounded light element function: how long the calling thread works alone before it
// considers help, and how much work must be left (at the measured pace) to ask for it.
constexpr int64_t kProbeNs = 20'000;
constexpr int64_t kSpawnNs = 200'000;

} // namespace

template <typename Body>
void TaskflowExecSchedPass::runElements(
    const ElementCallSite &site, GraphPlan *targetPlan, size_t n, Body &&body) {
    if (n == 0) {
        return;
    }
    // The counter every worker of the operator writes and the chunk size they mostly read, on
    // lines of their own.
    struct Shared {
        alignas(64) std::atomic<size_t> next{0};
        alignas(64) std::atomic<size_t> grain{1};
    } shared;
    // Elements are claimed in rising order, so once a claim starts above the smallest
    // faulting index nothing that may still start is left.
    ElementFaults faults;
    const size_t workers  = executor_.num_workers();
    const size_t maxGrain = std::max<size_t>(1, n / (workers * 8));

    // What the calling thread measured while it worked alone: the time and the chunks so far,
    // and how long the rest would take at the pace of the last chunk. The first chunk is not
    // taken as the pace (it pays for whatever the element function does once: resolving its
    // operators, building the plans of the graphs it calls) unless it took milliseconds.
    int64_t probedNs    = 0;
    size_t probedChunks = 0;
    int64_t probedLeft  = 0;

    // Claims and runs chunks on the calling thread until none is left. With `probing`,
    // returns false as soon as the rest is worth helpers.
    const auto drain = [&](bool probing) {
        TaskflowGcRegion region;
        InheritedDepth inherited(region.thread(), site.depth, site.domain);
        for (;;) {
            const size_t grain = shared.grain.load(std::memory_order_relaxed);
            const size_t begin = shared.next.fetch_add(grain, std::memory_order_relaxed);
            if (begin >= n || faults.lowest.load(std::memory_order_acquire) < begin) {
                return true;
            }
            const size_t end   = std::min(n, begin + grain);
            const int64_t from = steadyNowNs();
            for (size_t i = begin; i < end; ++i) {
                // Nothing escapes: an element that faults is kept in `faults`, and the
                // elements after it in this chunk are skipped there.
                faults.run(i, [&] { body(region, i); });
            }
            const int64_t took = std::max<int64_t>(1, steadyNowNs() - from);
            const size_t count = end - begin;
            const auto fits    = static_cast<size_t>(
                std::min<int64_t>(kChunkNs * static_cast<int64_t>(count) / took, 1 << 20));
            shared.grain.store(
                std::clamp<size_t>(fits, 1, std::min(maxGrain, grain * 4)),
                std::memory_order_relaxed);
            if (probing) {
                probedNs += took;
                ++probedChunks;
                const size_t claimed = shared.next.load(std::memory_order_relaxed);
                if (claimed >= n) {
                    return true;
                }
                probedLeft = took * static_cast<int64_t>(n - claimed) / static_cast<int64_t>(count);
                if (probedLeft >= kSpawnNs && probedNs >= kProbeNs &&
                    (probedChunks >= 2 || took >= 10 * kSpawnNs)) {
                    return false;
                }
            }
        }
    };

    bool done = false;
    if (workers <= 1 || n == 1 || !site.independent) {
        done = drain(false);
    } else if (boundedLight(*targetPlan)) {
        done = drain(true);
    }
    if (!done) {
        const size_t claimed = std::min(n, shared.next.load(std::memory_order_relaxed));
        size_t helpers       = std::min(workers, n - claimed);
        if (probedChunks != 0) {
            // As many as the work left can keep busy for kSpawnNs each.
            helpers =
                std::min(helpers, static_cast<size_t>(std::max<int64_t>(2, probedLeft / kSpawnNs)));
        }
        tf::Taskflow flow;
        for (size_t helper = 0; helper < helpers; ++helper) {
            flow.emplace([&drain]() { (void)drain(false); }).name("ELEMENTS");
        }
        taskGraphsRun_.fetch_add(1, std::memory_order_relaxed);
        runOnExecutor(flow);
    }
    faults.rethrow();
}

void TaskflowExecSchedPass::mark_map_arr(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    TaskflowGcRegion region;
    const ElementCallSite site{
        graph,
        nodeRef,
        region.thread().callDepth,
        region.thread().faultDomain,
        elementsIndependent(graph, nodeRef)};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    const auto resIdx  = dataIndexOf(graph, nodeRef);
    GCGraph *target =
        makeHigherOrderCallSite(programs_, frame->get<Function *>(funcIdx)).runtimeGraph;
    GraphPlan *plan    = planFor(target);
    const size_t n     = arrayAt(frame, arrIdx)->size();
    auto *resultType   = frame->typeAt<ArrayType>(resIdx);
    {
        Array *res = Array::create(camel::core::mm::autoSpace(), resultType, n);
        // The result array is reachable from the node's slot from here on; each element stores
        // its value at its own position.
        frame->set(resIdx, res);
    }
    region.leave();

    runElements(site, plan, n, [&](TaskflowGcRegion &elementRegion, size_t i) {
        callClosure(
            elementRegion,
            site,
            target,
            plan,
            frame,
            funcIdx,
            1,
            [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
            [&](slot_t value) { arrayAt(frame, resIdx)->set<slot_t>(i, value, resultType); });
    });
}

void TaskflowExecSchedPass::mark_filter_arr(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    TaskflowGcRegion region;
    const ElementCallSite site{
        graph,
        nodeRef,
        region.thread().callDepth,
        region.thread().faultDomain,
        elementsIndependent(graph, nodeRef)};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target =
        makeHigherOrderCallSite(programs_, frame->get<Function *>(funcIdx)).runtimeGraph;
    GraphPlan *plan    = planFor(target);
    const size_t n     = arrayAt(frame, arrIdx)->size();
    region.leave();

    // One byte per element: the predicates run concurrently, and the bits of a
    // std::vector<bool> share words.
    std::vector<uint8_t> keep(n, 0);
    runElements(site, plan, n, [&](TaskflowGcRegion &elementRegion, size_t i) {
        callClosure(
            elementRegion,
            site,
            target,
            plan,
            frame,
            funcIdx,
            1,
            [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
            [&](slot_t value) { keep[i] = fromSlot<bool>(value) ? 1 : 0; });
    });

    region.enter();
    Array *arr       = arrayAt(frame, arrIdx);
    auto *resultType = frame->typeAt<ArrayType>(dataIndexOf(graph, nodeRef));
    Array *filtered  = Array::create(camel::core::mm::autoSpace(), resultType, 0);
    // Rooted before it grows: appending allocates.
    frame->set(dataIndexOf(graph, nodeRef), filtered);
    for (size_t i = 0; i < n; ++i) {
        if (keep[i] != 0) {
            filtered->append(arr->get<slot_t>(i), resultType);
        }
    }
    filtered->shrinkToFit(resultType);
}

void TaskflowExecSchedPass::mark_reduce_arr(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    TaskflowGcRegion region;
    const ElementCallSite site{
        graph,
        nodeRef,
        region.thread().callDepth,
        region.thread().faultDomain,
        elementsIndependent(graph, nodeRef)};
    region.enter();
    const auto arrIdx     = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto withInputs = graph->withInputsOf(nodeRef);
    const auto funcIdx    = dataIndexOf(graph, withInputs[0]);
    GCGraph *target =
        makeHigherOrderCallSite(programs_, frame->get<Function *>(funcIdx)).runtimeGraph;
    GraphPlan *plan       = planFor(target);
    const auto accIdx     = dataIndexOf(graph, nodeRef);
    const size_t n        = arrayAt(frame, arrIdx)->size();
    // The accumulator lives in the node's own slot between two steps.
    frame->set(accIdx, frame->get<slot_t>(dataIndexOf(graph, withInputs[1])));

    for (size_t i = 0; i < n; ++i) {
        callClosure(
            region,
            site,
            target,
            plan,
            frame,
            funcIdx,
            2,
            [&](std::span<slot_t> args) {
                args[0] = frame->get<slot_t>(accIdx);
                args[1] = arrayAt(frame, arrIdx)->get<slot_t>(i);
            },
            [&](slot_t value) { frame->set(accIdx, value); });
    }
}

void TaskflowExecSchedPass::mark_unordered_reduce_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    TaskflowGcRegion region;
    const uint32_t depth        = region.thread().callDepth;
    TaskflowFaultDomain *domain = region.thread().faultDomain;
    region.enter();
    const auto arrIdx     = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto withInputs = graph->withInputsOf(nodeRef);
    const auto funcIdx    = dataIndexOf(graph, withInputs[0]);
    const auto initIdx    = dataIndexOf(graph, withInputs[1]);
    GCGraph *target =
        makeHigherOrderCallSite(programs_, frame->get<Function *>(funcIdx)).runtimeGraph;
    const size_t n        = arrayAt(frame, arrIdx)->size();
    if (n == 0) {
        frame->set(dataIndexOf(graph, nodeRef), frame->get<slot_t>(initIdx));
        return;
    }

    const size_t workerHint = std::max<size_t>(2, executor_.num_workers());
    const size_t chunkGoal  = n <= 1 ? size_t(1) : std::max<size_t>(2, std::min(workerHint, n / 2));
    const size_t chunkCount = std::min(n, chunkGoal);
    const size_t chunkSize  = (n + chunkCount - 1) / chunkCount;
    // The partial results wait in a rooted scratch array (its elements have the array's
    // element type). A chunk past the end of the array has no partial.
    auto *arrType = frame->typeAt<ArrayType>(arrIdx);
    camel::core::mm::RootHandle partialsRoot(
        camel::core::mm::autoSpace(),
        "Taskflow.unordered_reduce.partials");
    {
        Array *partials = Array::create(camel::core::mm::autoSpace(), chunkCount);
        for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
            partials->set<slot_t>(chunk, NullSlot);
        }
        partialsRoot.reset(partials, arrType);
    }
    std::vector<uint8_t> hasPartial(chunkCount, 0);
    region.leave();

    // Folds `count` values into `acc` inside the caller's region: between two calls the
    // accumulator is in no frame, so the region is not left in between.
    const auto fold = [this, graph, nodeRef, target, frame, funcIdx](
                          TaskflowGcRegion &foldRegion,
                          slot_t acc,
                          size_t count,
                          const std::function<slot_t(size_t)> &valueAt) {
        for (size_t i = 0; i < count; ++i) {
            requireCallRoom(foldRegion.thread(), graph, nodeRef);
            OpenCall openCall(foldRegion.thread().callDepth);
            foldRegion.enter();
            std::array<slot_t, 2> args{acc, valueAt(i)};
            acc = callGraph(target, nullptr, foldRegion, [&](Frame *dest) {
                fillClosureCallFrame(dest, target, closureAt(frame, funcIdx), args);
            });
        }
        return acc;
    };

    // A chunk is a run of consecutive elements folded in order: the smallest faulting chunk
    // holds the smallest faulting element.
    ElementFaults faults;
    tf::Taskflow level;
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const size_t begin = chunk * chunkSize;
        const size_t end   = std::min(n, begin + chunkSize);
        if (begin >= end) {
            continue;
        }
        level
            .emplace([frame,
                      arrIdx,
                      begin,
                      end,
                      chunk,
                      arrType,
                      depth,
                      domain,
                      &partialsRoot,
                      &hasPartial,
                      &fold,
                      &faults]() {
                faults.run(chunk, [&] {
                    TaskflowGcRegion chunkRegion;
                    InheritedDepth inherited(chunkRegion.thread(), depth, domain);
                    chunkRegion.enter();
                    const slot_t acc = fold(
                        chunkRegion,
                        arrayAt(frame, arrIdx)->get<slot_t>(begin),
                        end - begin - 1,
                        [frame, arrIdx, begin](size_t i) {
                            return arrayAt(frame, arrIdx)->get<slot_t>(begin + 1 + i);
                        });
                    chunkRegion.enter();
                    partialsRoot.getAs<Array>()->set<slot_t>(chunk, acc, arrType);
                    hasPartial[chunk] = 1;
                });
            })
            .name("UNORDERED_REDUCE_CHUNK");
    }
    runOnExecutor(level);
    faults.rethrow();

    region.enter();
    std::vector<size_t> filled;
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
        if (hasPartial[chunk] != 0) {
            filled.push_back(chunk);
        }
    }
    const slot_t acc = fold(
        region,
        frame->get<slot_t>(initIdx),
        filled.size(),
        [&partialsRoot, &filled](size_t i) {
            return partialsRoot.getAs<Array>()->get<slot_t>(filled[i]);
        });
    region.enter();
    frame->set(dataIndexOf(graph, nodeRef), acc);
}

void TaskflowExecSchedPass::mark_foreach_arr(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    TaskflowGcRegion region;
    const ElementCallSite site{
        graph,
        nodeRef,
        region.thread().callDepth,
        region.thread().faultDomain,
        elementsIndependent(graph, nodeRef)};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target =
        makeHigherOrderCallSite(programs_, frame->get<Function *>(funcIdx)).runtimeGraph;
    GraphPlan *plan    = planFor(target);
    const size_t n     = arrayAt(frame, arrIdx)->size();

    for (size_t i = 0; i < n; ++i) {
        callClosure(
            region,
            site,
            target,
            plan,
            frame,
            funcIdx,
            1,
            [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
            [](slot_t) {});
    }
    region.enter();
    frame->set(dataIndexOf(graph, nodeRef), NullSlot);
}

void TaskflowExecSchedPass::mark_unordered_foreach_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    TaskflowGcRegion region;
    const ElementCallSite site{
        graph,
        nodeRef,
        region.thread().callDepth,
        region.thread().faultDomain,
        elementsIndependent(graph, nodeRef)};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target =
        makeHigherOrderCallSite(programs_, frame->get<Function *>(funcIdx)).runtimeGraph;
    GraphPlan *plan    = planFor(target);
    const size_t n     = arrayAt(frame, arrIdx)->size();
    region.leave();

    runElements(site, plan, n, [&](TaskflowGcRegion &elementRegion, size_t i) {
        callClosure(
            elementRegion,
            site,
            target,
            plan,
            frame,
            funcIdx,
            1,
            [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
            [](slot_t) {});
    });
    region.enter();
    frame->set(dataIndexOf(graph, nodeRef), NullSlot);
}

// The fault-isolation node (docs/technical/33_fault_isolation.md, 3.6).
//
// The callee runs on the calling thread, like any call, in a fault domain of its own. Tasks it
// starts inherit the domain. Every flow is joined by the thread that started it, so when a
// fault arrives here as an exception, no task of the boundary is still running: the node gives
// its error value only after everything that had started inside the boundary has ended.
slot_t
TaskflowExecSchedPass::executeIsolateNode(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    const auto *node = requireNode(graph, nodeRef);
    TaskflowGcRegion region;
    auto &thread = region.thread();
    region.enter();
    const auto normInputs = graph->normInputsOf(nodeRef);
    ASSERT(!normInputs.empty(), "The fault-isolation node requires a function input.");
    Function *func = frame->get<Function *>(dataIndexOf(graph, normInputs.front()));
    ASSERT(func != nullptr && func->graph() != nullptr, "Fault-isolation callee is null.");
    GCGraph *target = programs_.graphOf(func);

    // The room for the call is the caller's: a recursion limit reached here is a fault of the
    // enclosing graph, outside the boundary.
    requireCallRoom(thread, graph, nodeRef);
    OpenCall openCall(thread.callDepth);

    std::vector<slot_t> args;
    args.reserve(normInputs.size() - 1);
    for (size_t i = 1; i < normInputs.size(); ++i) {
        args.push_back(frame->get<slot_t>(dataIndexOf(graph, normInputs[i])));
    }
    Tuple *closure = func->tuple();

    TaskflowFaultDomain domain;

    slot_t value = NullSlot;
    try {
        FaultIsolationScope boundary;
        domain.isolationDepth = faultIsolationDepth();
        struct DomainScope {
            DomainScope(TaskflowGcGate::ThreadState &t, TaskflowFaultDomain *d)
                : thread(t), saved(t.faultDomain) {
                thread.faultDomain = d;
            }
            ~DomainScope() { thread.faultDomain = saved; }
            TaskflowGcGate::ThreadState &thread;
            TaskflowFaultDomain *saved;
        } domainScope(thread, &domain);
        // Nothing polls between reading `closure` above and filling the callee frame here.
        value = callGraph(target, nullptr, region, [&](Frame *dest) {
            fillClosureCallFrame(dest, target, closure, args);
        });
    } catch (const Diagnostic &fault) {
        if (!isIsolatedFault(fault)) {
            throw;
        }
        region.enter();
        const slot_t failed = camel::execute::isolatedFaultToError(fault);
        frame->set(node->dataIndex, failed);
        return failed;
    }
    region.enter();
    const auto *calleeType   = target->funcType();
    const Type *calleeResult = calleeType ? calleeType->exitType() : nullptr;
    const slot_t result      = camel::execute::wrapIsolatedResult(
        value,
        calleeResult,
        frame->typeAt<Type>(node->dataIndex));
    frame->set(node->dataIndex, result);
    return result;
}

template void TaskflowExecSchedPass::instantiate_graph_instance_generic<tf::Taskflow>(
    tf::Taskflow &flowLike, GCGraph *runtimeGraph, Frame *frame);
