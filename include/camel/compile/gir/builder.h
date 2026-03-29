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
 * Created: Mar. 14, 2026
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "nodes.h"

#include <unordered_map>
#include <unordered_set>

namespace camel::compile::gir {

using type_vec_t = camel::core::type::type_vec_t;

enum class GraphImportMode {
    ReferenceOnly,
    CloneIntoDraft,
};

enum class InlineSyncPolicy {
    Auto,
    Force,
    Never,
};

struct InlineOptions {
    InlineSyncPolicy syncPolicy = InlineSyncPolicy::Auto;
    bool importReferencedGraphs = true;
};

struct InlineResult {
    Node *callNode  = nullptr;
    Node *valueExit = nullptr;
    Node *ctrlEntry = nullptr;
    std::vector<graph_ptr_t> importedSubgraphs;
    std::vector<graph_ptr_t> importedDependencies;
    bool insertedEntrySync = false;

    explicit operator bool() const { return valueExit != nullptr; }
};

// =============================================================================
// LayoutResult：computeLayout() 的纯计算输出。
//
// 包含图中所有节点的新 slot 编号映射、静态数据数组、以及各类签名类型，
// 由 applyLayout() 一次性写入 Graph。将 layout 计算与 Graph 变更解耦，
// 使得 sealGraph / exportRoot 等流程可以先计算再搬运。
// =============================================================================
struct LayoutResult {
    std::vector<std::pair<Node *, data_idx_t>> nodeIndices;
    static_slot_vec_t staticDataArr;
    TupleType *staticDataType  = nullptr;
    TupleType *runtimeDataType = nullptr;
    TupleType *closureType     = nullptr;
    size_t runtimeDataSize     = 1;
};

// =============================================================================
// GraphBuilder：Graph 的唯一底层构造/变换入口。
//
// 职责：
//   1. 创建新图 (createGraph)、克隆图 (cloneGraph)。
//   2. 在 draft（非 finalized）图上执行单图结构编辑：增删节点、端口、闭包、子图、依赖。
//   3. 封印 (sealGraph)：一次性执行布局计算（rearrange + finalized frame layout），然后将所有
//      节点的 draft 邻接 vectors 冻结到 arena 定长数组上。封印后图不再允许编辑。
//
// 设计约束：
//   - GraphBuilder 只负责当前 graph_ 这一张图的底层变换，不负责跨图事务。
//   - 构图期可变状态存放在 GraphBuilderState（定义于 graph.h）中。
//   - Graph 自身不暴露任何 mutable 接口，所有写操作均通过 GraphBuilder (friend)。
//   - sealGraph 是单向终结。要修改已封印的图必须先 cloneGraph 出 draft 副本。
//   - computeLayout() / applyLayout() 将布局计算与变更解耦。
// =============================================================================
class GraphBuilder {
  public:
    explicit GraphBuilder(Graph &graph) : graph_(&graph) {}
    explicit GraphBuilder(Graph *graph) : graph_(graph) {
        ASSERT(graph_ != nullptr, "Graph is null.");
    }
    explicit GraphBuilder(const graph_ptr_t &graph) : GraphBuilder(graph.get()) {}

    static graph_ptr_t createGraph(
        FunctionType *funcType, const graph_ptr_t &outer = nullptr, const std::string &name = "");
    static graph_ptr_t cloneGraph(const graph_ptr_t &graph);

    Graph &graph() const { return *graph_; }
    graph_ptr_t graphPtr() const { return graph().shared_from_this(); }

    data_idx_t addStaticSlot(slot_t slot) const;
    data_idx_t addStaticData(const data_ptr_t &data) const;
    data_idx_t addRuntimeData() const;
    void setStaticSlot(data_idx_t index, slot_t slot) const;
    void setStaticData(data_idx_t index, const data_ptr_t &data) const;

    Node *ownNode(Node *node) const;
    void addNode(Node *node) const;
    void eraseNode(Node *node) const;

    void addPort(Node *node, bool isWith = false) const;
    void addClosure(Node *node) const;
    void parametrizeClosure() const;
    void setOutput(Node *node) const;

    void addSubGraph(const graph_ptr_t &subGraph) const;
    void eraseSubGraph(const graph_ptr_t &subGraph) const;
    void addDependency(const graph_ptr_t &dep) const;
    void eraseDependency(const graph_ptr_t &dep) const;

