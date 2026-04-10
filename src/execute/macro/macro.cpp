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

#include "macro.h"

#include "camel/common/algo/topo.h"
#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/rewrite.h"
#include <format>
#include <optional>
#include <unordered_set>

namespace mm = camel::core::mm;

using namespace std;
using namespace GIR;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

constexpr size_t kMaxMacroRecursionDepth = 64;
constexpr size_t kMacroFramePoolSize     = 8 * mm::MB;

class MacroExecutionError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

inline camel::runtime::GCGraph *findRuntimeGraph(const context_ptr_t &context, Graph *graph) {
    if (!context || !graph) {
        return nullptr;
    }
    return context->runtimeGraphManager().find(graph);
}

inline ::Function *makeRuntimeFunctionFromGraph(
    const context_ptr_t &context, Graph *graph, camel::core::mm::IAllocator &allocator) {
    ASSERT(graph != nullptr, "Macro callee graph is null.");
    auto *runtimeGraph = findRuntimeGraph(context, graph);
    ASSERT(
        runtimeGraph != nullptr,
        std::format(
            "Macro runtime execution requires graph '{}' to be materialized before pass execution.",
            graph->name()));
    return ::Function::create(runtimeGraph, runtimeGraph->closureType(), allocator);
}

inline slot_t getStaticNodeSlot(Node *node) {
    ASSERT(node != nullptr, "Node is null.");
    ASSERT(node->type() == NodeType::DATA, "Node is not static DATA.");
    auto *graph = &node->graph();
    ASSERT(
        graph->finalized(),
        std::format("Graph '{}' must be sealed before macro execution.", graph->name()));
    ASSERT(
        graph->hasFrameLayout(),
        std::format("Graph '{}' has no finalized frame layout.", graph->name()));
    return graph->staticArea()->get<slot_t>(static_cast<size_t>(-node->index()));
}

inline bool areStaticDataInputs(node_span_t inputs, size_t start = 0) {
    for (size_t i = start; i < inputs.size(); ++i) {
        if (inputs[i] == nullptr || inputs[i]->type() != NodeType::DATA) {
            return false;
        }
    }
    return true;
}

inline bool isMacroFunction(const ::Function *funcObj) {
    return funcObj != nullptr && funcObj->graph() != nullptr && funcObj->graph()->isMacro();
}

inline Graph *macroFunctionMetadataGraph(const ::Function *funcObj) {
    ASSERT(funcObj != nullptr, "Macro function is null.");
    auto *runtimeGraph = funcObj->graph();
    ASSERT(runtimeGraph != nullptr, "Macro function is missing runtime graph materialization.");
    auto *graph = runtimeGraph->sourceGraph().get();
    ASSERT(graph != nullptr, "Macro function is missing compile graph metadata.");
    return graph;
}

inline std::string macroFunctionName(const ::Function *funcObj) {
    ASSERT(funcObj != nullptr, "Macro function is null.");
    auto *runtimeGraph = funcObj->graph();
    ASSERT(runtimeGraph != nullptr, "Macro function is missing runtime graph materialization.");
    return runtimeGraph->name();
}

inline void bindRuntimeGraphArgs(
    Frame *frame, camel::runtime::GCGraph *runtimeGraph, node_span_t withInputs,
    node_span_t normInputs) {
    ASSERT(frame != nullptr, "Macro runtime argument binding requires a frame.");
    ASSERT(runtimeGraph != nullptr, "Macro runtime argument binding requires a runtime graph.");
    const auto runtimeWithPorts = runtimeGraph->withPorts();
    const auto runtimeNormPorts = runtimeGraph->normPorts();
    ASSERT(runtimeWithPorts.size() == withInputs.size(), "Runtime with-port count mismatch.");
    ASSERT(runtimeNormPorts.size() == normInputs.size(), "Runtime norm-port count mismatch.");
    for (size_t i = 0; i < runtimeWithPorts.size(); ++i) {
        const auto *portRecord = runtimeGraph->node(runtimeWithPorts[i]);
        ASSERT(portRecord != nullptr, "Macro runtime with-port record is missing.");
        frame->set(portRecord->dataIndex, getStaticNodeSlot(withInputs[i]));
    }
    for (size_t i = 0; i < runtimeNormPorts.size(); ++i) {
        const auto *portRecord = runtimeGraph->node(runtimeNormPorts[i]);
        ASSERT(portRecord != nullptr, "Macro runtime norm-port record is missing.");
        frame->set(portRecord->dataIndex, getStaticNodeSlot(normInputs[i]));
    }
}

