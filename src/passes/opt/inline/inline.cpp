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
 * Created: Oct. 25, 2025
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "inline.h"

#include "camel/compile/gir/reachable.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/rewrite.h"
#include "camel/utils/log.h"

using namespace std;
using namespace GIR;
using namespace camel::core::error;

namespace {

constexpr size_t kInlineTargetBudgetPerScc = 20000;
constexpr size_t kInlineApplyBudgetPerScc  = 20000;
constexpr size_t kNodeDeltaBudgetPerScc    = 50000;
// Inline budgets are correctness tripwires in debug builds and safety valves in release
// builds. Debug should fail fast when the rewrite unexpectedly explodes, while release should
// stop expanding the current SCC instead of running into pathological compile time or IR growth.

enum class UseKind {
    FuncBody,
    DataFunction,
};

struct StaticFunctionValueInfo {
    graph_ptr_t bodyGraph;
    std::vector<Node *> closureArgs;
};

using SpecializationCache = std::unordered_map<std::string, graph_ptr_t>;

struct GraphUseSite {
    graph_ptr_t owner;
    Node *node = nullptr;
    UseKind kind{};
};

using GraphUseIndex = std::unordered_map<const Graph *, std::vector<GraphUseSite>>;

struct InlineTarget {
    graph_ptr_t owner;
    Node *path             = nullptr;
    const Graph *bodyGraph = nullptr;
    bool isBranchArmHead   = false;
};

struct AppliedInlineRecord {
    graph_ptr_t owner;
    const Graph *targetBodyGraph = nullptr;
    std::string erasedPathText;
    bool isBranchArmHead = false;
};

struct SccGraphPlan {
    std::vector<std::vector<const Graph *>> components;
    std::unordered_map<const Graph *, size_t> componentOf;
    std::vector<std::vector<size_t>> dag;
    std::vector<size_t> processOrder;
};

node_vec_t collectUseCarrierNodes(const graph_ptr_t &graph) {
    node_vec_t carriers;
    carriers.reserve(graph->nodes().size() + 1);
    for (Node *node : graph->nodes()) {
        carriers.push_back(node);
    }
    Node *exitAnchor = graph->exitNode();
    if (std::ranges::find(carriers, exitAnchor) == carriers.end()) {
        carriers.push_back(exitAnchor);
    }
    return carriers;
}

std::vector<const Graph *> collectReferencedGraphs(const graph_ptr_t &graph) {
    std::vector<const Graph *> refs;
    refs.reserve(graph->dependencies().size() + graph->subGraphs().size() + graph->nodes().size());
    for (const auto &[_, subGraphs] : graph->subGraphs()) {
        for (const auto &sub : subGraphs) {
            refs.push_back(sub.get());
        }
    }
    for (const auto &dep : graph->dependencies()) {
        refs.push_back(dep.get());
    }
    for (Node *node : collectUseCarrierNodes(graph)) {
        if (node->type() == NodeType::FUNC) {
            auto *func = tt::as_ptr<FuncNode>(node);
            if (func->bodyGraph()) {
                refs.push_back(func->bodyGraph());
            }
            continue;
        }
        if (node->type() != NodeType::DATA) {
            continue;
        }
        auto *dataNode = tt::as_ptr<DataNode>(node);
        if (dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
            continue;
        }
        auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
        if (funcObj && funcObj->sourceGraph()) {
            refs.push_back(funcObj->sourceGraph());
        }
    }
    return refs;
}

graph_ptr_t findGraphHandleInOwnerRegistry(const graph_ptr_t &owner, const Graph *graph) {
    if (!owner || !graph) {
        return nullptr;
    }
    if (owner.get() == graph) {
        return owner;
    }
    if (auto outer = owner->outer(); outer && outer.get() == graph) {
        return outer;
    }
    for (const auto &dep : owner->dependencies()) {
        if (dep.get() == graph) {
            return dep;
        }
    }
    for (const auto &[_, subGraphs] : owner->subGraphs()) {
        for (const auto &sub : subGraphs) {
            if (sub.get() == graph) {
                return sub;
            }
        }
    }
    return nullptr;
}

graph_ptr_t resolveGraphHandle(
    camel::runtime::RuntimeSourceGraphEditSession &session, const graph_ptr_t &owner,
    const Graph *graph) {
    if (!graph) {
        return nullptr;
    }
    if (graph_ptr_t local = findGraphHandleInOwnerRegistry(owner, graph)) {
        return local;
    }
    try {
        graph_ptr_t sourceGraph = std::const_pointer_cast<Graph>(graph->shared_from_this());
        return session.ownsGraph(graph) ? session.canonicalGraph(sourceGraph) : sourceGraph;
    } catch (const std::bad_weak_ptr &) {
        return nullptr;
    }
}

Node *cloneStaticValueNodeIntoGraph(
    camel::runtime::RuntimeSourceGraphEditSession &session, const graph_ptr_t &owner, Node *node,
    std::unordered_map<const Node *, Node *> &cache);

std::optional<StaticFunctionValueInfo> tryResolveStaticFunctionValue(
    camel::runtime::RuntimeSourceGraphEditSession &session, const graph_ptr_t &owner, Node *node,
    std::unordered_map<const Node *, Node *> &cache) {
    if (!node || node->dataType()->code() != camel::core::type::TypeCode::Function) {
        return std::nullopt;
    }

    auto resolveFromFunctionObject =
        [&](::Function *funcObj) -> std::optional<StaticFunctionValueInfo> {
        if (!funcObj || !funcObj->sourceGraph()) {
            return std::nullopt;
        }
        graph_ptr_t sourceGraphPtr = resolveGraphHandle(session, owner, funcObj->sourceGraph());
        if (!sourceGraphPtr) {
            return std::nullopt;
        }
        graph_ptr_t canonicalGraph = session.ownsGraph(sourceGraphPtr.get())
                                         ? sourceGraphPtr
                                         : (session.hasDraftGraph(funcObj->sourceGraph())
                                                ? sourceGraphPtr
                                                : session.importDependency(
                                                      owner,
                                                      sourceGraphPtr,
                                                      GraphImportMode::CloneIntoDraft));

        StaticFunctionValueInfo info{.bodyGraph = canonicalGraph, .closureArgs = {}};
        if (auto *closure = funcObj->tuple()) {
            const auto *closureType = funcObj->tupleType();
            info.closureArgs.reserve(closureType->size());
            for (size_t i = 0; i < closureType->size(); ++i) {
                info.closureArgs.push_back(session.materializeStaticValue(
                    owner,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i)));
            }
        }
        return info;
    };

    if (node->type() == NodeType::DATA) {
        auto *dataNode = tt::as_ptr<DataNode>(node);
        return resolveFromFunctionObject(
            camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot()));
    }

    if (node->type() != NodeType::FILL) {
        return std::nullopt;
    }

    auto normInputs = node->normInputs();
    if (normInputs.empty()) {
        return std::nullopt;
    }
    auto baseInfo = tryResolveStaticFunctionValue(session, owner, normInputs.front(), cache);
    if (!baseInfo.has_value()) {
        return std::nullopt;
    }

    StaticFunctionValueInfo info = std::move(*baseInfo);
    auto withInputs              = node->withInputs();
    if (withInputs.size() > info.closureArgs.size()) {
        return std::nullopt;
    }
    for (size_t i = 0; i < withInputs.size(); ++i) {
        Node *clonedArg = cloneStaticValueNodeIntoGraph(session, owner, withInputs[i], cache);
        if (!clonedArg) {
            return std::nullopt;
        }
        info.closureArgs[i] = clonedArg;
    }
    return info;
}

