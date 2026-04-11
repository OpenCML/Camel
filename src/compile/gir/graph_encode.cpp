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
 * Created: Apr. 11, 2026
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Compile-side Graph -> GCGraph encoding.
 *
 * This translation unit owns the one-shot pre-execution materialization from
 * mutable compile Graph objects to immutable runtime GCGraph objects. Keeping
 * the session here avoids leaking compile-GIR traversal into runtime graph
 * core.
 */

#include "camel/compile/gir.h"
#include "camel/compile/gir/static_function.h"
#include "camel/compile/gir/validate.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "runtime/graph_build.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <stdexcept>
#include <unordered_set>

using camel::core::type::ArrayType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

namespace camel::compile::gir {

namespace {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::Object;
using camel::core::rtdata::toSlot;
using camel::runtime::collectStaticGraphRefsRecursive;
using camel::runtime::createGraphDebugRecord;
using camel::runtime::GCAccsBody;
using camel::runtime::GCBranchArm;
using camel::runtime::GCBrchBody;
using camel::runtime::GCCallBody;
using camel::runtime::GCFillBody;
using camel::runtime::GCFuncBody;
using camel::runtime::GCGraph;
using camel::runtime::GCGraphDebugRecord;
using camel::runtime::GCGraphPayloadShape;
using camel::runtime::GCJoinBody;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GCReturnKind;
using camel::runtime::Slice;

template <typename T, typename U> T narrowIntegral(U value, const char *what) {
    ASSERT(
        value <= static_cast<U>(std::numeric_limits<T>::max()),
        std::format("{} exceeds the target integral width.", what));
    return static_cast<T>(value);
}

GCNodeKind toRuntimeNodeKind(NodeType type) {
    switch (type) {
    case NodeType::DATA:
        return GCNodeKind::Data;
    case NodeType::PORT:
        return GCNodeKind::Port;
    case NodeType::CAST:
        return GCNodeKind::Cast;
    case NodeType::COPY:
        return GCNodeKind::Copy;
    case NodeType::FILL:
        return GCNodeKind::Fill;
    case NodeType::ACCS:
        return GCNodeKind::Accs;
    case NodeType::BRCH:
        return GCNodeKind::Brch;
    case NodeType::JOIN:
        return GCNodeKind::Join;
    case NodeType::CALL:
        return GCNodeKind::Call;
    case NodeType::BIND:
        return GCNodeKind::Bind;
    case NodeType::FUNC:
        return GCNodeKind::Func;
    case NodeType::OPER:
        return GCNodeKind::Oper;
    case NodeType::SYNC:
        return GCNodeKind::Sync;
    case NodeType::GATE:
        return GCNodeKind::Gate;
    case NodeType::DREF:
        return GCNodeKind::Dref;
    }
    return GCNodeKind::Data;
}

std::vector<Node *> collectNativeNodes(const graph_ptr_t &graph) {
    std::vector<Node *> nodes;
    const auto &normPorts = graph->normPorts();
    const auto &withPorts = graph->withPorts();
    const auto &closure   = graph->closure();
    nodes.reserve(normPorts.size() + withPorts.size() + closure.size() + graph->nodes().size() + 2);
    nodes.insert(nodes.end(), normPorts.begin(), normPorts.end());
    nodes.insert(nodes.end(), withPorts.begin(), withPorts.end());
    nodes.insert(nodes.end(), closure.begin(), closure.end());
    nodes.insert(nodes.end(), graph->nodes().begin(), graph->nodes().end());

    auto pushUniqueNode = [&](Node *node) {
        if (node && std::ranges::find(nodes, node) == nodes.end()) {
            nodes.push_back(node);
        }
    };

    pushUniqueNode(graph->outputNode());
    pushUniqueNode(graph->exitNode());
    return nodes;
}

size_t nodeBodyBytes(const Node *node) {
    ASSERT(node != nullptr, "Runtime node-body sizing received a null source node.");
    switch (node->type()) {
    case NodeType::FUNC:
        return sizeof(GCFuncBody);
    case NodeType::CALL:
        return sizeof(GCCallBody);
    case NodeType::ACCS: {
        auto *accsNode = tt::as_ptr<AccsNode>(const_cast<Node *>(node));
        ASSERT(accsNode != nullptr, "Runtime ACCS layout sizing requires an ACCS node.");
        return sizeof(GCAccsBody) + (accsNode->isNum() ? 0 : accsNode->strIndex().size());
    }
    case NodeType::FILL:
        return sizeof(GCFillBody);
    case NodeType::BRCH:
        return sizeof(GCBrchBody);
    case NodeType::JOIN:
        return sizeof(GCJoinBody);
    case NodeType::OPER: {
        auto *operNode = tt::as_ptr<OperNode>(const_cast<Node *>(node));
        ASSERT(operNode != nullptr, "Runtime OPER layout sizing requires an OPER node.");
        return sizeof(GCOperBody) + operNode->oper()->uri().size();
    }
    default:
        return 0;
    }
}

camel::runtime::gc_cnt_t nodeBlockCount(const Node *node) {
    const size_t totalBytes = sizeof(GCNode) + nodeBodyBytes(node);
    return camel::runtime::blocksForBytes(totalBytes);
}

std::byte *
mutableNodeStorage(camel::runtime::gc_block_t *blocks, camel::runtime::gc_node_ref_t ref) {
    return reinterpret_cast<std::byte *>(blocks + ref);
}

template <typename Body>
void writeBody(
    camel::runtime::gc_block_t *blocks, camel::runtime::gc_node_ref_t ref, const Body &body) {
    static_assert(
        std::is_trivially_copyable_v<Body>,
        "GC node bodies must remain trivially copyable.");
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode);
    std::memcpy(dst, &body, sizeof(Body));
}

void writeTrailingBytes(
    camel::runtime::gc_block_t *blocks, camel::runtime::gc_node_ref_t ref, size_t bodyHeaderBytes,
    std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    auto *dst = mutableNodeStorage(blocks, ref) + sizeof(GCNode) + bodyHeaderBytes;
    std::memcpy(dst, bytes.data(), bytes.size());
}

camel::runtime::GCFillKind classifyFillKind(Type *type) {
    ASSERT(type != nullptr, "Runtime FILL classification requires a target type.");
    switch (type->code()) {
    case TypeCode::Tuple:
        return camel::runtime::GCFillKind::Tuple;
    case TypeCode::Array:
        return camel::runtime::GCFillKind::Array;
    case TypeCode::Struct:
        return camel::runtime::GCFillKind::Struct;
    case TypeCode::Function:
        return camel::runtime::GCFillKind::FunctionClosure;
    default:
        ASSERT(false, std::format("Unsupported runtime FILL target type '{}'.", type->toString()));
        return camel::runtime::GCFillKind::Tuple;
    }
}

struct PlannedNativeNode {
    Node *source                        = nullptr;
    camel::runtime::gc_node_ref_t ref   = camel::runtime::kInvalidNodeRef;
    camel::runtime::gc_cnt_t blockCount = 0;
};

struct NativePayloadPlan {
    std::vector<PlannedNativeNode> nodes;
    std::unordered_map<const Node *, camel::runtime::gc_node_ref_t> nodeRefs;
    camel::runtime::gc_cnt_t nodeBlockCount = 0;
    camel::runtime::gc_cnt_t edgeCount      = 0;
    camel::runtime::gc_cnt_t branchArmCount = 0;
    camel::runtime::gc_cnt_t normPortCount  = 0;
    camel::runtime::gc_cnt_t withPortCount  = 0;
    camel::runtime::gc_cnt_t closureCount   = 0;
};

NativePayloadPlan planNativePayload(const graph_ptr_t &graph) {
    NativePayloadPlan plan;
    if (!graph) {
        return plan;
    }

    std::vector<Node *> orderedNodes = collectNativeNodes(graph);
    plan.nodes.reserve(orderedNodes.size());
    plan.nodeRefs.reserve(orderedNodes.size());
    plan.normPortCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        graph->normPorts().size(),
        "Runtime norm-port count");
    plan.withPortCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        graph->withPorts().size(),
        "Runtime with-port count");
    plan.closureCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        graph->closure().size(),
        "Runtime closure-node count");

    camel::runtime::gc_off_t currentNodeOffset = 0;
    size_t edgeCount                           = 0;
    size_t branchArmCount                      = 0;
    for (Node *node : orderedNodes) {
        const camel::runtime::gc_cnt_t blocks = nodeBlockCount(node);
        plan.nodes.push_back(
            PlannedNativeNode{.source = node, .ref = currentNodeOffset, .blockCount = blocks});
        plan.nodeRefs.emplace(node, currentNodeOffset);
        currentNodeOffset = narrowIntegral<camel::runtime::gc_off_t>(
            static_cast<size_t>(currentNodeOffset) + blocks,
            "Runtime node blob size");
        edgeCount += node->normInputs().size() + node->withInputs().size() +
                     node->ctrlInputs().size() + node->normOutputs().size() +
                     node->withOutputs().size() + node->ctrlOutputs().size();
        if (node->type() == NodeType::BRCH) {
            auto *brchNode = tt::as_ptr<BrchNode>(node);
            ASSERT(brchNode != nullptr, "Runtime BRCH plan requires BRCH metadata.");
            branchArmCount += brchNode->armCount();
        }
    }

    plan.nodeBlockCount = narrowIntegral<camel::runtime::gc_cnt_t>(
        currentNodeOffset,
        "Runtime node-blob block count");
    plan.edgeCount = narrowIntegral<camel::runtime::gc_cnt_t>(edgeCount, "Runtime edge arena size");
    plan.branchArmCount =
        narrowIntegral<camel::runtime::gc_cnt_t>(branchArmCount, "Runtime branch-arm arena size");
    return plan;
}