class MacroExecutor {
  public:
    explicit MacroExecutor(const context_ptr_t &context)
        : context_(context), framePool_(kMacroFramePoolSize) {}

    std::optional<slot_t> tryExecute(Node *node, std::ostream &os) {
        try {
            if (node == nullptr) {
                return std::nullopt;
            }
            switch (node->type()) {
            case NodeType::FUNC:
                return tryExecuteDirectFunc(tt::as_ptr<FuncNode>(node), os);
            case NodeType::CALL:
                return tryExecuteIndirectCall(tt::as_ptr<CallNode>(node), os);
            default:
                return std::nullopt;
            }
        } catch (const MacroExecutionError &e) {
            os << "[macro] skip " << node->debugEntityId() << ": " << e.what() << "\n";
            return std::nullopt;
        } catch (const Diagnostic &d) {
            (void)d;
            os << "[macro] diagnostic while evaluating " << node->debugEntityId() << "\n";
            return std::nullopt;
        } catch (const std::exception &e) {
            os << "[macro] exception while evaluating " << node->debugEntityId() << ": " << e.what()
               << "\n";
            return std::nullopt;
        }
    }

  private:
    context_ptr_t context_;
    FramePool framePool_;
    size_t recursionDepth_ = 0;

    bool macroCallsFunctionParam(Graph *graph) const {
        ASSERT(graph != nullptr, "Macro function-parameter analysis requires a graph.");
        std::unordered_set<Node *> paramNodes;
        for (Node *p : graph->withPorts())
            paramNodes.insert(p);
        for (Node *p : graph->normPorts())
            paramNodes.insert(p);
        for (Node *p : graph->closure())
            paramNodes.insert(p);
        for (Node *n : graph->nodes()) {
            if (n->type() != NodeType::CALL || !tt::as_ptr<CallNode>(n)->hasCallee())
                continue;
            if (paramNodes.count(tt::as_ptr<CallNode>(n)->calleeInput()))
                return true;
        }
        return false;
    }

    std::optional<slot_t> tryExecuteDirectFunc(FuncNode *node, std::ostream &os) {
        if (!node || !node->isMacro()) {
            return std::nullopt;
        }
        if (!areStaticDataInputs(node->withInputs()) || !areStaticDataInputs(node->normInputs())) {
            return std::nullopt;
        }
        if (macroCallsFunctionParam(node->bodyGraph())) {
            return std::nullopt;
        }
        os << "[macro] execute direct macro " << node->bodyGraph()->name() << "\n";
        return executeFunction(
            makeRuntimeFunctionFromGraph(context_, node->bodyGraph(), mm::autoSpace()),
            [&](Frame *frame, camel::runtime::GCGraph *runtimeGraph) {
                bindRuntimeGraphArgs(frame, runtimeGraph, node->withInputs(), node->normInputs());
            });
    }