Node *cloneStaticValueNodeIntoGraph(
    camel::runtime::RuntimeSourceGraphEditSession &session, const graph_ptr_t &owner, Node *node,
    std::unordered_map<const Node *, Node *> &cache) {
    if (!node) {
        return nullptr;
    }
    if (&node->graph() == owner.get()) {
        return node;
    }
    if (auto it = cache.find(node); it != cache.end()) {
        return it->second;
    }

    if (node->type() == NodeType::DATA) {
        auto *dataNode = tt::as_ptr<DataNode>(node);
        Node *materialized =
            session.materializeStaticValue(owner, dataNode->dataSlot(), dataNode->dataType());
        cache.emplace(node, materialized);
        return materialized;
    }

    if (node->type() != NodeType::FILL) {
        return nullptr;
    }

    auto normInputs = node->normInputs();
    if (normInputs.empty()) {
        return nullptr;
    }
    Node *base = cloneStaticValueNodeIntoGraph(session, owner, normInputs.front(), cache);
    if (!base) {
        return nullptr;
    }
    auto *fill = FillNode::create(*owner, node->dataType());
    session.link(LinkType::Norm, base, fill);
    for (Node *with : node->withInputs()) {
        Node *cloned = cloneStaticValueNodeIntoGraph(session, owner, with, cache);
        if (!cloned) {
            return nullptr;
        }
        session.link(LinkType::With, cloned, fill);
    }
    cache.emplace(node, fill);
    return fill;
}

std::vector<Node *> orderedCallableInputs(Node *callable) {
    std::vector<Node *> inputs;
    inputs.reserve(callable->normInputs().size() + callable->withInputs().size());
    inputs.insert(inputs.end(), callable->normInputs().begin(), callable->normInputs().end());
    inputs.insert(inputs.end(), callable->withInputs().begin(), callable->withInputs().end());
    return inputs;
}

bool hasObservableUses(Node *node) {
    return node && (!node->normOutputs().empty() || !node->withOutputs().empty() ||
                    !node->ctrlOutputs().empty());
}

bool drivesIndirectCallCallee(Node *root) {
    if (!root || root->dataType()->code() != camel::core::type::TypeCode::Function) {
        return false;
    }
    std::vector<Node *> stack{root};
    std::unordered_set<Node *> visited;
    while (!stack.empty()) {
        Node *curr = stack.back();
        stack.pop_back();
        if (!curr || !visited.insert(curr).second) {
            continue;
        }
        for (Node *user : curr->withOutputs()) {
            if (!user) {
                continue;
            }
            if (user->type() == NodeType::CALL &&
                tt::as_ptr<CallNode>(user)->calleeInput() == curr) {
                return true;
            }
            if (user->type() == NodeType::FILL) {
                stack.push_back(user);
            }
        }
        for (Node *user : curr->normOutputs()) {
            if (!user) {
                continue;
            }
            if (user->type() == NodeType::FUNC) {
                // Forwarding a static function through a direct FUNC boundary is already enough
                // to justify specialization. The callee graph may not issue CALL directly at this
                // graph level, but once specialized the static function can continue propagating
                // and eventually devirtualize deeper indirect calls.
                return true;
            }
            if (user->type() == NodeType::FILL) {
                auto normInputs = user->normInputs();
                if (!normInputs.empty() && normInputs.front() == curr) {
                    stack.push_back(user);
                }
            }
        }
    }
    return false;
}

std::string encodeStaticSlotKey(slot_t slot, Type *type);

std::string encodeStaticFunctionNodeKey(Node *node) {
    if (!node) {
        return "null";
    }
    if (node->type() == NodeType::DATA) {
        auto *dataNode = tt::as_ptr<DataNode>(node);
        return encodeStaticSlotKey(dataNode->dataSlot(), dataNode->dataType());
    }
    if (node->type() == NodeType::FILL) {
        std::string key = "fill(";
        auto normInputs = node->normInputs();
        key += normInputs.empty() ? "null" : encodeStaticFunctionNodeKey(normInputs.front());
        for (Node *with : node->withInputs()) {
            key += ",";
            key += encodeStaticFunctionNodeKey(with);
        }
        key += ")";
        return key;
    }
    return std::format("node:{:p}", static_cast<void *>(node));
}

std::string encodeFunctionObjectKey(::Function *funcObj) {
    if (!funcObj || !funcObj->sourceGraph()) {
        return "func:null";
    }
    std::string key = std::format("func:{}(", funcObj->sourceGraph()->stableId());
    if (auto *closure = funcObj->tuple()) {
        const auto *tupleType = funcObj->tupleType();
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (i != 0) {
                key += ",";
            }
            key += encodeStaticSlotKey(closure->get<slot_t>(i), tupleType->typeAt(i));
        }
    }
    key += ")";
    return key;
}

std::string encodeStaticSlotKey(slot_t slot, Type *type) {
    if (!type) {
        return std::format("slot:{}:null", slot);
    }
    if (type->code() == camel::core::type::TypeCode::Function) {
        return encodeFunctionObjectKey(camel::core::rtdata::fromSlot<::Function *>(slot));
    }
    return std::format("slot:{}:{}", static_cast<int>(type->code()), slot);
}

std::string buildSpecializationKey(
    const Graph *targetGraph, const std::vector<std::pair<size_t, Node *>> &bindings) {
    std::string key = std::format("graph:{:p}", static_cast<const void *>(targetGraph));
    for (const auto &[index, arg] : bindings) {
        key += std::format("|{}={}", index, encodeStaticFunctionNodeKey(arg));
    }
    return key;
}

