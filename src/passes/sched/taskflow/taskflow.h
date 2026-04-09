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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/execute/pass/runtime_sched.h"

#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/taskflow.hpp>
#include <unordered_map>
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

    Frame *acquire(GIR::Graph *graph);
    Frame *acquire(camel::runtime::GCGraph *graph);
    void release(Frame *frame);
    void warmup(GIR::Graph *graph, size_t count);
    void warmup(camel::runtime::GCGraph *graph, size_t count);

  private:
    struct GraphArena {
        GIR::Graph *sourceGraph{nullptr};
        camel::runtime::GCGraph *runtimeGraph{nullptr};
        size_t frameSize{0};
        const camel::core::type::TupleType *runtimeDataType{nullptr};
        ::Tuple *staticArea{nullptr};
        size_t chunkFrames{0};
        std::mutex mutex;
        std::vector<Frame *> freeFrames;
        std::vector<std::byte *> chunks;
    };

    GraphArena &getOrCreateArena(GIR::Graph *graph);
    GraphArena &getOrCreateArena(camel::runtime::GCGraph *graph);
    void allocateChunk(GraphArena &arena, size_t minFrameCount);
    static uintptr_t arenaKey(GIR::Graph *graph);
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
    TaskflowExecSchedPass(const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks = 32)
        : RuntimeGraphSchedulePass(ctx), executor_(max_concurrent_tasks) {}
    virtual ~TaskflowExecSchedPass() = default;

    virtual GIR::graph_ptr_t apply(camel::runtime::GCGraph *graph, std::ostream &os) override;

    /// Build the task graph and dump it as DOT without executing it.
    void buildAndDump(GIR::Graph *graph, std::ostream &os);

    tf::Taskflow mainFlow_; // Main taskflow instance for the current execution.
    ctx::TaskflowFramePool framePool_{1 * camel::core::mm::MB};

    // Precomputed graph metadata: BRCH/JOIN structure, argument indices,
    // and port/closure layouts reused during execution.
    struct GraphInfos {
        struct BranchArmMeta {
            GIR::Node *head{nullptr};
            GIR::Node *tail{nullptr};
            std::vector<GIR::Node *> topoNodes;
            camel::runtime::gc_node_ref_t headRuntimeIndex = camel::runtime::kInvalidNodeRef;
            camel::runtime::gc_node_ref_t tailRuntimeIndex = camel::runtime::kInvalidNodeRef;
            std::vector<camel::runtime::gc_node_ref_t> topoNodeRuntimeIndices;
        };

        struct NodeExecMeta {
            std::vector<GIR::data_idx_t> normIndices;
            std::vector<GIR::data_idx_t> withIndices;
            camel::runtime::gc_node_ref_t runtimeNodeIndex = camel::runtime::kInvalidNodeRef;
        };

        GIR::Graph *graph{nullptr};
        camel::runtime::GCGraph *runtimeGraph{nullptr};
        std::unordered_map<GIR::Node *, GIR::Node *> joinToBrch;
        std::unordered_map<GIR::Node *, std::vector<BranchArmMeta>> branchArms;
        std::unordered_map<GIR::Node *, NodeExecMeta> nodeExecMeta;
        std::vector<NodeExecMeta> runtimeNodeExecMeta;
        std::vector<std::vector<BranchArmMeta>> runtimeBranchArms;
        std::vector<GIR::data_idx_t> normPortIndices;
        std::vector<GIR::data_idx_t> withPortIndices;
        std::vector<GIR::data_idx_t> closureIndices;
        std::unordered_set<camel::runtime::gc_node_ref_t> runtimeSkipNodeIndices;

        NodeExecMeta &getOrCreateNodeExecMeta(GIR::Node *node) { return nodeExecMeta[node]; }

        const NodeExecMeta &getNodeExecMeta(GIR::Node *node) const {
            auto it = nodeExecMeta.find(node);
            ASSERT(it != nodeExecMeta.end(), "Node exec meta not found.");
            return it->second;
        }

        const NodeExecMeta &getNodeExecMeta(camel::runtime::gc_node_ref_t runtimeNodeIndex) const {
            ASSERT(
                runtimeNodeIndex < runtimeNodeExecMeta.size(),
                "Runtime node exec meta index is out of range.");
            return runtimeNodeExecMeta[runtimeNodeIndex];
        }
    };

    struct GlobalBuildCtx {
        std::vector<std::unique_ptr<GraphInfos>> ownedGraphInfos;
        std::unordered_map<GIR::Graph *, GraphInfos *> graphInfoMap;
        std::unordered_map<camel::runtime::GCGraph *, GraphInfos *> runtimeGraphInfoMap;
        std::unordered_set<GIR::Node *> skipNodes;

        GraphInfos &getOrCreateGraphInfos(GIR::Graph *graph) {
            auto it = graphInfoMap.find(graph);
            if (it == graphInfoMap.end()) {
                auto tasks   = std::make_unique<GraphInfos>();
                tasks->graph = graph;
                auto &ref    = *tasks;
                ownedGraphInfos.push_back(std::move(tasks));
                graphInfoMap[graph] = &ref;
                return ref;
            }
            return *it->second;
        }

        GraphInfos &
        getOrCreateGraphInfos(camel::runtime::GCGraph *runtimeGraph, GIR::Graph *graph) {
            ASSERT(
                runtimeGraph != nullptr,
                "Runtime graph info creation requires a runtime graph.");
            auto it = runtimeGraphInfoMap.find(runtimeGraph);
            if (it != runtimeGraphInfoMap.end()) {
                if (graph && it->second->graph == nullptr) {
                    it->second->graph   = graph;
                    graphInfoMap[graph] = it->second;
                }
                return *it->second;
            }

            auto tasks          = std::make_unique<GraphInfos>();
            tasks->graph        = graph;
            tasks->runtimeGraph = runtimeGraph;
            auto &ref           = *tasks;
            ownedGraphInfos.push_back(std::move(tasks));
            runtimeGraphInfoMap[runtimeGraph] = &ref;
            if (graph) {
                graphInfoMap[graph] = &ref;
            }
            return ref;
        }

        GraphInfos &getGraphInfos(GIR::Graph *graph) {
            auto it = graphInfoMap.find(graph);
            ASSERT(it != graphInfoMap.end(), "Graph tasks not found.");
            return *it->second;
        }

        const GraphInfos &getGraphInfos(GIR::Graph *graph) const {
            auto it = graphInfoMap.find(graph);
            ASSERT(it != graphInfoMap.end(), "Graph tasks not found.");
            return *it->second;
        }

        GraphInfos &getGraphInfos(camel::runtime::GCGraph *graph) {
            auto it = runtimeGraphInfoMap.find(graph);
            ASSERT(it != runtimeGraphInfoMap.end(), "Runtime graph tasks not found.");
            return *it->second;
        }

        const GraphInfos &getGraphInfos(camel::runtime::GCGraph *graph) const {
            auto it = runtimeGraphInfoMap.find(graph);
            ASSERT(it != runtimeGraphInfoMap.end(), "Runtime graph tasks not found.");
            return *it->second;
        }
    } globalBuildCtx_;

  private:
    tf::Executor executor_;

    // Build and run the taskflow for one graph instance, then return the graph result.
    slot_t evalGraphTF(camel::runtime::GCGraph *graph, ctx::Frame *frame);

    // Build metadata for all reachable graphs.
    void buildGraphsInfo(GIR::Graph *rootGraph);
    void buildGraphsInfo(camel::runtime::GCGraph *rootGraph);

    // Unified subgraph execution entry. Ownership of the prepared frame stays here.
    slot_t runPreparedSubgraph(tf::Subflow &sf, camel::runtime::GCGraph *graph, ctx::Frame *frame);

    // Fill a callee frame using precomputed call-site layout metadata.
    ctx::Frame *acquirePreparedNodeCallFrame(
        camel::runtime::GCGraph *targetGraph, GIR::Node *callNode, ctx::Frame *sourceFrame);
    ctx::Frame *acquirePreparedRuntimeCallFrame(
        camel::runtime::GCGraph *targetGraph, camel::runtime::gc_node_ref_t callNodeRuntimeIndex,
        ctx::Frame *sourceFrame);
    ctx::Frame *acquirePreparedClosureCallFrame(
        camel::runtime::GCGraph *targetGraph, ::Tuple *closure, std::span<const slot_t> args);

    // Reuse precomputed argument indices to avoid rebuilding temporary vectors.
    slot_t executePreparedOperator(GIR::Node *n, ctx::Frame *frame);
    slot_t executePreparedNode(GIR::Node *n, ctx::Frame *frame, tf::Subflow &sf);
    slot_t executePreparedBranchArm(
        GIR::BrchNode *brch, size_t armIndex, ctx::Frame *frame, tf::Subflow &sf);

    // Generic graph instantiation helpers for both Taskflow and Subflow contexts.
    template <typename FlowT>
    void instantiate_graph_instance_generic(
        FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, ctx::Frame *frame);

    template <typename FlowT>
    void instantiate_graph_instance_generic(FlowT &flowLike, GIR::Graph *graph, ctx::Frame *frame);

    template <typename FlowT>
    void buildRuntimeNodeTasks(
        FlowT &flowLike, camel::runtime::GCGraph *graph, ctx::Frame *frame,
        std::vector<tf::Task> &taskMap, std::vector<uint8_t> &taskBuilt);

    template <typename FlowT>
    void connectRuntimeDependencies(
        FlowT &flowLike, camel::runtime::GCGraph *graph, ctx::Frame *frame,
        std::vector<tf::Task> &taskMap, std::vector<uint8_t> &taskBuilt);

    // Per-node task builders. Each node kind gets its own construction routine.
    template <typename FlowT>
    tf::Task buildDataTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildPortTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCopyTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCastTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildFillTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildAccsTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildFuncTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildCallTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    template <typename FlowT>
    tf::Task buildOperTask(FlowT &flowLike, GIR::Node *n, ctx::Frame *frame);

    // Build the BRCH/JOIN region as a structured set of selector and join tasks.
    template <typename FlowT>
    void buildBranchJoinRegion(
        FlowT &flowLike, ctx::Frame *frame, std::unordered_map<GIR::Node *, tf::Task> &taskMap,
        GIR::Node *brch);

    template <typename FlowT>
    void buildRuntimeBranchJoinRegion(
        FlowT &flowLike, camel::runtime::GCGraph *runtimeGraph, ctx::Frame *frame,
        std::vector<tf::Task> &taskMap, std::vector<uint8_t> &taskBuilt,
        camel::runtime::gc_node_ref_t brchRuntimeIndex, GIR::Node *brch);

    // Build ordinary node tasks outside BRCH/JOIN regions, including ports.
    template <typename FlowT>
    void buildNormalNodeTasks(
        FlowT &flowLike, GIR::Graph *graph, ctx::Frame *frame,
        std::unordered_map<GIR::Node *, tf::Task> &taskMap);

    // Connect data/control dependencies between already-built tasks.
    template <typename FlowT>
    void connectDependencies(
        FlowT &flowLike, GIR::Graph *graph, ctx::Frame *frame,
        std::unordered_map<GIR::Node *, tf::Task> &taskMap);

    // Mark operators that expand into element-wise parallel subflows.
    void mark_map_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_apply_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_filter_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_reduce_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_foreach_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_unordered_foreach_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
    void mark_unordered_reduce_arr(GIR::Node *node, ctx::Frame *frame, tf::Subflow &sf);
};

/// Dump the constructed Taskflow graph as GraphViz DOT without executing it.
class TfDumpPass : public GraphIRPass {
  public:
    TfDumpPass(const ctx::context_ptr_t &ctx) : GraphIRPass(ctx) {}
    virtual ~TfDumpPass() = default;
    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;
};