    void touch() const {
        assertBuildable("mark dirty on");
        markMutated();
    }
    InlineResult inlineCallable(Node *node, const InlineOptions &options = {}) const;
    void pruneUnreachable() const;
    /// 封印此图：执行布局计算（rearrange + finalized frame layout），然后将所有节点的
    /// draft 邻接 vectors 搬迁到 arena 上的定长数组。具有 consume 语义：
    /// 调用后 builder 不再可用（graph_ 置 nullptr）。
    void sealGraph();
    /// 递归封印整棵图树（深度优先）。
    static void sealGraphRecursively(const graph_ptr_t &graph);

    /// 纯计算：遍历 graph 的 ports/closure/nodes，生成 slot 编号映射和签名类型，
    /// 不修改 Graph 任何成员。后续由 applyLayout() 一次性写入。
    static LayoutResult computeLayout(const Graph &graph);

    static void validateGraph(const Graph &graph);
    static void validateGraphRecursively(const graph_ptr_t &graph);

  private:
    /// 内部 seal 状态机：Draft -> Sealing -> Sealed（幂等）。
    /// 顺序固定为：rearrange -> debug promote -> freeze adjacency -> static pack
    /// -> installFinalFrameLayout -> releaseDraftRegion。
    void finalize() const;
    /// 将 computeLayout() 的结果写入 graph：更新每个节点的 dataIndex、
    /// 更新 Graph 的 signature / staticDataArr / finalized frame layout。
    static void applyLayout(Graph &graph, const LayoutResult &layout);
    void rearrange() const;
    void assertBuildable(const char *action) const;
    void markMutated() const;
    GraphBuilderState &state() const;
    static std::shared_ptr<GraphBuilderState> snapshotStateFromGraph(Graph &graph);
    void syncStateToGraph() const;

    Graph *graph_;
};

// =============================================================================
// GraphDraft：一次 rewrite 的工作态。
//
// GraphDraft 持有 source root 的 cloneGraph 工作副本（draft-owned），并负责：
//   1. 默认只把 root 视为 owned graph；子图/依赖图必须显式 import/clone。
//   2. 约束所有编辑只能发生在 draft-owned graph 上（assertDraftOwned）。
//   3. 编辑结束后由 seal() 一次性校验并封印整棵图树。
//   4. 为 GraphRewriteSession 提供 graph-in / graph-out 的内部编辑原语。
//
// 使用流程：
//   GraphDraft draft(frozenGraph);        // 1. 从 frozen 源图克隆出 draft
//   draft.eraseNode(n);                   // 2. 在 draft 上执行编辑
//   draft.replaceNode(old, new);
//   draft.seal();                         // 3. 封印：rearrange + finalized frame layout + freeze
//   return draft.root();                  // 4. 返回新的 sealed graph
//
// draft 内的编辑通过 GraphBuilder 的底层 API 完成，但 draft 只保证 root clone 默认可写；
// 任何对子图/依赖图的改写都必须先显式 import/clone，绝不隐式沿 FuncNode::bodyGraph()
// 或 subGraphs()/dependencies() 直接写入共享图。
// =============================================================================
class GraphDraft {
  public:
    explicit GraphDraft(const graph_ptr_t &source)
        : sourceRoot_(source), root_(GraphBuilder::cloneGraph(source)) {
        registerOwnedGraphs(root_);
        if (sourceRoot_) {
            draftBySource_[sourceRoot_.get()] = root_;
        }
    }

    graph_ptr_t sourceRoot() const { return sourceRoot_; }
    graph_ptr_t root() const { return root_; }
    graph_ptr_t canonicalGraph(const graph_ptr_t &graph) const {
        if (!graph) {
            return nullptr;
        }
        auto it = draftBySource_.find(graph.get());
        if (it != draftBySource_.end()) {
            return it->second;
        }
        return graph;
    }
    bool hasDraftFor(const Graph *graph) const {
        return graph != nullptr && draftBySource_.contains(graph);
    }

    bool owns(const Graph *graph) const {
        return graph != nullptr && ownedGraphs_.contains(const_cast<Graph *>(graph));
    }