bool specializeDirectFuncTargets(
    camel::runtime::RuntimeSourceGraphEditSession &session, std::vector<graph_ptr_t> &workGraphs) {
    bool changed = false;
    SpecializationCache specializationCache;
    std::unordered_set<const Graph *> knownGraphs;
    for (const auto &graph : workGraphs) {
        knownGraphs.insert(graph.get());
    }

    bool localChanged = true;
    while (localChanged) {
        localChanged        = false;
        const auto snapshot = workGraphs;
        for (const auto &owner : snapshot) {
            std::vector<Node *> funcNodes;
            for (Node *node : owner->nodes()) {
                if (node && node->type() == NodeType::FUNC) {
                    funcNodes.push_back(node);
                }
            }
            for (Node *node : funcNodes) {
                auto *funcNode = tt::as_ptr<FuncNode>(node);
                if (!funcNode || !funcNode->bodyGraph()) {
                    continue;
                }

                graph_ptr_t targetSource =
                    resolveGraphHandle(session, owner, funcNode->bodyGraph());
                if (!targetSource) {
                    continue;
                }
                graph_ptr_t targetDraft = session.ownsGraph(targetSource.get())
                                              ? targetSource
                                              : (session.hasDraftGraph(funcNode->bodyGraph())
                                                     ? targetSource
                                                     : session.importDependency(
                                                           owner,
                                                           targetSource,
                                                           GraphImportMode::CloneIntoDraft));

                std::vector<Node *> allArgs     = orderedCallableInputs(funcNode);
                std::vector<Node *> targetPorts = targetDraft->ports();
                targetPorts.insert(
                    targetPorts.end(),
                    targetDraft->closure().begin(),
                    targetDraft->closure().end());
                if (allArgs.size() < targetPorts.size()) {
                    continue;
                }

                std::unordered_map<const Node *, Node *> resolveCache;
                std::vector<std::pair<size_t, Node *>> staticBindings;
                for (size_t i = 0; i < targetPorts.size(); ++i) {
                    if (!hasObservableUses(targetPorts[i]) ||
                        !drivesIndirectCallCallee(targetPorts[i])) {
                        continue;
                    }
                    auto binding =
                        tryResolveStaticFunctionValue(session, owner, allArgs[i], resolveCache);
                    if (!binding.has_value()) {
                        continue;
                    }
                    staticBindings.emplace_back(i, allArgs[i]);
                }
                if (staticBindings.empty()) {
                    continue;
                }

                const std::string specializationKey =
                    buildSpecializationKey(targetDraft.get(), staticBindings);
                if (auto cachedIt = specializationCache.find(specializationKey);
                    cachedIt != specializationCache.end()) {
                    if (funcNode->bodyGraph() != cachedIt->second.get()) {
                        session.addDependency(owner, cachedIt->second);
                        detail::NodeMutation::setBodyGraph(funcNode, cachedIt->second.get());
                        session.markChanged();
                        changed      = true;
                        localChanged = true;
                    }
                    continue;
                }

                std::unordered_map<const Node *, Node *> nodeMap;
                graph_ptr_t specialized = GraphBuilder::cloneGraph(targetDraft, &nodeMap);
                session.adoptOwnedGraph(specialized);
                std::unordered_map<const Node *, Node *> staticCloneCache;
                for (const auto &[portIndex, staticArg] : staticBindings) {
                    Node *targetPort = targetPorts[portIndex];
                    auto it          = nodeMap.find(targetPort);
                    if (it == nodeMap.end()) {
                        continue;
                    }
                    Node *clonedPort   = it->second;
                    Node *clonedStatic = cloneStaticValueNodeIntoGraph(
                        session,
                        specialized,
                        staticArg,
                        staticCloneCache);
                    if (!clonedStatic) {
                        continue;
                    }
                    session.replaceAllUses(clonedPort, clonedStatic);
                }

                GraphBuilder::validateGraph(*specialized);
                specializationCache.emplace(specializationKey, specialized);
                session.addDependency(owner, specialized);
                detail::NodeMutation::setBodyGraph(funcNode, specialized.get());
                session.markChanged();
                if (knownGraphs.insert(specialized.get()).second) {
                    workGraphs.push_back(specialized);
                }
                changed      = true;
                localChanged = true;
            }
        }
    }

    return changed;
}

bool devirtualizeStaticCalls(
    camel::runtime::RuntimeSourceGraphEditSession &session,
    const std::vector<graph_ptr_t> &workGraphs) {
    bool changed = false;
    std::unordered_map<const Graph *, graph_ptr_t> parameterizedClosureGraphs;
    for (const auto &owner : workGraphs) {
        std::vector<Node *> callNodes;
        for (Node *node : owner->nodes()) {
            if (node && node->type() == NodeType::CALL) {
                callNodes.push_back(node);
            }
        }

        for (Node *node : callNodes) {
            auto *callNode = tt::as_ptr<CallNode>(node);
            if (!callNode || !callNode->hasCallee()) {
                continue;
            }

            std::unordered_map<const Node *, Node *> staticCloneCache;
            auto calleeInfo = tryResolveStaticFunctionValue(
                session,
                owner,
                callNode->calleeInput(),
                staticCloneCache);
            if (!calleeInfo.has_value() || !calleeInfo->bodyGraph) {
                continue;
            }

            graph_ptr_t directBody = calleeInfo->bodyGraph;
            if (!calleeInfo->closureArgs.empty()) {
                auto cacheIt = parameterizedClosureGraphs.find(directBody.get());
                if (cacheIt != parameterizedClosureGraphs.end()) {
                    directBody = cacheIt->second;
                } else {
                    graph_ptr_t parameterized = GraphBuilder::cloneGraph(directBody);
                    session.adoptOwnedGraph(parameterized);
                    GraphBuilder(parameterized).parametrizeClosure();
                    GraphBuilder::validateGraph(*parameterized);
                    parameterizedClosureGraphs.emplace(directBody.get(), parameterized);
                    if (std::ranges::find_if(workGraphs, [&](const graph_ptr_t &g) {
                            return g.get() == parameterized.get();
                        }) == workGraphs.end()) {
                        // no-op: caller-owned vector is const here; the clone is still reachable
                        // via the rewritten callsite dependency and session graph registry
                    }
                    directBody = parameterized;
                }
            }

            session.addDependency(owner, directBody);
            auto *funcNode = tt::as_ptr<FuncNode>(FuncNode::create(*owner, directBody));
            for (Node *pred : callNode->ctrlInputs()) {
                session.link(LinkType::Ctrl, pred, funcNode);
            }
            for (Node *arg : callNode->normArgs()) {
                session.link(LinkType::Norm, arg, funcNode);
            }
            for (size_t i = 0; i < callNode->withArgCount(); ++i) {
                session.link(LinkType::With, callNode->withArg(i), funcNode);
            }
            for (Node *closureArg : calleeInfo->closureArgs) {
                session.link(LinkType::With, closureArg, funcNode);
            }
            session.replaceAllUses(callNode, funcNode);
            session.eraseNode(callNode);
            changed = true;
        }
    }
    return changed;
}

bool ensureFuncBodyDependencies(
    camel::runtime::RuntimeSourceGraphEditSession &session,
    const std::vector<graph_ptr_t> &workGraphs) {
    bool changed = false;
    for (const auto &owner : workGraphs) {
        for (Node *node : collectUseCarrierNodes(owner)) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            if (!funcNode || !funcNode->bodyGraph() || funcNode->bodyGraph() == owner.get()) {
                continue;
            }
            graph_ptr_t target = findGraphHandleInOwnerRegistry(owner, funcNode->bodyGraph());
            if (!target) {
                try {
                    target = funcNode->bodyGraph()->shared_from_this();
                } catch (const std::bad_weak_ptr &) {
                    continue;
                }
            }
            if (!target || owner->dependencies().contains(target)) {
                continue;
            }
            session.addDependency(owner, target);
            changed = true;
        }
    }
    return changed;
}

GraphUseIndex buildUseIndex(const std::vector<graph_ptr_t> &draftGraphs) {
    // Session-level use-site index:
    // calleeGraph -> [(ownerGraph, useNode, kind)].
    // This is the only batch entry point for legacy->draft redirection and
    // dependency synchronization.
    GraphUseIndex index;
    for (const auto &owner : draftGraphs) {
        for (Node *node : collectUseCarrierNodes(owner)) {
            if (node->type() == NodeType::FUNC) {
                auto *func = tt::as_ptr<FuncNode>(node);
                if (func->bodyGraph()) {
                    index[func->bodyGraph()].push_back(
                        GraphUseSite{
                            .owner = owner,
                            .node  = node,
                            .kind  = UseKind::FuncBody,
                        });
                }
                continue;
            }
            if (node->type() != NodeType::DATA) {
                continue;
            }
            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (dataNode->dataType()->code() != camel::core::type::TypeCode::Function) {
                continue;
            }
            auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(dataNode->dataSlot());
            if (!funcObj || !funcObj->sourceGraph()) {
                continue;
            }
            index[funcObj->sourceGraph()].push_back(
                GraphUseSite{
                    .owner = owner,
                    .node  = node,
                    .kind  = UseKind::DataFunction,
                });
        }
    }
    return index;
}