GCGraphPayloadShape describeNativePayload(const NativePayloadPlan &plan) {
    return GCGraphPayloadShape{
        .nodeCount =
            narrowIntegral<camel::runtime::gc_cnt_t>(plan.nodes.size(), "Runtime node count"),
        .nodeBlockCount = plan.nodeBlockCount,
        .edgeCount      = plan.edgeCount,
        .normPortCount  = plan.normPortCount,
        .withPortCount  = plan.withPortCount,
        .closureCount   = plan.closureCount,
        .branchArmCount = plan.branchArmCount,
    };
}

graph_ptr_t
requireSourceGraphHandle(Graph *graph, std::string_view context, GCGraph *owner = nullptr) {
    ASSERT(graph != nullptr, "Compile graph encode received a null source graph.");
    try {
        return graph->shared_from_this();
    } catch (const std::bad_weak_ptr &) {
        throw std::runtime_error(
            std::format(
                "{} encountered non-owned source graph '{}' ({:p}) while materializing runtime "
                "owner {:p}.",
                context,
                graph->name(),
                static_cast<void *>(graph),
                static_cast<void *>(owner)));
    }
}

graph_ptr_t resolveGraphHandle(const graph_ptr_t &owner, const Graph *target) {
    if (!owner || !target) {
        return nullptr;
    }
    if (owner.get() == target) {
        return owner;
    }
    for (const auto &dep : owner->dependencies()) {
        if (dep.get() == target) {
            return dep;
        }
    }
    for (const auto &[_, subGraphs] : owner->subGraphs()) {
        for (const auto &subGraph : subGraphs) {
            if (subGraph.get() == target) {
                return subGraph;
            }
        }
    }
    if (auto outer = owner->outer()) {
        if (auto resolved = resolveGraphHandle(outer, target)) {
            return resolved;
        }
    }
    return nullptr;
}