    std::optional<slot_t> tryExecuteIndirectCall(CallNode *node, std::ostream &os) {
        if (!node || !node->hasCallee()) {
            return std::nullopt;
        }
        Node *calleeNode = node->calleeInput();
        if (calleeNode->type() != NodeType::DATA) {
            return std::nullopt;
        }
        auto *funcObj = fromSlot<::Function *>(getStaticNodeSlot(calleeNode));
        if (!funcObj || !isMacroFunction(funcObj)) {
            return std::nullopt;
        }
        if (!areStaticDataInputs(node->withInputs(), 1) ||
            !areStaticDataInputs(node->normInputs())) {
            return std::nullopt;
        }
        auto *macroGraph = macroFunctionMetadataGraph(funcObj);
        ASSERT(macroGraph != nullptr, "Indirect macro callee is missing graph metadata.");
        if (macroCallsFunctionParam(macroGraph)) {
            return std::nullopt;
        }
        os << "[macro] execute indirect macro " << macroFunctionName(funcObj) << "\n";
        return executeFunction(funcObj, [&](Frame *frame, camel::runtime::GCGraph *runtimeGraph) {
            node_vec_t withArgs;
            withArgs.reserve(node->withInputs().size() - 1);
            for (size_t i = 1; i < node->withInputs().size(); ++i) {
                withArgs.push_back(node->withInputs()[i]);
            }
            bindRuntimeGraphArgs(frame, runtimeGraph, withArgs, node->normInputs());
        });
    }

    template <typename FillArgs> slot_t executeFunction(::Function *funcObj, FillArgs &&fillArgs) {
        if (!funcObj) {
            throw MacroExecutionError("Macro callee is null.");
        }
        auto *runtimeGraph = funcObj->graph();
        Graph *graph       = macroFunctionMetadataGraph(funcObj);
        ASSERT(runtimeGraph != nullptr, "Macro function must carry a runtime graph.");
        if (!runtimeGraph->isMacro()) {
            throw MacroExecutionError(
                std::format("'{}' is not marked as macro.", runtimeGraph->name()));
        }
        if (recursionDepth_ >= kMaxMacroRecursionDepth) {
            throw MacroExecutionError(
                std::format("Macro recursion depth exceeded at '{}'.", graph->name()));
        }

        Frame *frame = framePool_.acquire(runtimeGraph);
        try {
            fillArgs(frame, runtimeGraph);
            if (!runtimeGraph->closureNodes().empty()) {
                auto *closure           = funcObj->tuple();
                const auto closureNodes = runtimeGraph->closureNodes();
                ASSERT(
                    closure != nullptr && closureNodes.size() == closure->size(),
                    "Runtime closure tuple mismatch in macro execution.");
                for (size_t i = 0; i < closureNodes.size(); ++i) {
                    const auto *closureRecord = runtimeGraph->node(closureNodes[i]);
                    ASSERT(closureRecord != nullptr, "Macro runtime closure record is missing.");
                    frame->set(closureRecord->dataIndex, closure->get<slot_t>(i));
                }
            }
            recursionDepth_++;
            slot_t result = executeGraph(frame, runtimeGraph);
            recursionDepth_--;
            framePool_.release(frame);
            return result;
        } catch (...) {
            if (recursionDepth_ > 0) {
                recursionDepth_--;
            }
            framePool_.release(frame);
            throw;
        }
    }

    static Node *findSourceNodeByIndex(GIR::Graph *graph, GIR::data_idx_t index) {
        if (!graph || index == 0) {
            return nullptr;
        }
        auto tryNode = [index](Node *node) -> Node * {
            return (node && node->index() == index) ? node : nullptr;
        };
        for (Node *node : graph->normPorts()) {
            if (auto *found = tryNode(node)) {
                return found;
            }
        }
        for (Node *node : graph->withPorts()) {
            if (auto *found = tryNode(node)) {
                return found;
            }
        }
        for (Node *node : graph->closure()) {
            if (auto *found = tryNode(node)) {
                return found;
            }
        }
        for (Node *node : graph->nodes()) {
            if (auto *found = tryNode(node)) {
                return found;
            }
        }
        if (auto *found = tryNode(graph->outputNode())) {
            return found;
        }
        if (auto *found = tryNode(graph->exitNode())) {
            return found;
        }
        return nullptr;
    }