void assertNoLegacyGraphRefs(
    const std::vector<graph_ptr_t> &draftGraphs,
    const std::unordered_set<const Graph *> &legacySources) {
    for (const auto &owner : draftGraphs) {
        for (Node *node : collectUseCarrierNodes(owner)) {
            if (node->type() == NodeType::FUNC) {
                auto *func = tt::as_ptr<FuncNode>(node);
                if (!func->bodyGraph()) {
                    continue;
                }
                ASSERT(
                    !legacySources.contains(func->bodyGraph()),
                    std::format(
                        "Legacy FUNC.bodyGraph '{}' still referenced in draft graph '{}'.",
                        func->bodyGraph()->name(),
                        owner->name()));
            } else if (node->type() == NodeType::DATA) {
                auto *data = tt::as_ptr<DataNode>(node);
                if (data->dataType()->code() != camel::core::type::TypeCode::Function) {
                    continue;
                }
                auto *funcObj = camel::core::rtdata::fromSlot<::Function *>(data->dataSlot());
                if (!funcObj || !funcObj->sourceGraph()) {
                    continue;
                }
                ASSERT(
                    !legacySources.contains(funcObj->sourceGraph()),
                    std::format(
                        "Legacy DATA(Function).graph '{}' still referenced in draft graph '{}'.",
                        funcObj->sourceGraph()->name(),
                        owner->name()));
            }
        }
    }
}

void assertNoAnonymousFuncTargets(const std::vector<graph_ptr_t> &draftGraphs) {
    for (const auto &owner : draftGraphs) {
        for (Node *node : collectUseCarrierNodes(owner)) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *func       = tt::as_ptr<FuncNode>(node);
            Graph *bodyGraph = func ? func->bodyGraph() : nullptr;
            if (!bodyGraph) {
                throw std::runtime_error(
                    std::format(
                        "FUNC node '{}' in graph '{}' refers to a null body graph.",
                        node->toString(),
                        owner->name()));
            }
            if (bodyGraph != owner.get() && bodyGraph->name().empty()) {
                throw std::runtime_error(
                    std::format(
                        "FUNC node '{}' in graph '{}' refers to anonymous body graph {:p}.",
                        node->toString(),
                        owner->name(),
                        static_cast<void *>(bodyGraph)));
            }
        }
    }
}

void assertStaticValueGraphsWellOwned(
    const graph_ptr_t &owner, slot_t slot, Type *type,
    std::unordered_set<const camel::core::rtdata::Object *> &visited) {
    if (!type || !type->isGCTraced() || slot == NullSlot) {
        return;
    }

    camel::core::rtdata::Object *object =
        camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(slot);
    if (!object || !visited.insert(object).second) {
        return;
    }

    switch (type->code()) {
    case camel::core::type::TypeCode::Function: {
        auto *funcObj      = camel::core::rtdata::fromSlot<::Function *>(slot);
        Graph *sourceGraph = funcObj ? funcObj->sourceGraph() : nullptr;
        if (!sourceGraph) {
            throw std::runtime_error(
                std::format(
                    "Static function value in graph '{}' refers to a null source graph.",
                    owner ? owner->name() : "<null>"));
        }
        if (sourceGraph->name().empty()) {
            throw std::runtime_error(
                std::format(
                    "Static function value in graph '{}' refers to anonymous source graph {:p}.",
                    owner ? owner->name() : "<null>",
                    static_cast<void *>(sourceGraph)));
        }
        try {
            (void)sourceGraph->shared_from_this();
        } catch (const std::bad_weak_ptr &) {
            throw std::runtime_error(
                std::format(
                    "Static function value in graph '{}' refers to non-owned source graph '{}' "
                    "({:p}).",
                    owner ? owner->name() : "<null>",
                    sourceGraph->name(),
                    static_cast<void *>(sourceGraph)));
        }

        if (::Tuple *closure = funcObj->tuple()) {
            TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
            for (size_t i = 0; i < closureType->size(); ++i) {
                if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                    continue;
                }
                assertStaticValueGraphsWellOwned(
                    owner,
                    closure->get<slot_t>(i),
                    closureType->typeAt(i),
                    visited);
            }
        }
        return;
    }
    case camel::core::type::TypeCode::Tuple: {
        auto *tuple     = camel::core::rtdata::fromSlot<::Tuple *>(slot);
        auto *tupleType = static_cast<TupleType *>(type);
        if (!tuple) {
            return;
        }
        for (size_t i = 0; i < tupleType->size(); ++i) {
            if (!camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                continue;
            }
            assertStaticValueGraphsWellOwned(
                owner,
                tuple->get<slot_t>(i),
                tupleType->typeAt(i),
                visited);
        }
        return;
    }
    case camel::core::type::TypeCode::Array: {
        auto *array     = camel::core::rtdata::fromSlot<::Array *>(slot);
        auto *arrayType = static_cast<camel::core::type::ArrayType *>(type);
        if (!array) {
            return;
        }
        for (size_t i = 0; i < array->size(); ++i) {
            if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
                continue;
            }
            assertStaticValueGraphsWellOwned(
                owner,
                array->get<slot_t>(i),
                arrayType->elemType(),
                visited);
        }
        return;
    }
    case camel::core::type::TypeCode::Struct: {
        auto *st         = camel::core::rtdata::fromSlot<::Struct *>(slot);
        auto *structType = static_cast<camel::core::type::StructType *>(type);
        if (!st) {
            return;
        }
        for (size_t i = 0; i < structType->size(); ++i) {
            if (!camel::core::type::isGCTraced(structType->codeAt(i))) {
                continue;
            }
            assertStaticValueGraphsWellOwned(
                owner,
                st->get<slot_t>(i),
                structType->typeAt(i),
                visited);
        }
        return;
    }
    default:
        return;
    }
}

void assertNoInvalidStaticFunctionGraphs(const std::vector<graph_ptr_t> &draftGraphs) {
    for (const auto &owner : draftGraphs) {
        const TupleType *staticType = owner ? owner->staticDataType() : nullptr;
        if (!owner || !staticType) {
            continue;
        }
        std::unordered_set<const camel::core::rtdata::Object *> visited;
        for (size_t i = 1; i < owner->staticDataSize() && i < staticType->size(); ++i) {
            assertStaticValueGraphsWellOwned(
                owner,
                owner->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                staticType->typeAt(i),
                visited);
        }
    }
}

void assertNoDanglingFuncTargetsAfterFinish(const graph_ptr_t &root) {
    if (!root) {
        return;
    }

    std::unordered_set<const Graph *> reachable;
    std::vector<const Graph *> stack{root.get()};
    while (!stack.empty()) {
        const Graph *curr = stack.back();
        stack.pop_back();
        if (!curr || !reachable.insert(curr).second) {
            continue;
        }
        if (curr != root.get() && curr->name().empty()) {
            throw std::runtime_error(
                std::format(
                    "Final reachable graph tree rooted at '{}' contains anonymous graph {:p} "
                    "(stableId='{}').",
                    root->name(),
                    static_cast<const void *>(curr),
                    curr->stableId()));
        }
        for (const auto &[_, subGraphs] : curr->subGraphs()) {
            for (const auto &sub : subGraphs) {
                stack.push_back(sub.get());
            }
        }
        for (const auto &dep : curr->dependencies()) {
            stack.push_back(dep.get());
        }
    }

    for (const Graph *ownerRaw : reachable) {
        std::vector<Node *> carriers;
        carriers.reserve(ownerRaw->nodes().size() + 1);
        for (Node *node : ownerRaw->nodes()) {
            carriers.push_back(node);
        }
        if (Node *exitAnchor = ownerRaw->exitNode();
            std::ranges::find(carriers, exitAnchor) == carriers.end()) {
            carriers.push_back(exitAnchor);
        }
        for (Node *node : carriers) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *func       = tt::as_ptr<FuncNode>(node);
            Graph *bodyGraph = func ? func->bodyGraph() : nullptr;
            if (!bodyGraph) {
                throw std::runtime_error(
                    std::format(
                        "Final FUNC node '{}' in graph '{}' refers to a null body graph.",
                        node->toString(),
                        ownerRaw->name()));
            }
            if (!reachable.contains(bodyGraph)) {
                std::string refs;
                for (const auto &[name, subGraphs] : ownerRaw->subGraphs()) {
                    for (const auto &sub : subGraphs) {
                        refs += std::format("[sub:{} {:p}] ", name, static_cast<void *>(sub.get()));
                    }
                }
                for (const auto &dep : ownerRaw->dependencies()) {
                    refs +=
                        std::format("[dep:{} {:p}] ", dep->name(), static_cast<void *>(dep.get()));
                }
                throw std::runtime_error(
                    std::format(
                        "Final FUNC node '{}' in graph '{}' refers to unreachable graph {:p} "
                        "('{}'). refs={}",
                        node->toString(),
                        ownerRaw->name(),
                        static_cast<void *>(bodyGraph),
                        bodyGraph->name(),
                        refs));
            }
        }
    }
}

