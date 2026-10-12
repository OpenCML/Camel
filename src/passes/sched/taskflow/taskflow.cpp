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
 * Updated: Oct. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"

#include "camel/core/effect_trace.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
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

TaskflowHigherOrderCallSite makeHigherOrderCallSite(Function *func) {
    auto *runtimeGraph = func ? func->graph() : nullptr;
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
    info.skipNodes.resize(graph->nodeBlockCount(), 0);
    info.branchArms.resize(graph->nodeBlockCount());

    for (gc_node_ref_t nodeRef : camel::execute::buildReachableExecutionTopoIndices(graph)) {
        const auto *node = requireNode(graph, nodeRef);
        if (node->kind != GCNodeKind::Brch) {
            continue;
        }
        const auto ctrlOutputs = graph->ctrlOutputsOf(nodeRef);
        auto &arms             = info.branchArms[nodeRef];
        arms.reserve(ctrlOutputs.size());
        for (size_t armIndex = 0; armIndex < ctrlOutputs.size(); ++armIndex) {
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

// True when the calling thread has used fifteen sixteenths of its native stack: one more call
// of a graph is refused with a diagnostic instead of running into the guard page. The
// scheduler has no fixed limit of its own (NodeVM stops at 256 frames); what a thread can nest
// depends on its stack and on what else the worker picked up while waiting. The depth the
// diagnostic reports is the number of calls open on that thread (a recursion that started on
// another worker is deeper than that).
bool nativeStackIsExhausted(const TaskflowGcGate::ThreadState &ts) {
    return camel::execute::nativeStackGuardEnabled() && nativeStackUsedBeyond(ts, 15, 16);
}

// One more call open on a thread, for as long as the object lives.
struct OpenCall {
    explicit OpenCall(uint32_t &depth) : depth_(depth) { ++depth_; }
    ~OpenCall() { --depth_; }
    uint32_t &depth_;
};

// A task continues from the calls open on the thread that started it: while the object lives,
// the running thread counts from `depth`; afterwards from what it had (a worker that picked
// the task up while waiting inside another call).
struct InheritedDepth {
    InheritedDepth(TaskflowGcGate::ThreadState &thread, uint32_t depth)
        : depth_(thread.callDepth), saved_(thread.callDepth) {
        depth_ = depth;
    }
    ~InheritedDepth() { depth_ = saved_; }
    uint32_t &depth_;
    uint32_t saved_;
};

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool envSwitch(const char *name) {
    const char *raw = std::getenv(name);
    return raw != nullptr && raw[0] != '\0' && !(raw[0] == '0' && raw[1] == '\0');
}

struct PlanMismatch {
    std::string reason;
};

} // namespace

namespace {

// CAMEL_TF_GC_STATS=2: one line per collection, and per time the collector gave up.
bool gcTraceEnabled() {
    static const bool enabled = [] {
        const char *raw = std::getenv("CAMEL_TF_GC_STATS");
        return raw != nullptr && raw[0] == '2';
    }();
    return enabled;
}

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
    constexpr auto kPatience           = 500ms;
    constexpr int64_t kFirstBackoffNs  = 10'000'000;
    constexpr int64_t kMaxBackoffNs    = 1'000'000'000;
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
    bool quiet = false;
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
                // Built-in operators have an empty protocol (":op/add_l"); ":mark/..." are
                // the higher-order ones.
                heavy = body == nullptr || !body->uri().starts_with(":") ||
                        body->uri().starts_with(":mark/");
            } else if (node->kind == GCNodeKind::Func) {
                GCGraph *callee = current->directCalleeGraphOf(it.ref());
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

TaskflowExecSchedPass::GraphPlan *TaskflowExecSchedPass::planFor(GCGraph *graph) {
    ASSERT(graph != nullptr, "Taskflow plan lookup requires a runtime graph.");
    {
        std::shared_lock lock(plansMutex_);
        if (auto it = plans_.find(graph); it != plans_.end()) {
            return it->second.get();
        }
    }
    // Built outside the lock; two workers that meet a graph for the first time may both build
    // it, and the first to publish wins. A published plan is never changed or removed.
    auto built = buildGraphPlan(graph);
    std::unique_lock lock(plansMutex_);
    auto [it, inserted] = plans_.try_emplace(graph, std::move(built));
    (void)inserted;
    return it->second.get();
}

std::unique_ptr<TaskflowExecSchedPass::GraphPlan>
TaskflowExecSchedPass::buildGraphPlan(GCGraph *graph) {
    auto plan   = std::make_unique<GraphPlan>();
    plan->graph = graph;
    plan->topo  = camel::execute::buildReachableExecutionTopoIndices(graph);

    // Tail position, by the rule of the node-by-node evaluator: the tail value must be the
    // last thing in the linear order except for forwarding GATEs.
    plan->lastNode       = camel::execute::resolveRuntimeTailValueRef(graph);
    plan->lastNodeIsJoin = plan->lastNode != kInvalidNodeRef &&
                           graph->node(plan->lastNode)->kind == GCNodeKind::Join;
    const auto anchor = std::find(plan->topo.begin(), plan->topo.end(), plan->lastNode);
    plan->anchorOk    = anchor != plan->topo.end() &&
                     camel::execute::hasOnlyTrivialRuntimeTailSuffixAfter(
                         graph,
                         plan->topo,
                         static_cast<size_t>(anchor - plan->topo.begin()));
    if (plan->anchorOk) {
        // A call that feeds an inner JOIN of nested branches in tail position (what inlining
        // a recursive function into itself leaves) is a tail call too: taken as an ordinary
        // call, a 50000-level tail recursion overflowed the native stack.
        std::vector<gc_node_ref_t> candidates{plan->lastNode};
        if (plan->lastNodeIsJoin) {
            const auto armValues =
                camel::execute::collectRuntimeTailJoinArmValues(graph, plan->lastNode);
            candidates.insert(candidates.end(), armValues.begin(), armValues.end());
        }
        for (gc_node_ref_t ref : candidates) {
            const auto *node = ref != kInvalidNodeRef ? graph->node(ref) : nullptr;
            if (node != nullptr && node->kind == GCNodeKind::Func) {
                plan->tailCalls.push_back(ref);
            }
        }
        std::sort(plan->tailCalls.begin(), plan->tailCalls.end());
        plan->tailCalls.erase(
            std::unique(plan->tailCalls.begin(), plan->tailCalls.end()),
            plan->tailCalls.end());
    }

    if (!forceLinear_) {
        try {
            buildStructuredPlan(*plan);
            plan->structured = true;
        } catch (const PlanMismatch &mismatch) {
            plan->scopes.clear();
            plan->branches.clear();
            plan->structured = false;
            if (envSwitch("CAMEL_TF_DIAG")) {
                std::cerr << std::format(
                    "[taskflow] graph '{}' is evaluated node by node: {}\n",
                    graph->name(),
                    mismatch.reason);
            }
        }
    }
    if (plan->structured && envSwitch("CAMEL_TF_DIAG")) {
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
    const size_t blocks    = graph->nodeBlockCount();
    constexpr uint32_t kNo = std::numeric_limits<uint32_t>::max();

    std::vector<uint32_t> pos(blocks, kNo);
    for (size_t i = 0; i < topo.size(); ++i) {
        if (topo[i] >= blocks) {
            throw PlanMismatch{"a node ref lies outside the node table"};
        }
        pos[topo[i]] = static_cast<uint32_t>(i);
    }
    const uint32_t anchorPos =
        plan.anchorOk && plan.lastNode < blocks ? pos[plan.lastNode] : kNo;

    const auto isElided = [](GCNodeKind kind) {
        return kind == GCNodeKind::Data || kind == GCNodeKind::Port || kind == GCNodeKind::Sync ||
               kind == GCNodeKind::Bind || kind == GCNodeKind::Dref;
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
    std::function<void(int32_t, size_t, size_t)> parse = [&](int32_t scopeIdx, size_t lo,
                                                             size_t hi) {
        for (size_t i = lo; i < hi;) {
            const gc_node_ref_t ref = topo[i];
            const auto *node        = requireNode(graph, ref);
            if (isElided(node->kind)) {
                ++i;
                continue;
            }
            PlanItem item;
            item.node = ref;
            auto &items = plan.scopes[static_cast<size_t>(scopeIdx)]->items;
            const auto itemIdx = static_cast<uint32_t>(items.size());

            if (node->kind == GCNodeKind::Join) {
                throw PlanMismatch{"a JOIN is reached outside its branch region"};
            }
            if (node->kind == GCNodeKind::Brch) {
                const auto *body = graph->nodeBodyAs<camel::runtime::GCBrchBody>(ref);
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

                const auto arms = graph->branchArmsOf(ref);
                std::vector<int32_t> armScopes;
                armScopes.reserve(arms.size());
                for (size_t arm = 0; arm < arms.size(); ++arm) {
                    const gc_node_ref_t entry =
                        camel::execute::resolveRuntimeBranchArmEntry(graph, ref, arm, topo);
                    if (entry == join) {
                        armScopes.push_back(-1);
                        continue;
                    }
                    const gc_node_ref_t tail = arms[arm].tail;
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
                const auto *body = graph->nodeBodyAs<GCOperBody>(ref);
                item.kind =
                    body->uri().starts_with(":mark/") ? ItemKind::Mark : ItemKind::Leaf;
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
                 {graph->normOutputsOf(ref), graph->withOutputsOf(ref), graph->ctrlOutputsOf(ref)}) {
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
                worth = true;
                break;
            case ItemKind::Call:
                worth = requireNode(graph, item.node)->kind != GCNodeKind::Func ||
                        graphIsHeavy(graph->directCalleeGraphOf(item.node));
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

camel::runtime::GCGraph *
TaskflowExecSchedPass::apply(camel::runtime::GCGraph *graph, std::ostream & /*os*/) {
    ASSERT(graph != nullptr, "Taskflow requires a non-null runtime root graph.");

    forceLinear_ = envSwitch("CAMEL_TF_LINEAR");
    fanOutAll_   = envSwitch("CAMEL_TF_FANOUT_ALL");
    taskGraphsRun_.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(heavyMutex_);
        heavyGraphs_.clear();
    }
    aborting_.store(false, std::memory_order_relaxed);
    {
        std::lock_guard lock(faultMutex_);
        firstFault_ = nullptr;
    }
    {
        std::unique_lock lock(plansMutex_);
        plans_.clear();
    }
    linearArmEntryCache_.clear();
    const bool gcStats = envSwitch("CAMEL_TF_GC_STATS");
    TaskflowGcGate::instance().resetStats();
    Frame *rootFrame = framePool_.acquire(graph);
    try {
        slot_t result = evalGraphTF(graph, rootFrame);
        context_->captureProcessExitCode(graph, result);
        framePool_.release(rootFrame);
    } catch (...) {
        framePool_.release(rootFrame);
        if (gcStats) {
            TaskflowGcGate::instance().reportStats(std::cerr);
        }
        throw;
    }
    if (gcStats) {
        TaskflowGcGate::instance().reportStats(std::cerr);
        std::cerr << std::format(
            "[taskflow] run: task_graphs={}\n",
            taskGraphsRun_.load(std::memory_order_relaxed));
    }
    return nullptr;
}

slot_t TaskflowExecSchedPass::evalGraphTF(GCGraph *graph, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow evaluation requires a runtime graph.");
    TaskflowGcRegion region;
    return evalGraph(graph, frame, region);
}

slot_t TaskflowExecSchedPass::evalGraph(GCGraph *graph, Frame *frame, TaskflowGcRegion &region) {
    ASSERT(graph != nullptr, "Taskflow evaluation requires a runtime graph.");
    Frame *rootFrame = frame;
    Frame *currFrame = frame;
    Frame *twinFrame = nullptr;
    GraphPlan *plan  = planFor(graph);
    std::atomic<uint32_t> tailNode{kInvalidNodeRef};

    for (;;) {
        // Nothing of this graph instance is in flight here: every live value is in a frame.
        region.leave();
        region.poll("taskflow graph boundary");

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
        GCGraph *target      = callerGraph->directCalleeGraphOf(tailRef);
        ASSERT(target != nullptr, "Taskflow tail FUNC target graph is null.");
        Frame *lastFrame = currFrame;
        if (target == callerGraph) {
            camel::execute::fillFrameForDirectInvoke(lastFrame, currFrame, callerGraph, tailRef);
            continue;
        }
        plan = planFor(target);
        if (twinFrame && twinFrame->runtimeGraph() == target) {
            currFrame = twinFrame;
            twinFrame = lastFrame;
        } else {
            if (twinFrame != nullptr && twinFrame != rootFrame) {
                framePool_.release(twinFrame);
            }
            twinFrame = currFrame;
            currFrame = framePool_.acquire(target);
        }
        camel::execute::fillFrameForDirectInvoke(lastFrame, currFrame, callerGraph, tailRef);
    }

    region.enter();
    slot_t result = camel::execute::readRuntimeGraphReturn(plan->graph, currFrame);
    if (twinFrame != nullptr) {
        if (currFrame != rootFrame) {
            framePool_.release(currFrame);
        }
        if (twinFrame != rootFrame) {
            framePool_.release(twinFrame);
        }
    }
    return result;
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
            tasks.push_back(
                instance->flow
                    .emplace([this, self, planPtr = &plan, scopePtr = &scope, chain]() {
                        // Nothing may escape into the executor: the first failure is kept
                        // for the thread that runs this scope, and everything not yet
                        // started is skipped.
                        try {
                            TaskflowGcRegion taskRegion;
                            InheritedDepth inherited(taskRegion.thread(), self->parentDepth);
                            runItems(
                                *planPtr,
                                scopePtr->chains[chain],
                                self->frame,
                                *self->tailNode,
                                taskRegion);
                        } catch (...) {
                            aborting_.store(true, std::memory_order_release);
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
    instance->frame       = frame;
    instance->tailNode    = &tailNode;
    instance->parentDepth = region.thread().callDepth;
    region.leave(); // the calling thread waits for (and helps with) the tasks from here on
    runOnExecutor(instance->flow);
    std::exception_ptr error = std::exchange(instance->error, nullptr);
    {
        std::lock_guard lock(scope.poolMutex);
        scope.idle.push_back(std::move(instance));
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

void TaskflowExecSchedPass::runItems(
    GraphPlan &plan, std::span<const PlanItem> items, Frame *frame,
    std::atomic<uint32_t> &tailNode, TaskflowGcRegion &region) {
    GCGraph *graph = plan.graph;
    for (const PlanItem &item : items) {
        if (aborting_.load(std::memory_order_relaxed)) [[unlikely]] {
            return;
        }
        switch (item.kind) {
        case ItemKind::Leaf: {
            if (item.afterTail && tailNode.load(std::memory_order_acquire) != kInvalidNodeRef) {
                continue;
            }
            region.enter();
            (void)executeLinearNode(graph, item.node, frame);
            if (requireNode(graph, item.node)->kind == GCNodeKind::Oper) {
                // An operator can run for long; do not keep a collector waiting for the
                // next one as well.
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
        case ItemKind::Mark:
            region.leave();
            (void)executeLinearMarkedOperator(graph, item.node, frame);
            break;
        case ItemKind::Branch: {
            const PlanBranch &branch = plan.branches[item.branch];
            region.leave();
            region.poll("taskflow branch boundary");
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
            if (aborting_.load(std::memory_order_relaxed)) [[unlikely]] {
                return;
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

    for (size_t i = 0; i < nodes.size(); ++i) {
        if (aborting_.load(std::memory_order_relaxed)) [[unlikely]] {
            return;
        }
        const gc_node_ref_t nodeRef = nodes[i];
        const auto *node            = requireNode(graph, nodeRef);

        if (tillNode != kInvalidNodeRef) {
            if (tillNode == nodeRef) {
                tillNode = kInvalidNodeRef;
            } else {
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

            const auto arms  = graph->branchArmsOf(nodeRef);
            const auto *body = graph->nodeBodyAs<camel::runtime::GCBrchBody>(nodeRef);
            ASSERT(jumpIdx < arms.size(), "Taskflow linear BRCH arm index is out of range.");
            // Same arm range as NodeVM: from the arm's first executable node in the linear
            // order (nodes of an inlined arm can precede its recorded head) through its tail.
            if (skipNode != kInvalidNodeRef) {
                enclosingArms.emplace_back(skipNode, joinNode);
            }
            joinNode = body->join;
            tillNode = branchArmEntryForLinear(graph, nodeRef, jumpIdx, nodes);
            skipNode = tillNode == joinNode ? kInvalidNodeRef : arms[jumpIdx].tail;
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
        case GCNodeKind::Bind:
        case GCNodeKind::Dref:
            break;
        case GCNodeKind::Oper:
            if (graph->nodeBodyAs<GCOperBody>(nodeRef)->uri().starts_with(":mark/")) {
                region.leave();
                (void)executeLinearMarkedOperator(graph, nodeRef, frame);
                break;
            }
            region.enter();
            (void)executeLinearNode(graph, nodeRef, frame);
            region.leave();
            break;
        default:
            region.enter();
            (void)executeLinearNode(graph, nodeRef, frame);
            break;
        }
    }
}

void TaskflowExecSchedPass::raiseFault(
    const RuntimeFault &fault, GCGraph *graph, gc_node_ref_t nodeRef) {
    std::lock_guard lock(faultMutex_);
    if (!firstFault_) {
        auto site = makeGraphExecutionSite(context_->sourceContext(), graph, 0, "taskflow");
        if (nodeRef != kInvalidNodeRef) {
            if (const uint64_t origin = graph->nodeOrigin(nodeRef)) {
                site.cachedOrigin = origin;
            }
        }
        // Reporting records the diagnostic and gives it back to be thrown, or throws itself
        // when the diagnostics sink stops at the first error (as the command line sets it).
        try {
            throw reportRuntimeFault(*context_, fault, site);
        } catch (...) {
            firstFault_ = std::current_exception();
        }
    }
    std::rethrow_exception(firstFault_);
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
    // With no limit given, the scheduler nests as deep as the native stack of the thread
    // allows; what a thread can nest depends on its stack and on what else the worker picked
    // up while waiting.
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
        targetGraph = graph->directCalleeGraphOf(nodeRef);
    } else {
        const auto withInputs = graph->withInputsOf(nodeRef);
        ASSERT(!withInputs.empty(), "Taskflow CALL node must have function input.");
        Function *func = frame->get<Function *>(dataIndexOf(graph, withInputs.front()));
        targetGraph    = func ? func->graph() : nullptr;
    }
    ASSERT(targetGraph != nullptr, "Taskflow call target graph is null.");
    auto &thread = region.thread();
    requireCallRoom(thread, graph, nodeRef);
    OpenCall openCall(thread.callDepth);
    Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
    slot_t result      = NullSlot;
    try {
        result = evalGraph(targetGraph, calleeFrame, region);
    } catch (...) {
        framePool_.release(calleeFrame);
        throw;
    }
    frame->set(node->dataIndex, result);
    framePool_.release(calleeFrame);
}

template <typename FillArgs, typename Sink>
void TaskflowExecSchedPass::callClosure(
    const ElementCallSite &site, GCGraph *targetGraph, Frame *funcFrame,
    camel::runtime::gc_data_idx_t funcIdx, size_t argCount, FillArgs &&fillArgs, Sink &&sink) {
    std::array<slot_t, 2> args{NullSlot, NullSlot};
    ASSERT(argCount <= args.size(), "Taskflow closure call takes at most two arguments.");
    TaskflowGcRegion region;
    InheritedDepth inherited(region.thread(), site.depth);
    requireCallRoom(region.thread(), site.graph, site.node);
    OpenCall openCall(region.thread().callDepth);
    region.enter();
    fillArgs(std::span<slot_t>(args.data(), argCount));
    Frame *callee = acquirePreparedClosureCallFrame(
        targetGraph,
        closureAt(funcFrame, funcIdx),
        std::span<const slot_t>(args.data(), argCount));
    slot_t result = NullSlot;
    try {
        result = evalGraph(targetGraph, callee, region);
    } catch (...) {
        framePool_.release(callee);
        throw;
    }
    sink(result);
    framePool_.release(callee);
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

slot_t TaskflowExecSchedPass::executeLinearMarkedOperator(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    tf::Taskflow flow;
    const uint32_t depth = TaskflowGcGate::instance().self().callDepth;
    flow.emplace([this, graph, nodeRef, frame, depth](tf::Subflow &sf) {
        InheritedDepth inherited(TaskflowGcGate::instance().self(), depth);
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        const std::string uri(body->uri());
        if (uri == ":mark/map_arr") {
            mark_map_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/apply_arr") {
            mark_apply_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/filter_arr") {
            mark_filter_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/reduce_arr") {
            mark_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_reduce_arr") {
            mark_unordered_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/foreach_arr") {
            mark_foreach_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_foreach_arr") {
            mark_unordered_foreach_arr(graph, nodeRef, frame, sf);
        }
    });
    runOnExecutor(flow);
    const auto *node = requireNode(graph, nodeRef);
    return node->dataIndex == 0 ? NullSlot : frame->get<slot_t>(node->dataIndex);
}

slot_t
TaskflowExecSchedPass::executeLinearNode(GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
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
        if (node->dataType == Type::Void()) {
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
        Type *tgtType                   = node->dataType;
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
        Type *srcType                   = node->dataType;
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
        if (body->uri().starts_with(":mark/")) {
            return executeLinearMarkedOperator(graph, nodeRef, frame);
        }
        const slot_t result = executePreparedOperator(graph, nodeRef, frame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Brch:
        return NullSlot;

    case GCNodeKind::Bind:
    case GCNodeKind::Dref:
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
    Frame *dest = framePool_.acquire(targetGraph);

    const auto normPorts = targetGraph->normPorts();
    ASSERT(args.size() == normPorts.size(), "Taskflow closure call norm-arg count mismatch.");
    for (size_t i = 0; i < args.size(); ++i) {
        dest->set(dataIndexOf(targetGraph, normPorts[i]), args[i]);
    }

    const auto closureNodes = targetGraph->closureNodes();
    if (closureNodes.empty()) {
        return dest;
    }

    ASSERT(closure != nullptr, "Taskflow closure tuple is null.");
    ASSERT(closure->size() == closureNodes.size(), "Taskflow closure size mismatch.");
    for (size_t i = 0; i < closureNodes.size(); ++i) {
        dest->set(dataIndexOf(targetGraph, closureNodes[i]), closure->get<slot_t>(i));
    }
    return dest;
}

slot_t TaskflowExecSchedPass::executePreparedOperator(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame) {
    ASSERT(graph != nullptr, "Taskflow operator execution requires a graph.");
    ASSERT(frame != nullptr, "Taskflow operator execution requires a frame.");

    const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
    const std::string uri(body->uri());
    auto opFunc = context_->execMgr().find(uri);
    if (!opFunc) {
        raiseFault(RuntimeFault::make(RuntimeDiag::UnrecognizedOperatorURI, uri), graph, nodeRef);
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

    if (camel::core::effect_trace::enabled()) [[unlikely]] {
        camel::core::effect_trace::setSite(graph->nodeOrigin(nodeRef));
    }
    try {
        return (*opFunc)(withView, normView, *context_);
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
        if (node->dataType == Type::Void()) {
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
        Type *tgtType                   = node->dataType;
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
        Type *srcType                   = node->dataType;
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
        auto *targetGraph = graph->directCalleeGraphOf(nodeRef);
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
        auto *targetGraph = func ? func->graph() : nullptr;
        ASSERT(targetGraph != nullptr, "Taskflow CALL target graph is null.");
        Frame *calleeFrame = acquirePreparedRuntimeCallFrame(targetGraph, nodeRef, frame);
        slot_t result      = runPreparedSubgraph(sf, targetGraph, calleeFrame);
        frame->set(node->dataIndex, result);
        return result;
    }

    case GCNodeKind::Oper: {
        const auto *body = graph->nodeBodyAs<GCOperBody>(nodeRef);
        const std::string uri(body->uri());
        if (uri == ":mark/map_arr") {
            mark_map_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/apply_arr") {
            mark_apply_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/filter_arr") {
            mark_filter_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/reduce_arr") {
            mark_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_reduce_arr") {
            mark_unordered_reduce_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/foreach_arr") {
            mark_foreach_arr(graph, nodeRef, frame, sf);
        } else if (uri == ":mark/unordered_foreach_arr") {
            mark_unordered_foreach_arr(graph, nodeRef, frame, sf);
        } else {
            const slot_t result = executePreparedOperator(graph, nodeRef, frame);
            frame->set(node->dataIndex, result);
            return result;
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
        return NullSlot;
    }

    return NullSlot;
}

template <typename FlowT>
void TaskflowExecSchedPass::instantiate_graph_instance_generic(
    FlowT &flowLike, GCGraph *runtimeGraph, Frame *frame) {
    ASSERT(runtimeGraph != nullptr, "Taskflow graph instantiation requires a runtime graph.");
    auto buildInfo = std::make_shared<RuntimeBuildInfo>(buildRuntimeBuildInfo(runtimeGraph));
    std::vector<tf::Task> taskMap(runtimeGraph->nodeBlockCount());
    std::vector<uint8_t> taskBuilt(runtimeGraph->nodeBlockCount(), 0);
    buildRuntimeNodeTasks(flowLike, runtimeGraph, frame, buildInfo, taskMap, taskBuilt);
    connectRuntimeDependencies(flowLike, runtimeGraph, frame, buildInfo, taskMap, taskBuilt);
}

template <typename FlowT>
void TaskflowExecSchedPass::buildRuntimeNodeTasks(
    FlowT &flowLike, GCGraph *graph, Frame *frame,
    const std::shared_ptr<const RuntimeBuildInfo> &buildInfo, std::vector<tf::Task> &taskMap,
    std::vector<uint8_t> &taskBuilt) {
    for (gc_node_ref_t nodeRef : camel::execute::buildReachableExecutionTopoIndices(graph)) {
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
        if (node->kind == GCNodeKind::Bind || node->kind == GCNodeKind::Dref) {
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

    for (gc_node_ref_t nodeRef : camel::execute::buildReachableExecutionTopoIndices(graph)) {
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
        const size_t armCount = graph->branchArmsOf(brchRef).size();
        it->second.reserve(armCount);
        for (size_t arm = 0; arm < armCount; ++arm) {
            it->second.push_back(
                camel::execute::resolveRuntimeBranchArmEntry(graph, brchRef, arm, topoNodes));
        }
    }
    ASSERT(armIndex < it->second.size(), "Taskflow linear BRCH arm index is out of range.");
    return it->second[armIndex];
}

// ---------------------------------------------------------------------------------------------
// Higher-order operators
//
// Each keeps its own fan-out (one task per element, chained where the operator is ordered).
// What an element produces is stored where the collector finds it before the element's region
// ends: in the result array, in the node's own slot, or in a rooted scratch array.
// ---------------------------------------------------------------------------------------------

void TaskflowExecSchedPass::mark_map_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    TaskflowGcRegion region;
    const ElementCallSite site{graph, nodeRef, region.thread().callDepth};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    const auto resIdx  = dataIndexOf(graph, nodeRef);
    GCGraph *target    = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
    const size_t n     = arrayAt(frame, arrIdx)->size();
    auto *resultType   = frame->typeAt<ArrayType>(resIdx);
    {
        Array *res = Array::create(camel::core::mm::autoSpace(), n);
        for (size_t i = 0; i < n; ++i) {
            res->set<slot_t>(i, NullSlot);
        }
        // The result array is reachable from the node's slot from here on; each element stores
        // its value at its own position.
        frame->set(resIdx, res);
    }
    region.leave();

    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, site, i, frame, arrIdx, funcIdx, resIdx, resultType, target]() {
              callClosure(
                  site,
                  target,
                  frame,
                  funcIdx,
                  1,
                  [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
                  [&](slot_t value) {
                      arrayAt(frame, resIdx)->set<slot_t>(i, value, resultType);
                  });
          }).name("MAP_ELEM");
    }
    sf.join();
}

void TaskflowExecSchedPass::mark_apply_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    TaskflowGcRegion region;
    const ElementCallSite site{graph, nodeRef, region.thread().callDepth};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target    = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
    const size_t n     = arrayAt(frame, arrIdx)->size();
    auto *arrType      = frame->typeAt<ArrayType>(arrIdx);
    // Every element is computed from the array as it was; the results wait in a rooted
    // scratch array and are written back together.
    camel::core::mm::RootHandle resultsRoot(camel::core::mm::autoSpace(), "Taskflow.apply.results");
    {
        Array *results = Array::create(camel::core::mm::autoSpace(), n);
        for (size_t i = 0; i < n; ++i) {
            results->set<slot_t>(i, NullSlot);
        }
        resultsRoot.reset(results, arrType);
    }
    region.leave();

    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, site, i, frame, arrIdx, funcIdx, arrType, target, &resultsRoot]() {
              callClosure(
                  site,
                  target,
                  frame,
                  funcIdx,
                  1,
                  [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
                  [&](slot_t value) {
                      resultsRoot.getAs<Array>()->set<slot_t>(i, value, arrType);
                  });
          }).name("APPLY_ELEM");
    }
    sf.join();

    region.enter();
    Array *arr     = arrayAt(frame, arrIdx);
    Array *results = resultsRoot.getAs<Array>();
    for (size_t i = 0; i < n; ++i) {
        arr->set<slot_t>(i, results->get<slot_t>(i), arrType);
    }
    frame->set(dataIndexOf(graph, nodeRef), arr);
}

void TaskflowExecSchedPass::mark_filter_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    TaskflowGcRegion region;
    const ElementCallSite site{graph, nodeRef, region.thread().callDepth};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target    = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
    const size_t n     = arrayAt(frame, arrIdx)->size();
    region.leave();

    // One byte per element: the predicates run concurrently, and the bits of a
    // std::vector<bool> share words.
    std::vector<uint8_t> keep(n, 0);
    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, site, i, frame, arrIdx, funcIdx, target, &keep]() {
              callClosure(
                  site,
                  target,
                  frame,
                  funcIdx,
                  1,
                  [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
                  [&](slot_t value) { keep[i] = fromSlot<bool>(value) ? 1 : 0; });
          }).name("FILTER_PRED");
    }
    sf.join();

    region.enter();
    Array *arr       = arrayAt(frame, arrIdx);
    Array *filtered  = Array::create(camel::core::mm::autoSpace(), 0);
    auto *resultType = frame->typeAt<ArrayType>(dataIndexOf(graph, nodeRef));
    // Rooted before it grows: appending allocates.
    frame->set(dataIndexOf(graph, nodeRef), filtered);
    for (size_t i = 0; i < n; ++i) {
        if (keep[i] != 0) {
            filtered->append(arr->get<slot_t>(i), resultType);
        }
    }
    filtered->shrinkToFit(resultType);
}

void TaskflowExecSchedPass::mark_reduce_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    TaskflowGcRegion region;
    const ElementCallSite site{graph, nodeRef, region.thread().callDepth};
    region.enter();
    const auto arrIdx     = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto withInputs = graph->withInputsOf(nodeRef);
    const auto funcIdx    = dataIndexOf(graph, withInputs[0]);
    GCGraph *target       = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
    const auto accIdx     = dataIndexOf(graph, nodeRef);
    const size_t n        = arrayAt(frame, arrIdx)->size();
    // The accumulator lives in the node's own slot between two steps.
    frame->set(accIdx, frame->get<slot_t>(dataIndexOf(graph, withInputs[1])));
    region.leave();
    if (n == 0) {
        return;
    }

    tf::Task prev;
    bool hasPrev = false;
    for (size_t i = 0; i < n; ++i) {
        tf::Task step = sf.emplace([this, site, i, arrIdx, funcIdx, frame, accIdx, target]() {
                              callClosure(
                                  site,
                                  target,
                                  frame,
                                  funcIdx,
                                  2,
                                  [&](std::span<slot_t> args) {
                                      args[0] = frame->get<slot_t>(accIdx);
                                      args[1] = arrayAt(frame, arrIdx)->get<slot_t>(i);
                                  },
                                  [&](slot_t value) { frame->set(accIdx, value); });
                          }).name("REDUCE_STEP");
        if (hasPrev) {
            step.succeed(prev);
        }
        prev    = step;
        hasPrev = true;
    }
    sf.join();
}

void TaskflowExecSchedPass::mark_unordered_reduce_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    (void)sf;
    TaskflowGcRegion region;
    const uint32_t depth = region.thread().callDepth;
    region.enter();
    const auto arrIdx     = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto withInputs = graph->withInputsOf(nodeRef);
    const auto funcIdx    = dataIndexOf(graph, withInputs[0]);
    const auto initIdx    = dataIndexOf(graph, withInputs[1]);
    GCGraph *target       = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
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
                          TaskflowGcRegion &foldRegion, slot_t acc, size_t count,
                          const std::function<slot_t(size_t)> &valueAt) {
        for (size_t i = 0; i < count; ++i) {
            requireCallRoom(foldRegion.thread(), graph, nodeRef);
            OpenCall openCall(foldRegion.thread().callDepth);
            foldRegion.enter();
            std::array<slot_t, 2> args{acc, valueAt(i)};
            Frame *callee =
                acquirePreparedClosureCallFrame(target, closureAt(frame, funcIdx), args);
            try {
                acc = evalGraph(target, callee, foldRegion);
            } catch (...) {
                framePool_.release(callee);
                throw;
            }
            framePool_.release(callee);
        }
        return acc;
    };

    tf::Taskflow level;
    for (size_t chunk = 0; chunk < chunkCount; ++chunk) {
        const size_t begin = chunk * chunkSize;
        const size_t end   = std::min(n, begin + chunkSize);
        if (begin >= end) {
            continue;
        }
        level
            .emplace([frame, arrIdx, begin, end, chunk, arrType, depth, &partialsRoot,
                      &hasPartial, &fold]() {
                TaskflowGcRegion chunkRegion;
                InheritedDepth inherited(chunkRegion.thread(), depth);
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
            })
            .name("UNORDERED_REDUCE_CHUNK");
    }
    runOnExecutor(level);

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

void TaskflowExecSchedPass::mark_foreach_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    TaskflowGcRegion region;
    const ElementCallSite site{graph, nodeRef, region.thread().callDepth};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target    = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
    const size_t n     = arrayAt(frame, arrIdx)->size();
    region.leave();

    tf::Task prev;
    bool hasPrev = false;
    for (size_t i = 0; i < n; ++i) {
        tf::Task step = sf.emplace([this, site, i, frame, arrIdx, funcIdx, target]() {
                              callClosure(
                                  site,
                                  target,
                                  frame,
                                  funcIdx,
                                  1,
                                  [&](std::span<slot_t> args) {
                                      args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i);
                                  },
                                  [](slot_t) {});
                          }).name("FOREACH_ELEM");
        if (hasPrev) {
            step.succeed(prev);
        }
        prev    = step;
        hasPrev = true;
    }
    sf.join();
    frame->set(dataIndexOf(graph, nodeRef), NullSlot);
}

void TaskflowExecSchedPass::mark_unordered_foreach_arr(
    GCGraph *graph, gc_node_ref_t nodeRef, Frame *frame, tf::Subflow &sf) {
    TaskflowGcRegion region;
    const ElementCallSite site{graph, nodeRef, region.thread().callDepth};
    region.enter();
    const auto arrIdx  = dataIndexOf(graph, graph->normInputsOf(nodeRef).front());
    const auto funcIdx = dataIndexOf(graph, graph->withInputsOf(nodeRef).front());
    GCGraph *target    = makeHigherOrderCallSite(frame->get<Function *>(funcIdx)).runtimeGraph;
    const size_t n     = arrayAt(frame, arrIdx)->size();
    region.leave();

    for (size_t i = 0; i < n; ++i) {
        sf.emplace([this, site, i, frame, arrIdx, funcIdx, target]() {
              callClosure(
                  site,
                  target,
                  frame,
                  funcIdx,
                  1,
                  [&](std::span<slot_t> args) { args[0] = arrayAt(frame, arrIdx)->get<slot_t>(i); },
                  [](slot_t) {});
          }).name("FOREACH_ELEM");
    }
    sf.join();
    frame->set(dataIndexOf(graph, nodeRef), NullSlot);
}

template void TaskflowExecSchedPass::instantiate_graph_instance_generic<tf::Taskflow>(
    tf::Taskflow &flowLike, GCGraph *runtimeGraph, Frame *frame);