    static Node *requireSourceNode(
        const camel::runtime::GCGraph *runtimeGraph,
        camel::runtime::gc_node_ref_t runtimeNodeIndex) {
        ASSERT(runtimeGraph != nullptr, "Macro runtime source-node lookup requires a graph.");
        const auto *record = runtimeGraph->node(runtimeNodeIndex);
        ASSERT(record != nullptr, "Macro runtime source-node lookup requires a node record.");
        auto *sourceGraph = runtimeGraph->sourceGraph().get();
        Node *sourceNode  = findSourceNodeByIndex(sourceGraph, record->dataIndex);
        ASSERT(
            sourceNode != nullptr,
            std::format(
                "Runtime graph '{}' is missing source node metadata for runtime node {}.",
                runtimeGraph->name(),
                runtimeNodeIndex));
        return sourceNode;
    }

    static const camel::runtime::GCNode &
    requireRuntimeRecord(const camel::runtime::GCGraph *runtimeGraph, uint32_t runtimeNodeIndex) {
        ASSERT(runtimeGraph != nullptr, "Macro runtime node-record lookup requires a graph.");
        const auto *record = runtimeGraph->node(runtimeNodeIndex);
        ASSERT(record != nullptr, "Macro runtime node-record lookup resolved to null.");
        return *record;
    }