void assertFuncTargetsRegistered(const std::vector<graph_ptr_t> &draftGraphs) {
    for (const auto &owner : draftGraphs) {
        for (Node *node : collectUseCarrierNodes(owner)) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *func       = tt::as_ptr<FuncNode>(node);
            Graph *bodyGraph = func ? func->bodyGraph() : nullptr;
            if (!bodyGraph || bodyGraph == owner.get()) {
                continue;
            }
            if (findGraphHandleInOwnerRegistry(owner, bodyGraph)) {
                continue;
            }
            throw std::runtime_error(
                std::format(
                    "FUNC node '{}' in graph '{}' targets unregistered graph {:p} ('{}').",
                    node->toString(),
                    owner->name(),
                    static_cast<void *>(bodyGraph),
                    bodyGraph->name()));
        }
    }
}

void sweepUnreferencedGraphRegistries(
    camel::runtime::RuntimeSourceGraphEditSession &session,
    const std::vector<graph_ptr_t> &draftGraphs) {
    GraphUseIndex useIndex = buildUseIndex(draftGraphs);
    std::unordered_map<const Graph *, std::unordered_set<const Graph *>> referencedByOwner;
    referencedByOwner.reserve(draftGraphs.size());
    for (const auto &[calleeGraph, useSites] : useIndex) {
        for (const auto &use : useSites) {
            if (!use.owner) {
                continue;
            }
            referencedByOwner[use.owner.get()].insert(calleeGraph);
        }
    }

    for (const auto &owner : draftGraphs) {
        const auto it = referencedByOwner.find(owner.get());
        std::unordered_set<const Graph *> emptySet;
        const auto &liveRefs = (it == referencedByOwner.end()) ? emptySet : it->second;

        std::vector<graph_ptr_t> staleDeps;
        staleDeps.reserve(owner->dependencies().size());
        for (const auto &dep : owner->dependencies()) {
            if (!liveRefs.contains(dep.get())) {
                staleDeps.push_back(dep);
            }
        }
        for (const auto &dep : staleDeps) {
            session.eraseDependency(owner, dep);
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "InlinePass",
                "Pruned stale dependency '{}' from owner '{}'.",
                dep->name(),
                owner->name()));
        }

        std::vector<graph_ptr_t> staleSubGraphs;
        for (const auto &[_, subGraphs] : owner->subGraphs()) {
            for (const auto &sub : subGraphs) {
                if (liveRefs.contains(sub.get())) {
                    continue;
                }
                if (std::ranges::find_if(staleSubGraphs, [&](const graph_ptr_t &g) {
                        return g.get() == sub.get();
                    }) == staleSubGraphs.end()) {
                    staleSubGraphs.push_back(sub);
                }
            }
        }
        for (const auto &sub : staleSubGraphs) {
            session.eraseSubGraph(owner, sub);
            if (owner->dependencies().contains(sub)) {
                session.eraseDependency(owner, sub);
            }
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "InlinePass",
                "Pruned stale subgraph '{}' from owner '{}'.",
                sub->name(),
                owner->name()));
        }
    }
}

SccGraphPlan buildSccGraphPlan(const std::vector<graph_ptr_t> &sourceGraphs) {
    SccGraphPlan plan;
    std::unordered_map<const Graph *, graph_ptr_t> sourceByPtr;
    for (const auto &g : sourceGraphs) {
        sourceByPtr.emplace(g.get(), g);
    }

    std::unordered_map<const Graph *, std::vector<const Graph *>> adjacency;
    for (const auto &g : sourceGraphs) {
        std::unordered_set<const Graph *> uniqueRefs;
        for (const Graph *ref : collectReferencedGraphs(g)) {
            if (sourceByPtr.contains(ref) && ref != g.get()) {
                uniqueRefs.insert(ref);
            }
        }
        adjacency[g.get()] = std::vector<const Graph *>(uniqueRefs.begin(), uniqueRefs.end());
    }

    std::unordered_map<const Graph *, size_t> index;
    std::unordered_map<const Graph *, size_t> low;
    std::unordered_set<const Graph *> onStack;
    std::vector<const Graph *> stack;
    size_t nextIndex = 0;

    std::function<void(const Graph *)> dfs = [&](const Graph *v) {
        index[v] = nextIndex;
        low[v]   = nextIndex;
        nextIndex++;
        stack.push_back(v);
        onStack.insert(v);

        for (const Graph *w : adjacency[v]) {
            if (!index.contains(w)) {
                dfs(w);
                low[v] = std::min(low[v], low[w]);
            } else if (onStack.contains(w)) {
                low[v] = std::min(low[v], index[w]);
            }
        }

        if (low[v] == index[v]) {
            std::vector<const Graph *> component;
            while (!stack.empty()) {
                const Graph *node = stack.back();
                stack.pop_back();
                onStack.erase(node);
                component.push_back(node);
                if (node == v) {
                    break;
                }
            }
            size_t compId = plan.components.size();
            for (const Graph *node : component) {
                plan.componentOf[node] = compId;
            }
            plan.components.push_back(std::move(component));
        }
    };

    for (const auto &g : sourceGraphs) {
        if (!index.contains(g.get())) {
            dfs(g.get());
        }
    }

    plan.dag.assign(plan.components.size(), {});
    std::vector<std::unordered_set<size_t>> dedup(plan.components.size());
    for (const auto &g : sourceGraphs) {
        size_t fromComp = plan.componentOf.at(g.get());
        for (const Graph *ref : adjacency[g.get()]) {
            size_t toComp = plan.componentOf.at(ref);
            if (fromComp == toComp || dedup[fromComp].contains(toComp)) {
                continue;
            }
            dedup[fromComp].insert(toComp);
            plan.dag[fromComp].push_back(toComp);
        }
    }

    std::vector<size_t> indegree(plan.components.size(), 0);
    for (size_t u = 0; u < plan.dag.size(); ++u) {
        for (size_t v : plan.dag[u]) {
            indegree[v]++;
        }
    }
    std::deque<size_t> q;
    for (size_t i = 0; i < indegree.size(); ++i) {
        if (indegree[i] == 0) {
            q.push_back(i);
        }
    }
    std::vector<size_t> topo;
    topo.reserve(plan.components.size());
    while (!q.empty()) {
        size_t u = q.front();
        q.pop_front();
        topo.push_back(u);
        for (size_t v : plan.dag[u]) {
            indegree[v]--;
            if (indegree[v] == 0) {
                q.push_back(v);
            }
        }
    }
    ASSERT(
        topo.size() == plan.components.size(),
        "SCC condensed graph must be a DAG for topological ordering.");
    // Note: edges are defined as caller -> callee.
    // Therefore reverse topological order is callee-first / bottom-up, which
    // ensures the deepest referenced graphs are processed first.
    plan.processOrder = std::vector<size_t>(topo.rbegin(), topo.rend());
    return plan;
}