    graph_ptr_t cloneIntoDraft(const graph_ptr_t &graph) {
        ASSERT(graph != nullptr, "Cannot clone null graph into draft.");
        auto existing = draftBySource_.find(graph.get());
        if (existing != draftBySource_.end()) {
            return existing->second;
        }
        graph_ptr_t cloned = GraphBuilder::cloneGraph(graph);
        registerOwnedGraphs(cloned);
        draftBySource_[graph.get()] = cloned;
        // 先修正“新克隆图内部”对已知 draft 的引用，再做“全 owned 图域”的 source->draft 替换。
        // 这个顺序保证不会遗漏类似 A(新克隆) -> B(已克隆) 的跨图边。
        retargetKnownDraftRefsInOwner(cloned.get());
        retargetOwnedGraphRefs(graph, cloned);
        return cloned;
    }
    graph_ptr_t importSubGraph(
        const graph_ptr_t &owner, const graph_ptr_t &subGraph,
        GraphImportMode mode = GraphImportMode::ReferenceOnly) {
        ASSERT(owner != nullptr && subGraph != nullptr, "Cannot import null subgraph.");
        assertDraftOwned(owner.get(), "import subgraph into");
        graph_ptr_t imported = resolveImportTarget(subGraph, mode);
        GraphBuilder(owner).addSubGraph(imported);
        markDirty(owner.get());
        return imported;
    }
    graph_ptr_t importDependency(
        const graph_ptr_t &owner, const graph_ptr_t &dep,
        GraphImportMode mode = GraphImportMode::ReferenceOnly) {
        ASSERT(owner != nullptr && dep != nullptr, "Cannot import null dependency.");
        assertDraftOwned(owner.get(), "import dependency into");
        graph_ptr_t imported = resolveImportTarget(dep, mode);
        GraphBuilder(owner).addDependency(imported);
        markDirty(owner.get());
        return imported;
    }
    void eraseNode(Node *node) {
        Graph *ownerGraph = &node->graph();
        assertDraftOwned(ownerGraph, "erase node from");
        GraphBuilder(ownerGraph).eraseNode(node);
        markDirty(ownerGraph);
    }
    bool replaceNode(Node *oldNode, Node *newNode) {
        Graph *ownerGraph = &oldNode->graph();
        assertDraftOwned(ownerGraph, "replace node on");
        assertDraftOwned(&newNode->graph(), "replace node on");
        bool changed = NodeMutation::replace(oldNode, newNode);
        GraphBuilder(ownerGraph).touch();
        markDirty(ownerGraph);
        eraseNode(oldNode);
        return changed;
    }
    bool replaceAllUses(Node *oldNode, Node *newNode) {
        Graph *ownerGraph = &oldNode->graph();
        assertDraftOwned(ownerGraph, "replace uses on");
        assertDraftOwned(&newNode->graph(), "replace uses on");
        bool changed = NodeMutation::replaceUses(oldNode, newNode);
        GraphBuilder(ownerGraph).touch();
        markDirty(ownerGraph);
        return changed;
    }
    void addDependency(const graph_ptr_t &owner, const graph_ptr_t &dep) {
        importDependency(owner, dep, GraphImportMode::ReferenceOnly);
    }
    void eraseDependency(const graph_ptr_t &owner, const graph_ptr_t &dep) {
        assertDraftOwned(owner.get(), "erase dependency from");
        GraphBuilder(owner).eraseDependency(dep);
        markDirty(owner.get());
    }
    void retargetDependency(
        const graph_ptr_t &owner, const graph_ptr_t &oldDep, const graph_ptr_t &newDep,
        GraphImportMode mode = GraphImportMode::ReferenceOnly) {
        ASSERT(
            owner != nullptr && oldDep != nullptr && newDep != nullptr,
            "Cannot retarget null dependency.");
        graph_ptr_t imported = importDependency(owner, newDep, mode);
        eraseDependency(owner, oldDep);
        markDirty(owner.get());
        (void)imported;
    }
    void eraseSubGraph(const graph_ptr_t &owner, const graph_ptr_t &subGraph) {
        assertDraftOwned(owner.get(), "erase subgraph from");
        GraphBuilder(owner).eraseSubGraph(subGraph);
        markDirty(owner.get());
    }
    void link(LinkType type, Node *from, Node *to) {
        assertDraftOwned(&from->graph(), "link from");
        assertDraftOwned(&to->graph(), "link to");
        NodeMutation::link(type, from, to);
        GraphBuilder(&from->graph()).touch();
        markDirty(&from->graph());
    }
    void replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
        assertDraftOwned(&owner->graph(), "replace input on");
        assertDraftOwned(&oldInput->graph(), "replace input on");
        assertDraftOwned(&newInput->graph(), "replace input on");
        NodeMutation::replaceInput(type, owner, oldInput, newInput);
        GraphBuilder(&owner->graph()).touch();
        markDirty(&owner->graph());
    }
    void replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
        assertDraftOwned(&owner->graph(), "replace output on");
        assertDraftOwned(&oldOutput->graph(), "replace output on");
        assertDraftOwned(&newOutput->graph(), "replace output on");
        NodeMutation::replaceOutput(type, owner, oldOutput, newOutput);
        GraphBuilder(&owner->graph()).touch();
        markDirty(&owner->graph());
    }
    InlineResult inlineCallable(Node *node, const InlineOptions &options = {}) {
        Graph *ownerGraph = &node->graph();
        assertDraftOwned(ownerGraph, "inline into");
        InlineResult result = GraphBuilder(ownerGraph).inlineCallable(node, options);
        if (result) {
            markDirty(ownerGraph);
        }
        return result;
    }
    void pruneUnreachable(const graph_ptr_t &graph) {
        ASSERT(graph != nullptr, "Cannot prune null graph.");
        assertDraftOwned(graph.get(), "prune graph");
        GraphBuilder(graph).pruneUnreachable();
        markDirty(graph.get());
    }
    void validate() const { GraphBuilder::validateGraphRecursively(root_); }
    /// 统一封印导出：先做结构校验，再递归对整棵子图树执行 sealGraph。
    /// sealGraph 内部调用 finalize（幂等），对已 finalized 的图自动跳过布局阶段，
    /// 但仍执行 freeze 邻接表搬迁。
    void seal() {
        validate();
        dirtyGraphs_.clear();
        GraphBuilder::sealGraphRecursively(root_);
    }

  private:
    void registerOwnedGraphs(const graph_ptr_t &graph) {
        if (!graph) {
            return;
        }
        ownedGraphs_.insert(graph.get());
    }
    graph_ptr_t resolveImportTarget(const graph_ptr_t &graph, GraphImportMode mode) {
        if (mode == GraphImportMode::CloneIntoDraft) {
            return cloneIntoDraft(graph);
        }
        auto existing = draftBySource_.find(graph.get());
        if (existing != draftBySource_.end()) {
            return existing->second;
        }
        return graph;
    }
    void retargetOwnedGraphRefs(const graph_ptr_t &sourceGraph, const graph_ptr_t &draftGraph) {
        ASSERT(sourceGraph != nullptr && draftGraph != nullptr, "Cannot retarget null graph refs.");
        Graph *sourceRaw = sourceGraph.get();
        Graph *draftRaw  = draftGraph.get();
        if (sourceRaw == draftRaw) {
            return;
        }

        for (Graph *ownerRaw : ownedGraphs_) {
            if (!ownerRaw) {
                continue;
            }
            bool changed = false;

            std::vector<graph_ptr_t> staleDeps;
            for (const auto &dep : ownerRaw->dependencies()) {
                if (dep.get() == sourceRaw) {
                    staleDeps.push_back(dep);
                }
            }
            if (!staleDeps.empty()) {
                GraphBuilder ownerBuilder(ownerRaw);
                for (const auto &dep : staleDeps) {
                    ownerBuilder.eraseDependency(dep);
                    changed = true;
                }
                if (!ownerRaw->dependencies().contains(draftGraph)) {
                    ownerBuilder.addDependency(draftGraph);
                    changed = true;
                }
            }

            bool subGraphReferenced = false;
            for (const auto &[_, subGraphs] : ownerRaw->subGraphs()) {
                if (subGraphs.contains(sourceGraph)) {
                    subGraphReferenced = true;
                    break;
                }
            }
            if (subGraphReferenced) {
                GraphBuilder ownerBuilder(ownerRaw);
                ownerBuilder.eraseSubGraph(sourceGraph);
                // `addSubGraph()` 会写入 graph->outer。仅当 owner 确实是 source 的词法外层时
                // 才允许重建子图关系，避免把“别名注册”误升级为真实嵌套关系。
                const auto sourceOuter         = sourceGraph->outer();
                const bool ownerIsLexicalOuter = sourceOuter && sourceOuter.get() == ownerRaw;
                if (ownerIsLexicalOuter && !draftGraph->name().empty()) {
                    ownerBuilder.addSubGraph(draftGraph);
                }
                changed = true;
            }

            for (Node *node : ownerRaw->nodes()) {
                if (node->type() == NodeType::FUNC) {
                    auto *func = tt::as_ptr<FuncNode>(node);
                    if (func && func->bodyGraph() == sourceRaw) {
                        func->setBodyGraph(draftRaw);
                        GraphBuilder ownerBuilder(ownerRaw);
                        ensureDependencyRegistration(ownerRaw, draftGraph, ownerBuilder);
                        changed = true;
                    }
                } else if (node->type() == NodeType::DATA) {
                    auto *dataNode = tt::as_ptr<DataNode>(node);
                    if (!dataNode ||
                        dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                        continue;
                    }
                    auto *funcObj =
                        camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
                    if (funcObj && funcObj->graph() == sourceRaw) {
                        funcObj->setGraph(draftRaw);
                        GraphBuilder ownerBuilder(ownerRaw);
                        ensureDependencyRegistration(ownerRaw, draftGraph, ownerBuilder);
                        changed = true;
                    }
                }
            }

            if (changed) {
                markDirty(ownerRaw);
            }
        }
    }
    void retargetKnownDraftRefsInOwner(Graph *ownerRaw) {
        if (!ownerRaw) {
            return;
        }
        bool changed = false;
        GraphBuilder ownerBuilder(ownerRaw);

        std::vector<std::pair<graph_ptr_t, graph_ptr_t>> depRetargets;
        for (const auto &dep : ownerRaw->dependencies()) {
            auto it = draftBySource_.find(dep.get());
            if (it == draftBySource_.end()) {
                continue;
            }
            const graph_ptr_t &draftDep = it->second;
            if (draftDep.get() == dep.get()) {
                continue;
            }
            depRetargets.emplace_back(dep, draftDep);
        }
        for (const auto &[oldDep, newDep] : depRetargets) {
            ownerBuilder.eraseDependency(oldDep);
            if (!ownerRaw->dependencies().contains(newDep)) {
                ownerBuilder.addDependency(newDep);
            }
            changed = true;
        }

        std::vector<std::pair<graph_ptr_t, graph_ptr_t>> subRetargets;
        for (const auto &[_, subGraphs] : ownerRaw->subGraphs()) {
            for (const auto &sub : subGraphs) {
                auto it = draftBySource_.find(sub.get());
                if (it == draftBySource_.end()) {
                    continue;
                }
                const graph_ptr_t &draftSub = it->second;
                if (draftSub.get() == sub.get()) {
                    continue;
                }
                subRetargets.emplace_back(sub, draftSub);
            }
        }
        for (const auto &[oldSub, newSub] : subRetargets) {
            ownerBuilder.eraseSubGraph(oldSub);
            const auto oldOuter            = oldSub->outer();
            const bool ownerIsLexicalOuter = oldOuter && oldOuter.get() == ownerRaw;
            if (ownerIsLexicalOuter && !newSub->name().empty()) {
                ownerBuilder.addSubGraph(newSub);
            }
            changed = true;
        }

        for (Node *node : ownerRaw->nodes()) {
            if (node->type() == NodeType::FUNC) {
                auto *func = tt::as_ptr<FuncNode>(node);
                if (!func || !func->bodyGraph()) {
                    continue;
                }
                auto it = draftBySource_.find(func->bodyGraph());
                if (it == draftBySource_.end()) {
                    continue;
                }
                if (it->second.get() != func->bodyGraph()) {
                    func->setBodyGraph(it->second.get());
                    ensureDependencyRegistration(ownerRaw, it->second, ownerBuilder);
                    changed = true;
                }
                continue;
            }
            if (node->type() != NodeType::DATA) {
                continue;
            }
            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (!dataNode ||
                dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                continue;
            }
            auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
            if (!funcObj || !funcObj->graph()) {
                continue;
            }
            auto it = draftBySource_.find(funcObj->graph());
            if (it == draftBySource_.end()) {
                continue;
            }
            if (it->second.get() != funcObj->graph()) {
                funcObj->setGraph(it->second.get());
                ensureDependencyRegistration(ownerRaw, it->second, ownerBuilder);
                changed = true;
            }
        }

        if (changed) {
            markDirty(ownerRaw);
        }
    }
    static void ensureDependencyRegistration(
        Graph *ownerRaw, const graph_ptr_t &dep, GraphBuilder &ownerBuilder) {
        if (!ownerRaw || !dep) {
            return;
        }
        if (ownerRaw == dep.get()) {
            // self dependency 在 GraphBuilder 里不会进入 dependencies 集合，
            // 但会设置 looped=true（递归图语义），这里必须显式触发一次。
            ownerBuilder.addDependency(dep);
            return;
        }
        if (!ownerRaw->dependencies().contains(dep)) {
            ownerBuilder.addDependency(dep);
        }
    }
    void assertDraftOwned(const Graph *graph, const char *action) const {
        ASSERT(
            owns(graph),
            std::format(
                "GraphDraft cannot {} non-owned graph '{}'. Import/clone it into the draft first.",
                action,
                graph ? graph->name() : "<null>"));
    }
    void markDirty(Graph *graph) { dirtyGraphs_.insert(graph); }

    graph_ptr_t sourceRoot_;
    graph_ptr_t root_;
    std::unordered_set<Graph *> ownedGraphs_;
    std::unordered_set<Graph *> dirtyGraphs_;
    std::unordered_map<const Graph *, graph_ptr_t> draftBySource_;
};

} // namespace camel::compile::gir
