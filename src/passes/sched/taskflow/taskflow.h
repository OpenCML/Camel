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

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/execute/pass/runtime_sched.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
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
        size_t chunkFrames{0};
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

class TaskflowExecSchedPass : public RuntimeGraphSchedulePass {
  public:
    /// `max_concurrent_tasks` = 0: the thread limit of the context (`-t`), else the number of
    /// hardware threads.
    TaskflowExecSchedPass(const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks = 0)
        : RuntimeGraphSchedulePass(ctx), executor_(workerCount(ctx, max_concurrent_tasks)) {}
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
    // Flows now running on the executor (nested ones included). While it is not zero, tasks
    // hold values that no frame holds yet (the element results of a map, a reduce's
    // accumulator, an operator's fresh result) and other workers keep running, so nothing
    // may be collected: evalGraphLinear polls its safepoints only when it is zero.
    std::atomic<size_t> runningFlows_{0};
    std::unordered_map<camel::runtime::GCGraph *, std::vector<camel::runtime::gc_node_ref_t>>
        linearTopoCache_;
    // Per graph and BRCH: where each arm starts executing in the linear order
    // (resolveRuntimeBranchArmEntry, shared with NodeVM and FastVM).
    std::unordered_map<
        camel::runtime::GCGraph *,
        std::unordered_map<
            camel::runtime::gc_node_ref_t, std::vector<camel::runtime::gc_node_ref_t>>>
        linearArmEntryCache_;
    // Both caches are filled on first use by whichever worker gets there first (the elements
    // of one `map` call the same graph at once).
    std::mutex linearCacheMutex_;

    slot_t evalGraphTF(camel::runtime::GCGraph *graph, ctx::Frame *frame);
    slot_t evalGraphLinear(camel::runtime::GCGraph *graph, ctx::Frame *frame);
    camel::runtime::gc_node_ref_t branchArmEntryForLinear(
        camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t brchRef, size_t armIndex,
        std::span<const camel::runtime::gc_node_ref_t> topoNodes);
    std::span<const camel::runtime::gc_node_ref_t>
    topoNodesForLinear(camel::runtime::GCGraph *graph);
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
};

class TfDumpPass : public GraphIRPass {
  public:
    TfDumpPass(const ctx::context_ptr_t &ctx) : GraphIRPass(ctx) {}
    ~TfDumpPass() override = default;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