bool isSmallSubgraphForInline(const Graph *bodyGraph, const InlineRewriteConfig &config) {
    if (!bodyGraph) {
        return false;
    }
    size_t nonDataPortCount = 0;
    for (Node *node : bodyGraph->nodes()) {
        if (node->type() == NodeType::DATA || node->type() == NodeType::PORT) {
            continue;
        }
        nonDataPortCount++;
        if (nonDataPortCount > config.smallSubgraphMaxNonDataPortNodes) {
            return false;
        }
    }
    return true;
}

bool shouldInlineByStrategy(
    const InlineTarget &target, const InlineRewriteConfig &config, const bool isSmallSubgraph) {
    const bool isArmCandidate = target.isBranchArmHead;
    const bool isNonArmSmall  = !target.isBranchArmHead && isSmallSubgraph;
    switch (config.strategy) {
    case InlineTargetStrategy::Small:
        // Small strategy intentionally excludes BRCH arm-head calls.
        return isNonArmSmall;
    case InlineTargetStrategy::Arm:
        return isArmCandidate;
    case InlineTargetStrategy::Hybrid:
        return isNonArmSmall || isArmCandidate;
    default:
        return false;
    }
}

struct SccEntryGuard {
    std::unordered_set<const Graph *> entryRootDraftGraphs;
    std::unordered_set<const Graph *> protectedCalleeDraftGraphs;
};

SccEntryGuard buildSccEntryGuard(
    size_t sccId, const SccGraphPlan &plan, const GraphUseIndex &useIndex,
    const std::unordered_map<const Graph *, graph_ptr_t> &editableBySource) {
    SccEntryGuard guard;
    std::unordered_map<const Graph *, const Graph *> sourceByDraft;
    sourceByDraft.reserve(editableBySource.size());
    for (const auto &[sourceGraph, draftGraph] : editableBySource) {
        sourceByDraft[draftGraph.get()] = sourceGraph;
    }

    for (const Graph *callee : plan.components[sccId]) {
        auto editableIt = editableBySource.find(callee);
        if (editableIt == editableBySource.end()) {
            continue;
        }
        const Graph *draftCallee = editableIt->second.get();
        auto it                  = useIndex.find(draftCallee);
        if (it == useIndex.end()) {
            continue;
        }
        for (const auto &use : it->second) {
            if (!use.owner) {
                continue;
            }
            const auto sourceOwnerIt = sourceByDraft.find(use.owner.get());
            if (sourceOwnerIt == sourceByDraft.end()) {
                guard.entryRootDraftGraphs.insert(draftCallee);
                continue;
            }
            auto ownerCompIt = plan.componentOf.find(sourceOwnerIt->second);
            if (ownerCompIt == plan.componentOf.end() || ownerCompIt->second != sccId) {
                // Entry definition (SCC scope): the callee is called by a caller
                // outside the SCC.
                // We do not infer transitively to avoid misclassifying arm wrapper
                // graphs as entries and breaking valid inlining.
                guard.entryRootDraftGraphs.insert(draftCallee);
                continue;
            }
        }
    }
    // Protect only the entry callee itself:
    // within the SCC, any call targeting the entry callee is no longer inlined.
    guard.protectedCalleeDraftGraphs = guard.entryRootDraftGraphs;
    return guard;
}

std::vector<InlineTarget> collectInitialInlineTargetsForScc(
    size_t sccId, const SccGraphPlan &plan,
    const std::unordered_map<const Graph *, graph_ptr_t> &editableBySource,
    const InlineRewriteConfig &config, const SccEntryGuard &entryGuard) {
    // Freeze the initial targets inside the SCC:
    // this round only handles FUNC targets that already existed when entering
    // the SCC; FUNC targets introduced during inlining are deferred to later
    // passes to avoid single-round oscillation or blow-up.
    std::vector<InlineTarget> candidates;
    for (const Graph *sourceGraph : plan.components[sccId]) {
        graph_ptr_t g = editableBySource.at(sourceGraph);
        std::unordered_set<Node *> seenPaths;
        for (Node *node : collectUseCarrierNodes(g)) {
            if (node->type() != NodeType::FUNC) {
                continue;
            }
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            if (!funcNode->bodyGraph()) {
                continue;
            }
            candidates.push_back(
                InlineTarget{
                    .owner           = g,
                    .path            = node,
                    .bodyGraph       = funcNode->bodyGraph(),
                    .isBranchArmHead = false,
                });
            seenPaths.insert(node);
        }
        for (Node *brch : collectUseCarrierNodes(g)) {
            if (brch->type() != NodeType::BRCH) {
                continue;
            }
            auto *brchNode = tt::as_ptr<BrchNode>(brch);
            for (size_t i = 0; i < brchNode->armCount(); ++i) {
                Node *path = brchNode->armHead(i);
                if (path->type() != NodeType::FUNC) {
                    continue;
                }
                auto *funcPath = tt::as_ptr<FuncNode>(path);
                if (!funcPath->hasMatchedJoin() || !funcPath->bodyGraph()) {
                    continue;
                }
                if (seenPaths.insert(path).second) {
                    candidates.push_back(
                        InlineTarget{
                            .owner           = g,
                            .path            = path,
                            .bodyGraph       = funcPath->bodyGraph(),
                            .isBranchArmHead = true,
                        });
                    continue;
                }
                for (auto &candidate : candidates) {
                    if (candidate.path == path) {
                        candidate.isBranchArmHead = true;
                        break;
                    }
                }
            }
        }
    }
    std::vector<InlineTarget> targets;
    targets.reserve(candidates.size());
    for (const auto &candidate : candidates) {
        if (config.blockCallsToSccEntryCallees && !candidate.isBranchArmHead &&
            candidate.bodyGraph &&
            entryGuard.protectedCalleeDraftGraphs.contains(candidate.bodyGraph)) {
            continue;
        }
        const bool isSmall = isSmallSubgraphForInline(candidate.bodyGraph, config);
        if (!shouldInlineByStrategy(candidate, config, isSmall)) {
            continue;
        }
        targets.push_back(candidate);
    }
    return targets;
}

void assertAdjacencyPointersBelongToGraph(const graph_ptr_t &graph) {
    std::unordered_set<Node *> liveNodes;
    liveNodes.reserve(graph->nodes().size() + graph->ports().size() + graph->closure().size() + 1);
    for (Node *n : graph->nodes()) {
        liveNodes.insert(n);
    }
    for (Node *n : graph->ports()) {
        liveNodes.insert(n);
    }
    for (Node *n : graph->closure()) {
        liveNodes.insert(n);
    }
    liveNodes.insert(graph->exitNode());

    auto assertNodeList =
        [&](Node *owner, const auto &nodes, const char *dir, const char *linkType) {
            for (Node *peer : nodes) {
                std::string peerGraphName = "<unknown>";
                std::string peerNodeStr   = "<unknown>";
                if (peer) {
                    peerGraphName = peer->graph().name();
                    peerNodeStr   = peer->toString();
                }
                ASSERT(
                    liveNodes.contains(peer),
                    std::format(
                        "Graph '{}' has stale {} {} pointer: owner={}, peer={:p}, peerNode={}, "
                        "peerGraph={}.",
                        graph->name(),
                        linkType,
                        dir,
                        owner ? owner->toString() : "<null>",
                        static_cast<void *>(peer),
                        peerNodeStr,
                        peerGraphName));
            }
        };

    for (Node *owner : graph->nodes()) {
        assertNodeList(owner, owner->normInputs(), "input", "norm");
        assertNodeList(owner, owner->withInputs(), "input", "with");
        assertNodeList(owner, owner->ctrlInputs(), "input", "ctrl");
        assertNodeList(owner, owner->normOutputs(), "output", "norm");
        assertNodeList(owner, owner->withOutputs(), "output", "with");
        assertNodeList(owner, owner->ctrlOutputs(), "output", "ctrl");
    }
}