GCGraph *resolveCompileStaticFunctionGraph(
    StaticFunction *funcObj, const graph_ptr_t &ownerSourceGraph,
    const std::function<GCGraph *(const graph_ptr_t &)> &materializeSourceGraph,
    std::string_view context, GCGraph *owner = nullptr) {
    ASSERT(funcObj != nullptr, "Compile graph encode received a null static function.");
    Graph *sourceGraph = funcObj->graph();
    ASSERT(sourceGraph != nullptr, "Compile-time static function must reference a graph.");
    graph_ptr_t targetGraph = resolveGraphHandle(ownerSourceGraph, sourceGraph);
    if (!targetGraph) {
        targetGraph = requireSourceGraphHandle(sourceGraph, context, owner);
    }
    return materializeSourceGraph(targetGraph);
}

void emitCompileGraphPayload(
    const graph_ptr_t &graph, const NativePayloadPlan &plan,
    camel::runtime::GCGraphPayloadArena &payload,
    const std::function<GCGraph *(const Graph *)> &resolveRuntimeGraph) {
    ASSERT(
        payload.nodeCount ==
            narrowIntegral<camel::runtime::gc_cnt_t>(plan.nodes.size(), "Runtime node count"),
        "Compile payload arena shape does not match the planned node count.");

    camel::runtime::gc_off_t edgeCursor      = 0;
    camel::runtime::gc_off_t branchArmCursor = 0;
    auto appendSlice                         = [&](node_span_t span) -> Slice {
        Slice slice{.offset = edgeCursor, .count = 0};
        for (Node *node : span) {
            auto it = plan.nodeRefs.find(node);
            ASSERT(
                it != plan.nodeRefs.end(),
                "Runtime edge emission encountered a node outside the payload plan.");
            payload.edges[edgeCursor++] = it->second;
        }
        slice.count = narrowIntegral<camel::runtime::gc_cnt_t>(
            edgeCursor - slice.offset,
            "Runtime edge slice count");
        return slice;
    };

    for (const PlannedNativeNode &planned : plan.nodes) {
        Node *sourceNode                        = planned.source;
        const camel::runtime::gc_node_ref_t ref = planned.ref;
        auto *header = reinterpret_cast<GCNode *>(mutableNodeStorage(payload.nodeBlocks, ref));
        *header      = GCNode{
            .dataIndex   = static_cast<camel::runtime::gc_slot_idx_t>(sourceNode->index()),
            .blockCount  = planned.blockCount,
            .normInputs  = appendSlice(sourceNode->normInputs()),
            .withInputs  = appendSlice(sourceNode->withInputs()),
            .ctrlInputs  = appendSlice(sourceNode->ctrlInputs()),
            .normOutputs = appendSlice(sourceNode->normOutputs()),
            .withOutputs = appendSlice(sourceNode->withOutputs()),
            .ctrlOutputs = appendSlice(sourceNode->ctrlOutputs()),
            .dataType    = sourceNode->dataType(),
            .kind        = toRuntimeNodeKind(sourceNode->type()),
            .flags       = static_cast<uint8_t>(
                (sourceNode->macro() ? camel::runtime::kGCNodeFlagMacro : 0) |
                (sourceNode->constant() ? camel::runtime::kGCNodeFlagConstant : 0)),
        };

        switch (sourceNode->type()) {
        case NodeType::FUNC: {
            auto *funcNode = tt::as_ptr<FuncNode>(sourceNode);
            ASSERT(
                funcNode != nullptr && funcNode->bodyGraph() != nullptr,
                "Runtime FUNC node requires a body graph.");
            writeBody(
                payload.nodeBlocks,
                ref,
                GCFuncBody{.calleeGraph = resolveRuntimeGraph(funcNode->bodyGraph())});
        } break;
        case NodeType::CALL:
            writeBody(payload.nodeBlocks, ref, GCCallBody{});
            break;
        case NodeType::ACCS: {
            auto *accsNode = tt::as_ptr<AccsNode>(sourceNode);
            ASSERT(accsNode != nullptr, "Runtime ACCS node layout requires ACCS metadata.");
            GCAccsBody body{
                .accsKind  = accsNode->isNum() ? camel::runtime::GCAccsKind::TupleIndex
                                               : camel::runtime::GCAccsKind::StructKey,
                .reserved0 = 0,
                .keyBytes  = narrowIntegral<uint16_t>(
                    accsNode->isNum() ? 0 : accsNode->strIndex().size(),
                    "Runtime ACCS key length"),
                .value = narrowIntegral<uint32_t>(
                    accsNode->isNum() ? accsNode->numIndex() : 0,
                    "Runtime ACCS numeric index"),
            };
            writeBody(payload.nodeBlocks, ref, body);
            if (!accsNode->isNum()) {
                writeTrailingBytes(
                    payload.nodeBlocks,
                    ref,
                    sizeof(GCAccsBody),
                    accsNode->strIndex());
            }
        } break;
        case NodeType::FILL:
            writeBody(
                payload.nodeBlocks,
                ref,
                GCFillBody{.fillKind = classifyFillKind(sourceNode->dataType())});
            break;
        case NodeType::BRCH: {
            auto *brchNode = tt::as_ptr<BrchNode>(sourceNode);
            ASSERT(
                brchNode != nullptr && brchNode->matchedJoin() != nullptr,
                "Runtime BRCH node requires a matched JOIN.");
            const camel::runtime::gc_off_t armOffset = branchArmCursor;
            const camel::runtime::gc_cnt_t armCount  = narrowIntegral<camel::runtime::gc_cnt_t>(
                brchNode->armCount(),
                "Runtime branch-arm count");
            for (size_t i = 0; i < brchNode->armCount(); ++i) {
                payload.branchArms[branchArmCursor++] = GCBranchArm{
                    .head = plan.nodeRefs.at(brchNode->armHead(i)),
                    .tail = plan.nodeRefs.at(brchNode->matchedJoin()->armTail(i)),
                };
            }
            writeBody(
                payload.nodeBlocks,
                ref,
                GCBrchBody{
                    .join       = plan.nodeRefs.at(brchNode->matchedJoin()),
                    .armOffset  = armOffset,
                    .armCount   = armCount,
                    .defaultArm = camel::runtime::kInvalidNodeRef,
                });
        } break;
        case NodeType::JOIN: {
            auto *joinNode = tt::as_ptr<JoinNode>(sourceNode);
            ASSERT(joinNode != nullptr, "Runtime JOIN node layout requires JOIN metadata.");
            GCJoinBody body{
                .brch     = camel::runtime::kInvalidNodeRef,
                .armCount = narrowIntegral<camel::runtime::gc_cnt_t>(
                    joinNode->armCount(),
                    "Runtime JOIN arm count"),
                .reserved = 0,
            };
            for (Node *input : sourceNode->normInputs()) {
                if (input && input->type() == NodeType::BRCH) {
                    body.brch = plan.nodeRefs.at(input);
                    break;
                }
            }
            writeBody(payload.nodeBlocks, ref, body);
        } break;
        case NodeType::OPER: {
            auto *operNode = tt::as_ptr<OperNode>(sourceNode);
            ASSERT(
                operNode != nullptr && operNode->oper() != nullptr,
                "Runtime OPER node layout requires operator metadata.");
            GCOperBody body{
                .op       = operNode->getCachedOp(),
                .uriBytes = narrowIntegral<uint16_t>(
                    operNode->oper()->uri().size(),
                    "Runtime OPER URI length"),
                .reserved = 0,
            };
            writeBody(payload.nodeBlocks, ref, body);
            writeTrailingBytes(
                payload.nodeBlocks,
                ref,
                sizeof(GCOperBody),
                operNode->oper()->uri());
        } break;
        default:
            break;
        }
    }

    for (size_t i = 0; i < graph->normPorts().size(); ++i) {
        payload.normPorts[i] = plan.nodeRefs.at(graph->normPorts()[i]);
    }
    for (size_t i = 0; i < graph->withPorts().size(); ++i) {
        payload.withPorts[i] = plan.nodeRefs.at(graph->withPorts()[i]);
    }
    for (size_t i = 0; i < graph->closure().size(); ++i) {
        payload.closure[i] = plan.nodeRefs.at(graph->closure()[i]);
    }

    payload.entryNode =
        plan.nodes.empty() ? camel::runtime::kInvalidNodeRef : plan.nodes.front().ref;
    payload.exitNode =
        graph->exitNode() ? plan.nodeRefs.at(graph->exitNode()) : camel::runtime::kInvalidNodeRef;
    payload.outputNode =
        graph->outputNode() ? plan.nodeRefs.at(graph->outputNode()) : payload.exitNode;
    payload.returnNode = payload.exitNode;
    payload.returnKind = GCReturnKind::None;
    if (Node *exitNode = graph->exitNode()) {
        if (exitNode->index() != 0) {
            payload.returnKind = GCReturnKind::Self;
            payload.returnNode = plan.nodeRefs.at(exitNode);
        } else if (!exitNode->dataInputs().empty()) {
            payload.returnKind = GCReturnKind::LastDataInput;
            payload.returnNode = plan.nodeRefs.at(exitNode->dataInputs().back());
        } else if (!exitNode->ctrlInputs().empty()) {
            payload.returnKind = GCReturnKind::LastCtrlInput;
            payload.returnNode = plan.nodeRefs.at(exitNode->ctrlInputs().back());
        }
    }

    ASSERT(
        edgeCursor == payload.edgeCount,
        "Compile direct payload emission did not match edge count.");
    ASSERT(
        branchArmCursor == payload.branchArmCount,
        "Compile direct payload emission did not match branch-arm count.");
}

