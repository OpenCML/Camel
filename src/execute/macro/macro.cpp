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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "macro.h"

#include "camel/common/algo/topo.h"
#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/compile/gir/rewrite.h"
#include "camel/core/context/frame.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/executor.h"
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

inline ::Function *
makeRuntimeFunctionFromGraph(Graph *graph, camel::core::mm::IAllocator &allocator) {
    ASSERT(graph != nullptr, "Macro callee graph is null.");
    return ::Function::create(graph, graph->closureType(), allocator);
}

inline Graph *requireFinalizedGraphLayout(Graph *graph) {
    ASSERT(graph != nullptr, "Graph is null.");
    ASSERT(
        graph->finalized(),
        std::format("Graph '{}' must be sealed before macro execution.", graph->name()));
    ASSERT(
        graph->hasFrameLayout(),
        std::format("Graph '{}' has no finalized frame layout.", graph->name()));
    return graph;
}

inline slot_t getStaticNodeSlot(Node *node) {
    ASSERT(node != nullptr, "Node is null.");
    ASSERT(node->type() == NodeType::DATA, "Node is not static DATA.");
    auto *graph = requireFinalizedGraphLayout(&node->graph());
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

inline void fillFrameForDirectFunc(Frame *from, Frame *dest, Graph *graph, Node *node) {
    const auto &normNodes = node->normInputs();
    const auto &normPorts = graph->normPorts();
    ASSERT(normNodes.size() == normPorts.size(), "Norm nodes and ports count mismatch.");
    for (size_t i = 0; i < normNodes.size(); ++i) {
        dest->set(normPorts[i]->index(), from->get<slot_t>(normNodes[i]->index()));
    }

    const auto &withNodes = node->withInputs();
    const auto &withPorts = graph->withPorts();
    ASSERT(withNodes.size() == withPorts.size(), "With nodes and ports count mismatch.");
    for (size_t i = 0; i < withNodes.size(); ++i) {
        dest->set(withPorts[i]->index(), from->get<slot_t>(withNodes[i]->index()));
    }
}

inline void fillFrameForIndirectCall(Frame *from, Frame *dest, Graph *graph, Node *node) {
    const auto &normNodes = node->normInputs();
    const auto &normPorts = graph->normPorts();
    ASSERT(normNodes.size() == normPorts.size(), "Norm nodes and ports count mismatch.");
    for (size_t i = 0; i < normNodes.size(); ++i) {
        dest->set(normPorts[i]->index(), from->get<slot_t>(normNodes[i]->index()));
    }

    const auto &withNodes = node->withInputs();
    const auto &withPorts = graph->withPorts();
    ASSERT(withNodes.size() == withPorts.size() + 1, "With nodes and ports count mismatch.");
    for (size_t i = 0; i < withPorts.size(); ++i) {
        dest->set(withPorts[i]->index(), from->get<slot_t>(withNodes[i + 1]->index()));
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
            makeRuntimeFunctionFromGraph(node->bodyGraph(), mm::autoSpace()),
            [&](Frame *frame, Graph *graph) {
                const auto &withPorts = graph->withPorts();
                const auto &normPorts = graph->normPorts();
                ASSERT(
                    withPorts.size() == node->withInputs().size(),
                    "With nodes and ports count mismatch.");
                ASSERT(
                    normPorts.size() == node->normInputs().size(),
                    "Norm nodes and ports count mismatch.");
                for (size_t i = 0; i < withPorts.size(); ++i) {
                    frame->set(withPorts[i]->index(), getStaticNodeSlot(node->withInputs()[i]));
                }
                for (size_t i = 0; i < normPorts.size(); ++i) {
                    frame->set(normPorts[i]->index(), getStaticNodeSlot(node->normInputs()[i]));
                }
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
        if (!funcObj || !funcObj->graph()->isMacro()) {
            return std::nullopt;
        }
        if (!areStaticDataInputs(node->withInputs(), 1) ||
            !areStaticDataInputs(node->normInputs())) {
            return std::nullopt;
        }
        if (macroCallsFunctionParam(funcObj->graph())) {
            return std::nullopt;
        }
        os << "[macro] execute indirect macro " << funcObj->graph()->name() << "\n";
        return executeFunction(funcObj, [&](Frame *frame, Graph *graph) {
            const auto &withPorts = graph->withPorts();
            const auto &normPorts = graph->normPorts();
            ASSERT(
                withPorts.size() + 1 == node->withInputs().size(),
                "With nodes and ports count mismatch.");
            ASSERT(
                normPorts.size() == node->normInputs().size(),
                "Norm nodes and ports count mismatch.");
            for (size_t i = 0; i < withPorts.size(); ++i) {
                frame->set(withPorts[i]->index(), getStaticNodeSlot(node->withArg(i)));
            }
            for (size_t i = 0; i < normPorts.size(); ++i) {
                frame->set(normPorts[i]->index(), getStaticNodeSlot(node->normInputs()[i]));
            }
        });
    }

    template <typename FillArgs> slot_t executeFunction(::Function *funcObj, FillArgs &&fillArgs) {
        if (!funcObj) {
            throw MacroExecutionError("Macro callee is null.");
        }
        Graph *graph = funcObj->graph();
        if (!graph->isMacro()) {
            throw MacroExecutionError(std::format("'{}' is not marked as macro.", graph->name()));
        }
        if (recursionDepth_ >= kMaxMacroRecursionDepth) {
            throw MacroExecutionError(
                std::format("Macro recursion depth exceeded at '{}'.", graph->name()));
        }

        Frame *frame = framePool_.acquire(graph);
        try {
            fillArgs(frame, graph);
            if (graph->hasClosure()) {
                auto *closure            = funcObj->tuple();
                const auto &closureNodes = graph->closure();
                ASSERT(
                    closure != nullptr && closureNodes.size() == closure->size(),
                    "Closure tuple mismatch in macro execution.");
                for (size_t i = 0; i < closureNodes.size(); ++i) {
                    frame->set(closureNodes[i]->index(), closure->get<slot_t>(i));
                }
            }
            recursionDepth_++;
            slot_t result = executeGraph(graph, frame);
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

    std::vector<Node *> buildTopoNodes(Graph *graph) const {
        (void)requireFinalizedGraphLayout(graph);
        return findReachable(
            graph->exitNode(),
            [](Node *n) {
                node_vec_t ins;
                ins.reserve(n->dataInputs().size() + n->ctrlInputs().size());
                for (const auto &in : n->ctrlInputs()) {
                    if (&in->graph() == &n->graph()) {
                        ins.emplace_back(in);
                    }
                }
                for (const auto &in : n->dataInputs()) {
                    if (&in->graph() == &n->graph()) {
                        ins.emplace_back(in);
                    }
                }
                return ins;
            },
            false);
    }

    slot_t executeGraph(Graph *graph, Frame *frame) {
        if (recursionDepth_ > kMaxMacroRecursionDepth) {
            throw MacroExecutionError(
                std::format("Macro recursion depth exceeded at '{}'.", graph->name()));
        }
        auto topoNodes = buildTopoNodes(graph);
        Node *tillNode = nullptr, *skipNode = nullptr, *joinNode = nullptr;

        for (Node *node : topoNodes) {
            if (tillNode) {
                if (tillNode == node) {
                    tillNode = nullptr;
                } else {
                    continue;
                }
            }
            if (skipNode && skipNode == node) {
                skipNode = nullptr;
                tillNode = joinNode;
            }

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
                const auto &normIns = node->normInputs();
                const auto &withIns = node->withInputs();
                ASSERT(normIns.size() == 1, "BRCH node must have exactly one norm input.");

                size_t jumpIdx = 0;
                if (withIns.empty()) {
                    bool cond = frame->get<bool>(normIns.front()->index());
                    jumpIdx   = cond ? 0 : 1;
                } else {
                    TypeCode condType = frame->codeAt(normIns.front()->index());
                    size_t i          = 0;
                    if (isGCTraced(condType)) {
                        Type *condTypePtr = frame->typeAt<Type>(normIns.front()->index());
                        Object *condData  = frame->get<Object *>(normIns.front()->index());
                        for (; i < withIns.size(); ++i) {
                            Object *caseData = frame->get<Object *>(withIns[i]->index());
                            if (condData->equals(caseData, condTypePtr, false)) {
                                jumpIdx = i;
                                break;
                            }
                        }
                    } else {
                        slot_t condData = frame->get<slot_t>(normIns.front()->index());
                        for (; i < withIns.size(); ++i) {
                            if (condData == frame->get<slot_t>(withIns[i]->index())) {
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
                auto *brchNode   = tt::as_ptr<BrchNode>(node);
                auto *targetJoin = brchNode->matchedJoin();
                tillNode         = brchNode->armHead(jumpIdx);
                skipNode         = targetJoin->armTail(jumpIdx);
                joinNode         = targetJoin;
            } break;

            case NodeType::JOIN: {
                const auto &nargs = node->normInputs();
                const auto &wargs = node->withInputs();
                int32_t brIndex   = frame->get<int32_t>(nargs.front()->index());
                slot_t branchData =
                    frame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]->index());
                frame->set(node->index(), branchData);
            } break;

            case NodeType::CALL: {
                auto *callNode = tt::as_ptr<CallNode>(node);
                if (!callNode->hasCallee()) {
                    throw MacroExecutionError("CALL node has no callee.");
                }
                Node *calleeInput   = callNode->calleeInput();
                ::Function *funcObj = nullptr;
                bool calleeIsMacro  = false;
                if (calleeInput->type() == NodeType::DATA) {
                    funcObj = fromSlot<::Function *>(getStaticNodeSlot(calleeInput));
                    if (!funcObj) {
                        throw MacroExecutionError("CALL static callee is null.");
                    }
                    calleeIsMacro = funcObj->graph()->isMacro();
                } else {
                    funcObj = frame->get<::Function *>(calleeInput->index());
                    if (funcObj) {
                        calleeIsMacro = funcObj->graph()->isMacro();
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
                Graph *funcGraph   = funcObj->graph();
                Frame *calleeFrame = framePool_.acquire(funcGraph);
                try {
                    fillFrameForIndirectCall(frame, calleeFrame, funcGraph, node);
                    if (funcGraph->hasClosure()) {
                        auto *closure            = funcObj->tuple();
                        const auto &closureNodes = funcGraph->closure();
                        ASSERT(
                            closure != nullptr && closureNodes.size() == closure->size(),
                            "Closure tuple mismatch in macro CALL.");
                        for (size_t i = 0; i < closureNodes.size(); ++i) {
                            calleeFrame->set(closureNodes[i]->index(), closure->get<slot_t>(i));
                        }
                    }
                    recursionDepth_++;
                    slot_t result = executeGraph(funcGraph, calleeFrame);
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
                auto *funcNode     = tt::as_ptr<FuncNode>(node);
                Graph *funcGraph   = funcNode->bodyGraph();
                Frame *calleeFrame = framePool_.acquire(funcGraph);
                try {
                    fillFrameForDirectFunc(frame, calleeFrame, funcGraph, node);
                    recursionDepth_++;
                    slot_t result = executeGraph(funcGraph, calleeFrame);
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
        }

        Node *outputNode = graph->outputNode();
        return frame->get<slot_t>(outputNode->index());
    }
};

std::vector<graph_ptr_t> collectAllGraphs(const graph_ptr_t &root) {
    std::vector<graph_ptr_t> result;
    std::unordered_set<Graph *> visited;
    std::function<void(const graph_ptr_t &)> dfs = [&](const graph_ptr_t &graph) {
        if (!graph || !visited.insert(graph.get()).second) {
            return;
        }
        result.push_back(graph);
        for (auto &[_, subGraphs] : graph->subGraphs()) {
            for (const auto &subGraph : subGraphs) {
                dfs(subGraph);
            }
        }
        for (const auto &dep : graph->dependencies()) {
            dfs(dep);
        }
    };
    dfs(root);
    return result;
}

Node *materializeMacroResult(
    GraphRewriteSession &session, const graph_ptr_t &owner, slot_t resultSlot, Type *resultType) {
    ASSERT(owner != nullptr, "Owner graph is null.");
    if (resultType->code() == TypeCode::Function) {
        auto *funcObj = fromSlot<::Function *>(resultSlot);
        if (funcObj && funcObj->graph() != owner.get()) {
            session.addDependency(owner, funcObj->graph()->shared_from_this());
        }
    }
    Node *node = DataNode::createStaticSlot(*owner, resultType, resultSlot);
    if (resultType->code() == TypeCode::Function) {
        auto *funcObj = fromSlot<::Function *>(resultSlot);
        if (funcObj && funcObj->graph()->isMacro()) {
            detail::NodeMutation::setMacro(node, true);
        }
    }
    return node;
}

} // namespace

graph_ptr_t MacroRewritePass::apply(graph_ptr_t &graph, ostream &os) {
    if (!graph) {
        return graph;
    }

    GraphRewriteSession session(graph);
    graph_ptr_t workingRoot = session.root();
    MacroExecutor executor(context_);
    bool changed = true;
    while (changed) {
        changed     = false;
        auto graphs = collectAllGraphs(workingRoot);
        for (const auto &currGraph : graphs) {
            auto nodes = currGraph->nodes();
            for (Node *node : nodes) {
                if (std::find(currGraph->nodes().begin(), currGraph->nodes().end(), node) ==
                    currGraph->nodes().end()) {
                    continue;
                }

                auto result = executor.tryExecute(node, os);
                if (!result.has_value()) {
                    continue;
                }

                Node *newNode =
                    materializeMacroResult(session, currGraph, *result, node->dataType());
                session.replaceNode(node, newNode);
                changed = true;
                os << "[macro] rewrote " << node->debugEntityId() << " -> "
                   << newNode->debugEntityId() << "\n";
            }
        }
    }

    auto result = session.finish();
    graph       = result.graph;
    return graph;
}