std::unordered_set<Node *> collectLiveNodesFromExit(const graph_ptr_t &graph) {
    std::unordered_set<Node *> live;
    if (!graph) {
        return live;
    }
    std::vector<Node *> stack{graph->exitNode()};
    while (!stack.empty()) {
        Node *curr = stack.back();
        stack.pop_back();
        if (!curr || !live.insert(curr).second) {
            continue;
        }
        for (Node *in : curr->withInputs()) {
            stack.push_back(in);
        }
        for (Node *in : curr->normInputs()) {
            stack.push_back(in);
        }
        for (Node *in : curr->ctrlInputs()) {
            stack.push_back(in);
        }
    }
    return live;
}

void assertAppliedInlineTargetsGone(const std::vector<AppliedInlineRecord> &applied) {
    for (const auto &record : applied) {
        if (!record.owner || !record.targetBodyGraph) {
            continue;
        }
        for (Node *node : record.owner->nodes()) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *func = tt::as_ptr<FuncNode>(node);
            if (!func || func->bodyGraph() != record.targetBodyGraph) {
                continue;
            }
            throw std::runtime_error(
                std::format(
                    "Inline target survived in owner '{}' after applying {} inline: "
                    "erasedPath='{}', "
                    "survivingNode='{}', targetGraph='{}'.",
                    record.owner->name(),
                    record.isBranchArmHead ? "arm-head" : "regular",
                    record.erasedPathText,
                    node->toString(),
                    record.targetBodyGraph->name()));
        }
    }
}

std::unordered_set<Node *> collectBranchSlotPinnedNodes(const graph_ptr_t &graph) {
    std::unordered_set<Node *> pinned;
    if (!graph) {
        return pinned;
    }
    for (Node *node : graph->nodes()) {
        if (node->type() == NodeType::BRCH) {
            pinned.insert(node);
            for (Node *armHead : node->ctrlOutputs()) {
                if (armHead) {
                    pinned.insert(armHead);
                }
            }
            continue;
        }
        if (node->type() == NodeType::JOIN) {
            pinned.insert(node);
            for (Node *armTail : node->withInputs()) {
                if (armTail) {
                    pinned.insert(armTail);
                }
            }
            for (Node *ctrlIn : node->ctrlInputs()) {
                if (ctrlIn) {
                    pinned.insert(ctrlIn);
                }
            }
            for (Node *normIn : node->normInputs()) {
                if (normIn) {
                    pinned.insert(normIn);
                }
            }
        }
    }
    return pinned;
}

size_t pruneUnreachableSlotSafe(
    camel::runtime::RuntimeSourceGraphEditSession &session, const graph_ptr_t &graph) {
    if (!graph) {
        return 0;
    }
    std::unordered_set<Node *> live   = collectLiveNodesFromExit(graph);
    std::unordered_set<Node *> pinned = collectBranchSlotPinnedNodes(graph);
    // Pinned nodes (especially JOIN arm tails) must keep their producer chains.
    // Otherwise we may preserve a slot anchor GATE but erase its only Norm input.
    std::vector<Node *> pinnedStack(pinned.begin(), pinned.end());
    while (!pinnedStack.empty()) {
        Node *curr = pinnedStack.back();
        pinnedStack.pop_back();
        if (!curr) {
            continue;
        }
        auto pushInput = [&](node_span_t inputs) {
            for (Node *in : inputs) {
                if (!in || &in->graph() != graph.get()) {
                    continue;
                }
                if (pinned.insert(in).second) {
                    pinnedStack.push_back(in);
                }
            }
        };
        pushInput(curr->withInputs());
        pushInput(curr->normInputs());
        pushInput(curr->ctrlInputs());
    }

    std::vector<Node *> toErase;
    toErase.reserve(graph->nodes().size());
    for (Node *node : graph->nodes()) {
        if (live.contains(node)) {
            continue;
        }
        // BRCH/JOIN arm slots are semantically defined by edge order. If a
        // corresponding node has become unreachable during rewrite, keep the
        // skeleton node for now to avoid slot misalignment from detach/unlink
        // reordering the input/output vectors.
        if (pinned.contains(node)) {
            continue;
        }
        toErase.push_back(node);
    }
    for (Node *node : toErase) {
        session.eraseNode(node);
    }
    return toErase.size();
}

} // namespace