class GraphEncodeSession {
  public:
    GCGraph *encode(const graph_ptr_t &sourceGraph) {
        if (!sourceGraph) {
            return nullptr;
        }
        collectClosure(sourceGraph);
        allocateRuntimeGraphs();
        emitRuntimeGraphs();
        return runtimeGraphs_.at(sourceGraph.get());
    }

  private:
    void collectClosure(const graph_ptr_t &sourceGraph) {
        if (!sourceGraph || runtimeGraphs_.contains(sourceGraph.get())) {
            return;
        }
        EXEC_WHEN_DEBUG({
            validate::assertGraphSealingPreconditions(*sourceGraph);
            validate::assertGraphTreeStaticReferences(sourceGraph);
        });
        sourceGraph->refreshDerivedLayout();
        runtimeGraphs_.emplace(sourceGraph.get(), nullptr);
        closure_.push_back(sourceGraph);

        for (const auto &dep : sourceGraph->dependencies()) {
            collectClosure(dep);
        }
        for (Node *node : sourceGraph->nodes()) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            if (!funcNode || !funcNode->bodyGraph()) {
                continue;
            }
            auto target = resolveGraphHandle(sourceGraph, funcNode->bodyGraph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcNode->bodyGraph(),
                    "Graph::encode(closure-func-body)");
            }
            collectClosure(target);
        }
        for (const auto &[_, nestedSubGraphs] : sourceGraph->subGraphs()) {
            for (const auto &subGraph : nestedSubGraphs) {
                collectClosure(subGraph);
            }
        }
        std::unordered_set<const Object *> visited;
        const TupleType *staticType = sourceGraph->staticDataType();
        if (staticType) {
            for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
                visitStaticFunctionGraphs(
                    sourceGraph->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                    staticType->typeAt(i),
                    sourceGraph,
                    visited);
            }
        }
    }

    void visitStaticFunctionGraphs(
        slot_t slot, Type *type, const graph_ptr_t &ownerSourceGraph,
        std::unordered_set<const Object *> &visited) {
        if (!type || !type->isGCTraced() || slot == NullSlot) {
            return;
        }
        Object *object = fromSlot<Object *>(slot);
        if (!object || !visited.insert(object).second) {
            return;
        }

        switch (type->code()) {
        case TypeCode::Function: {
            auto *funcObj = fromSlot<StaticFunction *>(slot);
            if (!funcObj) {
                return;
            }
            auto target = resolveGraphHandle(ownerSourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(static-slot-function)");
            }
            collectClosure(target);
            if (::Tuple *closure = funcObj->tuple()) {
                TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
                for (size_t i = 0; i < closureType->size(); ++i) {
                    if (!camel::core::type::isGCTraced(closureType->codeAt(i))) {
                        continue;
                    }
                    visitStaticFunctionGraphs(
                        closure->get<slot_t>(i),
                        closureType->typeAt(i),
                        ownerSourceGraph,
                        visited);
                }
            }
            break;
        }
        case TypeCode::Tuple: {
            auto *tuple     = fromSlot<::Tuple *>(slot);
            auto *tupleType = static_cast<TupleType *>(type);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                if (camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                    visitStaticFunctionGraphs(
                        tuple->get<slot_t>(i),
                        tupleType->typeAt(i),
                        ownerSourceGraph,
                        visited);
                }
            }
            break;
        }
        case TypeCode::Array: {
            auto *array     = fromSlot<::Array *>(slot);
            auto *arrayType = static_cast<ArrayType *>(type);
            if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
                return;
            }
            for (size_t i = 0; i < array->size(); ++i) {
                visitStaticFunctionGraphs(
                    array->get<slot_t>(i),
                    arrayType->elemType(),
                    ownerSourceGraph,
                    visited);
            }
            break;
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            for (size_t i = 0; i < structType->size(); ++i) {
                if (camel::core::type::isGCTraced(structType->codeAt(i))) {
                    visitStaticFunctionGraphs(
                        st->get<slot_t>(i),
                        structType->typeAt(i),
                        ownerSourceGraph,
                        visited);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    std::vector<graph_ptr_t> collectDependencyHandles(const graph_ptr_t &sourceGraph) {
        std::vector<graph_ptr_t> dependencies;
        for (const auto &dep : sourceGraph->dependencies()) {
            if (dep) {
                dependencies.push_back(dep);
            }
        }
        for (Node *node : sourceGraph->nodes()) {
            if (!node || node->type() != NodeType::FUNC) {
                continue;
            }
            auto *funcNode = tt::as_ptr<FuncNode>(node);
            if (!funcNode || !funcNode->bodyGraph()) {
                continue;
            }
            auto targetGraph = resolveGraphHandle(sourceGraph, funcNode->bodyGraph());
            if (!targetGraph) {
                targetGraph = requireSourceGraphHandle(
                    funcNode->bodyGraph(),
                    "Graph::encode(func-body)",
                    nullptr);
            }
            if (targetGraph && std::find(dependencies.begin(), dependencies.end(), targetGraph) ==
                                   dependencies.end()) {
                dependencies.push_back(targetGraph);
            }
        }
        return dependencies;
    }

    std::vector<graph_ptr_t> collectSubGraphHandles(const graph_ptr_t &sourceGraph) {
        std::vector<graph_ptr_t> subGraphs;
        for (const auto &[_, nestedSubGraphs] : sourceGraph->subGraphs()) {
            for (const auto &subGraph : nestedSubGraphs) {
                if (subGraph) {
                    subGraphs.push_back(subGraph);
                }
            }
        }
        return subGraphs;
    }

    std::vector<slot_t> buildStaticSlots(const graph_ptr_t &sourceGraph) {
        std::vector<slot_t> runtimeStaticSlots(sourceGraph->staticDataSize(), NullSlot);
        const TupleType *staticType = sourceGraph->staticDataType();
        if (!staticType) {
            return runtimeStaticSlots;
        }

        std::unordered_map<const Object *, Object *> objectCache;
        for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
            runtimeStaticSlots[i] = canonicalizeStaticSlot(
                sourceGraph->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                staticType->typeAt(i),
                sourceGraph,
                objectCache);
        }
        return runtimeStaticSlots;
    }

    slot_t canonicalizeStaticSlot(
        slot_t slot, Type *type, const graph_ptr_t &sourceGraph,
        std::unordered_map<const Object *, Object *> &objectCache) {
        if (!type || !type->isGCTraced() || slot == NullSlot) {
            return slot;
        }

        Object *object = fromSlot<Object *>(slot);
        if (!object) {
            return slot;
        }
        if (auto it = objectCache.find(object); it != objectCache.end()) {
            return toSlot<Object *>(it->second);
        }

        switch (type->code()) {
        case TypeCode::Function: {
            auto *funcObj = fromSlot<StaticFunction *>(slot);
            ASSERT(funcObj != nullptr, "Function static slot payload is null.");
            auto target = resolveGraphHandle(sourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(canonicalize-static-function)");
            }
            auto *runtimeFunc = ::Function::create(
                runtimeGraphs_.at(target.get()),
                funcObj->tupleType(),
                camel::core::mm::autoSpace());
            objectCache.emplace(object, runtimeFunc);
            if (::Tuple *closure = funcObj->tuple()) {
                ::Tuple *runtimeClosure             = runtimeFunc->tuple();
                const TupleType *runtimeClosureType = runtimeFunc->tupleType();
                for (size_t i = 0; i < runtimeClosureType->size(); ++i) {
                    runtimeClosure->set<slot_t>(
                        i,
                        canonicalizeStaticSlot(
                            closure->get<slot_t>(i),
                            runtimeClosureType->typeAt(i),
                            sourceGraph,
                            objectCache));
                }
            }
            return toSlot<Object *>(runtimeFunc);
        }
        case TypeCode::Tuple: {
            auto *tuple        = fromSlot<::Tuple *>(slot);
            auto *tupleType    = static_cast<TupleType *>(type);
            auto *runtimeTuple = ::Tuple::create(tupleType->size(), camel::core::mm::autoSpace());
            objectCache.emplace(object, runtimeTuple);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                runtimeTuple->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(tupleType->codeAt(i)) ? canonicalizeStaticSlot(
                                                                              tuple->get<slot_t>(i),
                                                                              tupleType->typeAt(i),
                                                                              sourceGraph,
                                                                              objectCache)
                                                                        : tuple->get<slot_t>(i));
            }
            return toSlot<Object *>(runtimeTuple);
        }
        case TypeCode::Array: {
            auto *array        = fromSlot<::Array *>(slot);
            auto *arrayType    = static_cast<ArrayType *>(type);
            auto *runtimeArray = ::Array::create(camel::core::mm::autoSpace(), array->size());
            objectCache.emplace(object, runtimeArray);
            for (size_t i = 0; i < array->size(); ++i) {
                runtimeArray->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(arrayType->elemTypeCode())
                        ? canonicalizeStaticSlot(
                              array->get<slot_t>(i),
                              arrayType->elemType(),
                              sourceGraph,
                              objectCache)
                        : array->get<slot_t>(i));
            }
            return toSlot<Object *>(runtimeArray);
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            auto *runtimeStruct =
                ::Struct::create(structType->size(), camel::core::mm::autoSpace());
            objectCache.emplace(object, runtimeStruct);
            for (size_t i = 0; i < structType->size(); ++i) {
                runtimeStruct->set<slot_t>(
                    i,
                    camel::core::type::isGCTraced(structType->codeAt(i))
                        ? canonicalizeStaticSlot(
                              st->get<slot_t>(i),
                              structType->typeAt(i),
                              sourceGraph,
                              objectCache)
                        : st->get<slot_t>(i));
            }
            return toSlot<Object *>(runtimeStruct);
        }
        default: {
            Object *cloned = object->clone(camel::core::mm::autoSpace(), type, false);
            objectCache.emplace(object, cloned);
            return toSlot<Object *>(cloned);
        }
        }
    }

    std::vector<GCGraph *>
    collectStaticGraphRefs(const graph_ptr_t &sourceGraph, std::span<const slot_t> runtimeSlots) {
        std::vector<GCGraph *> staticGraphRefs;
        for (Node *node : sourceGraph->nodes()) {
            if (!node || node->type() != NodeType::DATA) {
                continue;
            }
            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (!dataNode || dataNode->dataType()->code() != TypeCode::Function) {
                continue;
            }
            auto *funcObj = fromSlot<StaticFunction *>(dataNode->dataSlot());
            if (!funcObj) {
                continue;
            }
            if (GCGraph *runtimeRef = resolveCompileStaticFunctionGraph(
                    funcObj,
                    sourceGraph,
                    [&](const graph_ptr_t &target) { return runtimeGraphs_.at(target.get()); },
                    "Graph::encode(static-function-ref)")) {
                if (std::find(staticGraphRefs.begin(), staticGraphRefs.end(), runtimeRef) ==
                    staticGraphRefs.end()) {
                    staticGraphRefs.push_back(runtimeRef);
                }
            }
        }

        const TupleType *staticType = sourceGraph->staticDataType();
        std::unordered_set<const Object *> visited;
        if (staticType) {
            for (size_t i = 1; i < runtimeSlots.size() && i < staticType->size(); ++i) {
                collectStaticGraphRefsRecursive(
                    staticGraphRefs,
                    runtimeSlots[i],
                    staticType->typeAt(i),
                    visited);
            }
        }
        return staticGraphRefs;
    }

    GCGraph *runtimeGraphOf(const graph_ptr_t &graph) const {
        if (!graph) {
            return nullptr;
        }
        auto it = runtimeGraphs_.find(graph.get());
        ASSERT(it != runtimeGraphs_.end(), "Runtime graph closure is missing a planned graph.");
        return it->second;
    }

    GCGraph *runtimeOuterGraphOf(const graph_ptr_t &graph) const {
        if (!graph) {
            return nullptr;
        }
        graph_ptr_t outerHandle = graph->outer();
        if (!outerHandle) {
            return nullptr;
        }
        return runtimeGraphOf(outerHandle);
    }

    std::vector<GCGraph *> mapRuntimeGraphs(const std::vector<graph_ptr_t> &graphs) const {
        std::vector<GCGraph *> result;
        result.reserve(graphs.size());
        for (const auto &graph : graphs) {
            if (graph) {
                result.push_back(runtimeGraphOf(graph));
            }
        }
        return result;
    }

    void allocateRuntimeGraphs() {
        if (allocated_) {
            return;
        }
        for (const auto &sourceGraph : closure_) {
            auto depHandles      = collectDependencyHandles(sourceGraph);
            auto subGraphHandles = collectSubGraphHandles(sourceGraph);
            std::vector<GCGraph *> depPlaceholders(depHandles.size(), nullptr);
            std::vector<GCGraph *> subPlaceholders(subGraphHandles.size(), nullptr);
            std::vector<GCGraph *> staticRefPlaceholders(
                countStaticGraphHandleRefs(sourceGraph),
                nullptr);
            std::vector<slot_t> staticSlots(sourceGraph->staticDataSize(), NullSlot);
            auto [planIt, inserted] =
                payloadPlans_.try_emplace(sourceGraph.get(), planNativePayload(sourceGraph));
            ASSERT(inserted, "Compile payload plan should be recorded exactly once per graph.");
            const auto payloadShape = describeNativePayload(planIt->second);
            size_t bytes            = camel::runtime::GCGraphBuildAccess::requiredBytes(
                depPlaceholders,
                subPlaceholders,
                staticRefPlaceholders,
                payloadShape,
                staticSlots);
            void *mem = camel::core::mm::graphSpace().alloc(bytes, alignof(GCGraph));
            if (!mem) {
                throw std::bad_alloc();
            }
            runtimeGraphs_[sourceGraph.get()]  = reinterpret_cast<GCGraph *>(mem);
            allocatedBytes_[sourceGraph.get()] = bytes;
        }
        allocated_ = true;
    }

    size_t countStaticGraphHandleRefs(const graph_ptr_t &sourceGraph) {
        std::vector<graph_ptr_t> refs;
        auto pushUniqueHandle = [&](const graph_ptr_t &graph) {
            if (graph && std::find(refs.begin(), refs.end(), graph) == refs.end()) {
                refs.push_back(graph);
            }
        };
        for (Node *node : sourceGraph->nodes()) {
            if (!node || node->type() != NodeType::DATA) {
                continue;
            }
            auto *dataNode = tt::as_ptr<DataNode>(node);
            if (!dataNode || dataNode->dataType()->code() != TypeCode::Function) {
                continue;
            }
            auto *funcObj = fromSlot<StaticFunction *>(dataNode->dataSlot());
            if (!funcObj) {
                continue;
            }
            auto target = resolveGraphHandle(sourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(static-function-ref-count)");
            }
            pushUniqueHandle(target);
        }
        std::unordered_set<const Object *> visited;
        const TupleType *staticType = sourceGraph->staticDataType();
        if (staticType) {
            for (size_t i = 1; i < sourceGraph->staticDataSize() && i < staticType->size(); ++i) {
                collectStaticFunctionRefHandles(
                    refs,
                    sourceGraph->getStaticDataSlot(-static_cast<data_idx_t>(i)),
                    staticType->typeAt(i),
                    sourceGraph,
                    visited);
            }
        }
        return refs.size();
    }

    void collectStaticFunctionRefHandles(
        std::vector<graph_ptr_t> &refs, slot_t slot, Type *type, const graph_ptr_t &sourceGraph,
        std::unordered_set<const Object *> &visited) {
        if (!type || !type->isGCTraced() || slot == NullSlot) {
            return;
        }
        Object *object = fromSlot<Object *>(slot);
        if (!object || !visited.insert(object).second) {
            return;
        }
        auto pushUniqueHandle = [&](const graph_ptr_t &graph) {
            if (graph && std::find(refs.begin(), refs.end(), graph) == refs.end()) {
                refs.push_back(graph);
            }
        };
        switch (type->code()) {
        case TypeCode::Function: {
            auto *funcObj = fromSlot<StaticFunction *>(slot);
            if (!funcObj) {
                return;
            }
            auto target = resolveGraphHandle(sourceGraph, funcObj->graph());
            if (!target) {
                target = requireSourceGraphHandle(
                    funcObj->graph(),
                    "Graph::encode(static-slot-ref-count)");
            }
            pushUniqueHandle(target);
            if (::Tuple *closure = funcObj->tuple()) {
                TupleType *closureType = const_cast<TupleType *>(funcObj->tupleType());
                for (size_t i = 0; i < closureType->size(); ++i) {
                    if (camel::core::type::isGCTraced(closureType->codeAt(i))) {
                        collectStaticFunctionRefHandles(
                            refs,
                            closure->get<slot_t>(i),
                            closureType->typeAt(i),
                            sourceGraph,
                            visited);
                    }
                }
            }
            break;
        }
        case TypeCode::Tuple: {
            auto *tuple     = fromSlot<::Tuple *>(slot);
            auto *tupleType = static_cast<TupleType *>(type);
            for (size_t i = 0; i < tupleType->size(); ++i) {
                if (camel::core::type::isGCTraced(tupleType->codeAt(i))) {
                    collectStaticFunctionRefHandles(
                        refs,
                        tuple->get<slot_t>(i),
                        tupleType->typeAt(i),
                        sourceGraph,
                        visited);
                }
            }
            break;
        }
        case TypeCode::Array: {
            auto *array     = fromSlot<::Array *>(slot);
            auto *arrayType = static_cast<ArrayType *>(type);
            if (!camel::core::type::isGCTraced(arrayType->elemTypeCode())) {
                return;
            }
            for (size_t i = 0; i < array->size(); ++i) {
                collectStaticFunctionRefHandles(
                    refs,
                    array->get<slot_t>(i),
                    arrayType->elemType(),
                    sourceGraph,
                    visited);
            }
            break;
        }
        case TypeCode::Struct: {
            auto *st         = fromSlot<::Struct *>(slot);
            auto *structType = static_cast<StructType *>(type);
            for (size_t i = 0; i < structType->size(); ++i) {
                if (camel::core::type::isGCTraced(structType->codeAt(i))) {
                    collectStaticFunctionRefHandles(
                        refs,
                        st->get<slot_t>(i),
                        structType->typeAt(i),
                        sourceGraph,
                        visited);
                }
            }
            break;
        }
        default:
            break;
        }
    }

    void emitRuntimeGraphs() {
        if (emitted_) {
            return;
        }
        for (auto it = closure_.rbegin(); it != closure_.rend(); ++it) {
            const auto &sourceGraph = *it;
            auto depHandles         = collectDependencyHandles(sourceGraph);
            auto subGraphHandles    = collectSubGraphHandles(sourceGraph);
            auto dependencies       = mapRuntimeGraphs(depHandles);
            auto subGraphs          = mapRuntimeGraphs(subGraphHandles);
            auto runtimeSlots       = buildStaticSlots(sourceGraph);
            auto staticGraphRefs    = collectStaticGraphRefs(sourceGraph, runtimeSlots);
            const auto planIt       = payloadPlans_.find(sourceGraph.get());
            ASSERT(planIt != payloadPlans_.end(), "Compile payload plan must exist before emit.");
            const auto &payloadPlan = planIt->second;
            const auto payloadShape = describeNativePayload(payloadPlan);
            auto *graph             = camel::runtime::GCGraphBuildAccess::constructInPlace(
                runtimeGraphs_.at(sourceGraph.get()),
                allocatedBytes_.at(sourceGraph.get()),
                createGraphDebugRecord(
                    sourceGraph->stableId(),
                    sourceGraph->mangledName(),
                    sourceGraph->name()),
                sourceGraph->funcType(),
                const_cast<TupleType *>(sourceGraph->runtimeDataType()),
                const_cast<TupleType *>(sourceGraph->staticDataType()),
                const_cast<TupleType *>(sourceGraph->closureType()),
                runtimeOuterGraphOf(sourceGraph),
                dependencies,
                subGraphs,
                staticGraphRefs,
                payloadShape,
                [&](camel::runtime::GCGraphPayloadArena &payload) {
                    emitCompileGraphPayload(
                        sourceGraph,
                        payloadPlan,
                        payload,
                        [&](const Graph *target) -> GCGraph             *{
                            if (!target) {
                                return nullptr;
                            }
                            auto targetHandle = resolveGraphHandle(sourceGraph, target);
                            if (!targetHandle) {
                                targetHandle = requireSourceGraphHandle(
                                    const_cast<Graph *>(target),
                                    "Graph::encode(payload-target)");
                            }
                            return runtimeGraphs_.at(targetHandle.get());
                        });
                },
                runtimeSlots);
            runtimeGraphs_[sourceGraph.get()] = graph;
            camel::runtime::validateRuntimeGraphPayload(graph);
        }
        emitted_ = true;
    }

    std::vector<graph_ptr_t> closure_;
    std::unordered_map<const Graph *, GCGraph *> runtimeGraphs_;
    std::unordered_map<const Graph *, size_t> allocatedBytes_;
    std::unordered_map<const Graph *, NativePayloadPlan> payloadPlans_;
    bool allocated_ = false;
    bool emitted_   = false;
};

} // namespace

camel::runtime::GCGraph *Graph::encode() {
    GraphEncodeSession session;
    return session.encode(shared_from_this());
}

} // namespace camel::compile::gir