    slot_t executeGraph(Frame *frame, camel::runtime::GCGraph *runtimeGraph) {
        ASSERT(runtimeGraph != nullptr, "Macro runtime graph execution requires a runtime graph.");
        ASSERT(
            runtimeGraph->sourceGraph() != nullptr,
            "Macro runtime graph is missing compile metadata.");
        if (recursionDepth_ > kMaxMacroRecursionDepth) {
            throw MacroExecutionError(
                std::format("Macro recursion depth exceeded at '{}'.", runtimeGraph->name()));
        }
        auto runtimeTopoIndices = camel::execute::buildReachableExecutionTopoIndices(runtimeGraph);
        std::optional<uint32_t> tillRuntimeIndex;
        std::optional<uint32_t> skipRuntimeIndex;
        std::optional<uint32_t> joinRuntimeIndex;

        auto executeNode = [&](Node *node, uint32_t runtimeNodeIndex) {
            ASSERT(node != nullptr, "Macro execution encountered a null node.");

            switch (node->type()) {
            case NodeType::CAST: {
                const auto &inputNode = node->normInputs().front();
                Type *srcType         = frame->typeAt<Type>(inputNode->index());
                Type *dstType         = node->dataType();
                slot_t value          = frame->get<slot_t>(inputNode->index());
                frame->set(node->index(), dstType->castSlotFrom(value, srcType));
            } break;

            case NodeType::COPY: {
                const auto &inputNode = node->normInputs().front();
                data_idx_t srcIdx     = inputNode->index();
                TypeCode srcCode      = frame->codeAt(srcIdx);
                if (isGCTraced(srcCode)) {
                    Object *srcData  = frame->get<Object *>(srcIdx);
                    Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
                    frame->set(node->index(), srcData->clone(mm::autoSpace(), srcTypePtr, false));
                } else {
                    frame->set(node->index(), frame->get<slot_t>(srcIdx));
                }
            } break;

            case NodeType::FILL: {
                const auto &srcNode    = node->normInputs().front();
                const auto &dataInputs = node->withInputs();
                TypeCode srcCode       = frame->codeAt(srcNode->index());
                Type *srcType          = frame->typeAt<Type>(srcNode->index());
                ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced.");
                Object *srcObj =
                    frame->get<Object *>(srcNode->index())->clone(mm::autoSpace(), srcType, false);
                ASSERT(srcObj != nullptr, "FILL target data is null.");

                switch (srcCode) {
                case TypeCode::Tuple: {
                    auto *type         = tt::as_ptr<TupleType>(srcType);
                    auto *tuple        = tt::as_ptr<::Tuple>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        tuple->set<slot_t>(refs[i], frame->get<slot_t>(dataInputs[i]->index()));
                    }
                } break;
                case TypeCode::Array: {
                    auto *array = tt::as_ptr<::Array>(srcObj);
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        array->set<slot_t>(i, frame->get<slot_t>(dataInputs[i]->index()));
                    }
                } break;
                case TypeCode::Struct: {
                    auto *type         = tt::as_ptr<StructType>(srcType);
                    auto *st           = tt::as_ptr<::Struct>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        st->set<slot_t>(refs[i], frame->get<slot_t>(dataInputs[i]->index()));
                    }
                } break;
                case TypeCode::Function: {
                    auto *func  = tt::as_ptr<::Function>(srcObj);
                    auto *tuple = func->tuple();
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        tuple->set<slot_t>(i, frame->get<slot_t>(dataInputs[i]->index()));
                    }
                } break;
                default:
                    throw MacroExecutionError(
                        std::format("Unsupported FILL target type '{}'.", srcType->toString()));
                }
                frame->set(node->index(), srcObj);
            } break;

            case NodeType::ACCS: {
                auto *accsNode    = tt::as_ptr<AccsNode>(node);
                data_idx_t srcIdx = node->dataInputs().front()->index();
                if (accsNode->isNum()) {
                    size_t idx  = accsNode->numIndex();
                    auto *tuple = frame->get<::Tuple *>(srcIdx);
                    frame->set(node->index(), tuple->get<slot_t>(idx));
                } else {
                    auto key         = accsNode->strIndex();
                    auto *st         = frame->get<::Struct *>(srcIdx);
                    Type *structType = frame->typeAt<Type>(srcIdx);
                    frame->set(node->index(), st->get<slot_t>(key, structType));
                }
            } break;

            case NodeType::BRCH: {
                std::vector<data_idx_t> normIns;
                std::vector<data_idx_t> withIns;
                for (uint32_t inputIndex : runtimeGraph->normInputsOf(runtimeNodeIndex)) {
                    const auto *inputRecord = runtimeGraph->node(inputIndex);
                    ASSERT(
                        inputRecord != nullptr,
                        "Macro runtime BRCH norm input record is missing.");
                    normIns.push_back(inputRecord->dataIndex);
                }
                for (uint32_t inputIndex : runtimeGraph->withInputsOf(runtimeNodeIndex)) {
                    const auto *inputRecord = runtimeGraph->node(inputIndex);
                    ASSERT(
                        inputRecord != nullptr,
                        "Macro runtime BRCH with input record is missing.");
                    withIns.push_back(inputRecord->dataIndex);
                }
                ASSERT(normIns.size() == 1, "BRCH node must have exactly one norm input.");

                size_t jumpIdx = 0;
                if (withIns.empty()) {
                    bool cond = frame->get<bool>(normIns.front());
                    jumpIdx   = cond ? 0 : 1;
                } else {
                    TypeCode condType = frame->codeAt(normIns.front());
                    size_t i          = 0;
                    if (isGCTraced(condType)) {
                        Type *condTypePtr = frame->typeAt<Type>(normIns.front());
                        Object *condData  = frame->get<Object *>(normIns.front());
                        for (; i < withIns.size(); ++i) {
                            Object *caseData = frame->get<Object *>(withIns[i]);
                            if (condData->equals(caseData, condTypePtr, false)) {
                                jumpIdx = i;
                                break;
                            }
                        }
                    } else {
                        slot_t condData = frame->get<slot_t>(normIns.front());
                        for (; i < withIns.size(); ++i) {
                            if (condData == frame->get<slot_t>(withIns[i])) {
                                jumpIdx = i;
                                break;
                            }
                        }
                    }
                    if (i == withIns.size()) {
                        jumpIdx = withIns.size();
                    }
                }

                frame->set(node->index(), fromSlot<Int32>(static_cast<Int32>(jumpIdx)));
            } break;

            case NodeType::JOIN: {
                std::vector<data_idx_t> nargs;
                std::vector<data_idx_t> wargs;
                for (uint32_t inputIndex : runtimeGraph->normInputsOf(runtimeNodeIndex)) {
                    const auto *inputRecord = runtimeGraph->node(inputIndex);
                    ASSERT(
                        inputRecord != nullptr,
                        "Macro runtime JOIN norm input record is missing.");
                    nargs.push_back(inputRecord->dataIndex);
                }
                for (uint32_t inputIndex : runtimeGraph->withInputsOf(runtimeNodeIndex)) {
                    const auto *inputRecord = runtimeGraph->node(inputIndex);
                    ASSERT(
                        inputRecord != nullptr,
                        "Macro runtime JOIN with input record is missing.");
                    wargs.push_back(inputRecord->dataIndex);
                }
                int32_t brIndex   = frame->get<int32_t>(nargs.front());
                slot_t branchData = frame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
                frame->set(node->index(), branchData);
            } break;

            case NodeType::CALL: {
                data_idx_t calleeSlot = 0;
                const auto withInputs = runtimeGraph->withInputsOf(runtimeNodeIndex);
                ASSERT(!withInputs.empty(), "Runtime CALL node must expose a callee input.");
                const auto *calleeRecord = runtimeGraph->node(withInputs.front());
                ASSERT(calleeRecord != nullptr, "Runtime CALL callee record is missing.");
                calleeSlot          = calleeRecord->dataIndex;
                ::Function *funcObj = nullptr;
                bool calleeIsMacro  = false;
                if (calleeSlot < 0) {
                    funcObj = fromSlot<::Function *>(frame->get<slot_t>(calleeSlot));
                    if (!funcObj) {
                        throw MacroExecutionError("CALL static callee is null.");
                    }
                    calleeIsMacro = isMacroFunction(funcObj);
                } else {
                    funcObj = frame->get<::Function *>(calleeSlot);
                    if (funcObj) {
                        calleeIsMacro = isMacroFunction(funcObj);
                    }
                }
                if (!funcObj) {
                    throw MacroExecutionError("CALL callee is null.");
                }
                if (!calleeIsMacro) {
                    throw MacroExecutionError(
                        "Macro cannot call non-macro function at compile time (use runtime "
                        "instead).");
                }
                auto *calleeRuntimeGraph = funcObj->graph();
                ASSERT(
                    calleeRuntimeGraph != nullptr,
                    "Macro CALL callee is missing runtime graph.");
                Frame *calleeFrame = framePool_.acquire(calleeRuntimeGraph);
                try {
                    camel::execute::fillFrameForIndirectCall(
                        frame,
                        calleeFrame,
                        runtimeGraph,
                        runtimeNodeIndex);
                    recursionDepth_++;
                    slot_t result = executeGraph(calleeFrame, calleeRuntimeGraph);
                    recursionDepth_--;
                    framePool_.release(calleeFrame);
                    frame->set(node->index(), result);
                } catch (...) {
                    if (recursionDepth_ > 0) {
                        recursionDepth_--;
                    }
                    framePool_.release(calleeFrame);
                    throw;
                }
            } break;

            case NodeType::FUNC: {
                auto *calleeRuntimeGraph = runtimeGraph->directCalleeGraphOf(runtimeNodeIndex);
                ASSERT(
                    calleeRuntimeGraph != nullptr,
                    "Runtime FUNC node must have a direct runtime callee graph.");
                Frame *calleeFrame = framePool_.acquire(calleeRuntimeGraph);
                try {
                    camel::execute::fillFrameForDirectInvoke(
                        frame,
                        calleeFrame,
                        runtimeGraph,
                        runtimeNodeIndex);
                    recursionDepth_++;
                    slot_t result = executeGraph(calleeFrame, calleeRuntimeGraph);
                    recursionDepth_--;
                    framePool_.release(calleeFrame);
                    frame->set(node->index(), result);
                } catch (...) {
                    if (recursionDepth_ > 0) {
                        recursionDepth_--;
                    }
                    framePool_.release(calleeFrame);
                    throw;
                }
            } break;

            case NodeType::OPER: {
                auto *operNode = tt::as_ptr<OperNode>(node);
                operator_t op  = operNode->getCachedOp();
                if (!op) {
                    const auto &uri = operNode->oper()->uri();
                    auto found      = context_->execMgr().find(uri);
                    if (!found) {
                        throw MacroExecutionError(
                            std::format("Operator '{}' is unavailable in macro execution.", uri));
                    }
                    op = *found;
                    operNode->setCachedOp(op);
                }

                std::vector<data_idx_t> indices;
                indices.reserve(node->normInputs().size() + node->withInputs().size());
                for (const auto &in : node->normInputs()) {
                    indices.push_back(in->index());
                }
                size_t normCount = indices.size();
                for (const auto &in : node->withInputs()) {
                    indices.push_back(in->index());
                }

                data_arr_t nargs{indices.data(), normCount};
                data_arr_t wargs{indices.data() + normCount, indices.size() - normCount};
                FrameArgsView withView(*frame, wargs);
                FrameArgsView normView(*frame, nargs);
                slot_t result = (*op)(withView, normView, *context_);
                frame->set(node->index(), result);
            } break;

            case NodeType::DATA: {
                frame->set(node->index(), getStaticNodeSlot(node));
            } break;

            case NodeType::PORT:
            case NodeType::SYNC:
            case NodeType::GATE:
                break;

            default:
                throw MacroExecutionError(
                    std::format("Unsupported node '{}' in macro execution.", node->toString()));
            }
        };

        for (uint32_t runtimeNodeIndex : runtimeTopoIndices) {
            if (tillRuntimeIndex.has_value()) {
                if (*tillRuntimeIndex == runtimeNodeIndex) {
                    tillRuntimeIndex.reset();
                } else {
                    continue;
                }
            }
            if (skipRuntimeIndex.has_value() && *skipRuntimeIndex == runtimeNodeIndex) {
                skipRuntimeIndex.reset();
                tillRuntimeIndex = joinRuntimeIndex;
            }

            Node *node = requireSourceNode(runtimeGraph, runtimeNodeIndex);
            executeNode(node, runtimeNodeIndex);

            const auto *record = runtimeGraph->node(runtimeNodeIndex);
            if (record != nullptr && record->kind == camel::runtime::GCNodeKind::Brch) {
                size_t jumpIdx = static_cast<size_t>(frame->get<Int32>(node->index()));
                auto armRegion = camel::execute::collectRuntimeBranchArmRegion(
                    runtimeGraph,
                    runtimeNodeIndex,
                    jumpIdx);
                tillRuntimeIndex = armRegion.headIndex;
                joinRuntimeIndex = armRegion.joinIndex;
                skipRuntimeIndex = armRegion.tailIndex;
            }
        }
        return camel::execute::readRuntimeGraphReturn(runtimeGraph, frame);
    }
};

} // namespace