graph_ptr_t InlineRewritePass::apply(camel::runtime::GCGraph *graph, ostream &os) {
    (void)os;
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "InlinePass",
        "Start inline pass from runtime root {}.",
        graph ? graph->name() : "<null>"));
    if (!graph) {
        return Graph::null();
    }

    camel::runtime::RuntimeGraphRewriteSession runtimeSession(context_, graph);
    auto session                                 = runtimeSession.createSourceEditSession();
    graph_ptr_t sourceRoot                       = session.sourceRoot();
    const std::vector<graph_ptr_t> &sourceGraphs = session.reachableSourceGraphs();
    std::unordered_map<const Graph *, graph_ptr_t> editableGraphsBySource;
    editableGraphsBySource.reserve(sourceGraphs.size());
    for (const auto &sourceGraph : sourceGraphs) {
        editableGraphsBySource.emplace(sourceGraph.get(), session.canonicalGraph(sourceGraph));
    }

    // The runtime-side source edit session has already normalized the cloned
    // source closure, so the inline pass can build its use index directly on
    // top of the editable cloned graphs.
    std::vector<graph_ptr_t> draftGraphs = session.editableGraphs();
    bool structuralRewriteChanged        = false;
    while (true) {
        bool iterationChanged = false;
        iterationChanged |= specializeDirectFuncTargets(session, draftGraphs);
        iterationChanged |= devirtualizeStaticCalls(session, draftGraphs);
        iterationChanged |= ensureFuncBodyDependencies(session, draftGraphs);
        if (!iterationChanged) {
            break;
        }
        structuralRewriteChanged = true;
        for (const auto &draftGraph : draftGraphs) {
            GraphBuilder::validateGraph(*draftGraph);
        }
    }

    GraphUseIndex useIndex = buildUseIndex(draftGraphs);
    std::unordered_set<const Graph *> legacySources;
    legacySources.reserve(sourceGraphs.size());
    for (const auto &sourceGraph : sourceGraphs) {
        legacySources.insert(sourceGraph.get());
    }
    assertNoLegacyGraphRefs(draftGraphs, legacySources);
    assertNoAnonymousFuncTargets(draftGraphs);
    assertNoInvalidStaticFunctionGraphs(draftGraphs);

    // Process SCCs in reverse topological order so deeper callees stabilize before their callers.
    // This reduces repeated re-scans where an upper layer changes first and invalidates lower-layer
    // opportunities.
    SccGraphPlan sccPlan  = buildSccGraphPlan(sourceGraphs);
    bool anyInlineApplied = false;
    std::vector<AppliedInlineRecord> appliedInlineRecords;
    for (size_t sccId : sccPlan.processOrder) {
        const SccEntryGuard sccEntryGuard =
            buildSccEntryGuard(sccId, sccPlan, useIndex, editableGraphsBySource);
        std::vector<InlineTarget> targets = collectInitialInlineTargetsForScc(
            sccId,
            sccPlan,
            editableGraphsBySource,
            config_,
            sccEntryGuard);
        if (targets.empty()) {
            continue;
        }

        if (targets.size() > kInlineTargetBudgetPerScc) {
            EXEC_WHEN_DEBUG(ASSERT(
                false,
                std::format(
                    "Inline budget exceeded before SCC {} starts: targets={}, limit={}.",
                    sccId,
                    targets.size(),
                    kInlineTargetBudgetPerScc)));
            CAMEL_LOG_WARN_S(
                "InlinePass",
                "Skip SCC {} due to target budget exceeded in release: targets={}, limit={}.",
                sccId,
                targets.size(),
                kInlineTargetBudgetPerScc);
            continue;
        }

        std::unordered_map<const Graph *, size_t> nodeCountBefore;
        for (const Graph *sourceGraph : sccPlan.components[sccId]) {
            graph_ptr_t owner            = editableGraphsBySource.at(sourceGraph);
            nodeCountBefore[sourceGraph] = owner->nodes().size();
        }
        std::vector<graph_ptr_t> changedGraphs;
        size_t appliedCount = 0;
        for (const auto &target : targets) {
            Node *path = target.path;
            if (!path || path->type() != NodeType::FUNC) {
                continue;
            }
            if (&path->graph() != target.owner.get()) {
                continue;
            }
            auto *funcPath = tt::as_ptr<FuncNode>(path);
            if (!funcPath->bodyGraph()) {
                continue;
            }
            const Graph *pathGraph = target.bodyGraph ? target.bodyGraph : funcPath->bodyGraph();
            const std::string pathText = path->toString();
            InlineOptions inlineOptions;
            InlineResult inlined = session.inlineCallable(path, inlineOptions);
            if (!inlined || !inlined.valueExit || !inlined.ctrlEntry) {
                continue;
            }
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "InlinePass",
                "Inline result in graph {}: path={}, valueExit={}, ctrlEntry={}.",
                target.owner->name(),
                path->toString(),
                inlined.valueExit ? inlined.valueExit->toString() : "<null>",
                inlined.ctrlEntry ? inlined.ctrlEntry->toString() : "<null>"));

            if (inlined.ctrlEntry == path) {
                continue;
            }
            session.eraseNode(path);
            if (std::ranges::find(target.owner->nodes(), path) != target.owner->nodes().end()) {
                throw std::runtime_error(
                    std::format(
                        "Inline pass erased FUNC '{}' in graph '{}' but node pointer {:p} is still "
                        "present in owner node list.",
                        pathText,
                        target.owner->name(),
                        static_cast<void *>(path)));
            }
            auto sameGraph = [&](const graph_ptr_t &g) { return g.get() == target.owner.get(); };
            if (std::find_if(changedGraphs.begin(), changedGraphs.end(), sameGraph) ==
                changedGraphs.end()) {
                changedGraphs.push_back(target.owner);
            }
            appliedCount++;
            anyInlineApplied = true;
            appliedInlineRecords.push_back(
                AppliedInlineRecord{
                    .owner           = target.owner,
                    .targetBodyGraph = pathGraph,
                    .erasedPathText  = pathText,
                    .isBranchArmHead = target.isBranchArmHead,
                });
            if (appliedCount > kInlineApplyBudgetPerScc) {
                EXEC_WHEN_DEBUG(ASSERT(
                    false,
                    std::format(
                        "Inline apply budget exceeded in SCC {}: applied={}, limit={}.",
                        sccId,
                        appliedCount,
                        kInlineApplyBudgetPerScc)));
                CAMEL_LOG_WARN_S(
                    "InlinePass",
                    "Stop applying inline targets in SCC {} due to apply budget exceeded in "
                    "release: applied={}, limit={}.",
                    sccId,
                    appliedCount,
                    kInlineApplyBudgetPerScc);
                break;
            }

            CAMEL_LOG_INFO_S(
                "InlinePass",
                "Inlined FUNC node {} (graph {}) in graph {}, armHead={}.",
                pathText,
                pathGraph ? pathGraph->name() : "<null>",
                target.owner->name(),
                target.isBranchArmHead ? "true" : "false");
        }

        size_t nodeDelta = 0;
        for (const Graph *sourceGraph : sccPlan.components[sccId]) {
            graph_ptr_t owner = editableGraphsBySource.at(sourceGraph);
            size_t before     = nodeCountBefore[sourceGraph];
            size_t after      = owner->nodes().size();
            if (after > before) {
                nodeDelta += (after - before);
            }
        }
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "InlinePass",
            "Node delta computed for SCC {}: {}.",
            sccId,
            nodeDelta));
        if (nodeDelta > kNodeDeltaBudgetPerScc) {
            EXEC_WHEN_DEBUG(ASSERT(
                false,
                std::format(
                    "Inline node-delta budget exceeded in SCC {}: delta={}, limit={}.",
                    sccId,
                    nodeDelta,
                    kNodeDeltaBudgetPerScc)));
            CAMEL_LOG_WARN_S(
                "InlinePass",
                "Node delta budget exceeded in SCC {} (release): delta={}, limit={}.",
                sccId,
                nodeDelta,
                kNodeDeltaBudgetPerScc);
        }

        for (const auto &changedGraph : changedGraphs) {
            (void)ensureFuncBodyDependencies(session, {changedGraph});
            (void)changedGraph->exitNode();
            const size_t pruned = pruneUnreachableSlotSafe(session, changedGraph);
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "InlinePass",
                "Slot-safe prune in graph {} removed {} unreachable nodes.",
                changedGraph->name(),
                pruned));
            (void)pruned;
            GraphBuilder::validateGraph(*changedGraph);
        }
        assertNoLegacyGraphRefs(draftGraphs, legacySources);
        assertNoAnonymousFuncTargets(draftGraphs);
        assertAppliedInlineTargetsGone(appliedInlineRecords);
    }

    if (anyInlineApplied || structuralRewriteChanged) {
        sweepUnreferencedGraphRegistries(session, draftGraphs);
        assertNoLegacyGraphRefs(draftGraphs, legacySources);
    }

    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "InlinePass",
        "Running adjacency ownership checks for {} draft graphs.",
        editableGraphsBySource.size()));
    for (const auto &[_, g] : editableGraphsBySource) {
        assertAdjacencyPointersBelongToGraph(g);
    }
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("InlinePass", "Adjacency ownership checks passed."));

    assertFuncTargetsRegistered(draftGraphs);
    assertNoAnonymousFuncTargets(draftGraphs);

    auto result = session.finish();
    if ((anyInlineApplied || structuralRewriteChanged) && result &&
        result.get() == sourceRoot.get()) {
        throw std::runtime_error(
            std::format(
                "InlinePass reported changes but finish() returned original root {:p} ('{}').",
                static_cast<void *>(sourceRoot.get()),
                sourceRoot ? sourceRoot->name() : "<null>"));
    }
    assertNoDanglingFuncTargetsAfterFinish(result);
    if (result) {
        assertNoInvalidStaticFunctionGraphs(camel::compile::gir::collectReachableGraphs(result));
    }
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "InlinePass",
        "Inline session finished: changed={}, resultRoot={}.",
        anyInlineApplied || structuralRewriteChanged,
        result ? result->name() : "<null>"));
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "InlinePass",
        "Inline pass apply returns root {}.",
        result ? result->name() : "<null>"));
    return runtimeSession.finish(result);
}