graph_ptr_t MacroRewritePass::apply(camel::runtime::GCGraph *graph, ostream &os) {
    if (!graph) {
        return Graph::null();
    }

    camel::runtime::RuntimeGraphRewriteSession runtimeSession(context_, graph);

    MacroExecutor executor(context_);
    graph_ptr_t currentRoot = runtimeSession.sourceRoot();

    while (true) {
        auto session                                 = runtimeSession.createSourceEditSession();
        const std::vector<graph_ptr_t> &sourceGraphs = session.reachableSourceGraphs();

        bool roundChanged = false;
        for (const auto &sourceGraph : sourceGraphs) {
            for (Node *sourceNode : sourceGraph->nodes()) {
                auto result = executor.tryExecute(sourceNode, os);
                if (!result.has_value()) {
                    continue;
                }

                Node *draftNode = session.canonicalNode(sourceNode);
                ASSERT(draftNode != nullptr, "Macro rewrite lost source-to-draft node mapping.");
                graph_ptr_t draftOwner = draftNode->graph().shared_from_this();
                Node *newNode          = session.materializeStaticValue(
                    draftOwner,
                    *result,
                    sourceNode->dataType(),
                    camel::runtime::RuntimeStaticValueMaterializationOptions{
                        .propagateMacro = true});
                session.replaceNode(draftNode, newNode);
                roundChanged = true;
                os << "[macro] rewrote " << sourceNode->debugEntityId() << " -> "
                   << sourceNode->graph().name() << "::node#" << newNode->index() << "\n";
            }
        }

        if (!roundChanged) {
            break;
        }

        currentRoot = session.finish();
        (void)runtimeSession.rematerialize(currentRoot);
    }

    return runtimeSession.finish(currentRoot);
}
