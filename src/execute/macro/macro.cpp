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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "macro.h"

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
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include <format>
#include <optional>
#include <unordered_set>

namespace mm = camel::core::mm;

using namespace std;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using camel::compile::gir::data_idx_t;

namespace {

constexpr size_t kMaxMacroRecursionDepth = 64;
constexpr size_t kMacroFramePoolSize     = 8 * mm::MB;

class MacroExecutionError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;

inline data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
    const auto *node = graph ? graph->node(nodeRef) : nullptr;
    ASSERT(node != nullptr, "Macro runtime node lookup resolved to null.");
    return node->dataIndex;
}

inline slot_t getStaticRuntimeNodeSlot(const GCGraph *graph, gc_node_ref_t nodeRef) {
    ASSERT(graph != nullptr, "Macro static-slot lookup requires a runtime graph.");
    const auto *node = graph->node(nodeRef);
    ASSERT(node != nullptr, "Macro static-slot lookup requires a valid runtime node.");
    ASSERT(node->kind == GCNodeKind::Data, "Macro static-slot lookup requires a DATA node.");
    ASSERT(node->dataIndex < 0, "Macro static DATA node must use a negative static slot index.");
    return graph->staticArea()->get<slot_t>(static_cast<size_t>(-node->dataIndex));
}

inline bool areStaticRuntimeDataInputs(
    const GCGraph *graph, std::span<const gc_node_ref_t> inputs, size_t start = 0) {
    for (size_t i = start; i < inputs.size(); ++i) {
        const auto *inputNode = graph ? graph->node(inputs[i]) : nullptr;
        if (inputNode == nullptr || inputNode->kind != GCNodeKind::Data ||
            inputNode->dataIndex >= 0) {
            return false;
        }
    }
    return true;
}

inline bool isMacroFunction(const ::Function *funcObj) {
    return funcObj != nullptr && funcObj->graph() != nullptr && funcObj->graph()->isMacro();
}

inline std::string macroFunctionName(const ::Function *funcObj) {
    ASSERT(funcObj != nullptr, "Macro function is null.");
    auto *runtimeGraph = funcObj->graph();
    ASSERT(runtimeGraph != nullptr, "Macro function is missing runtime graph materialization.");
    return runtimeGraph->name();
}

inline void bindRuntimeGraphArgs(
    Frame *frame, camel::runtime::GCGraph *calleeGraph, camel::runtime::GCGraph *callerGraph,
    std::span<const gc_node_ref_t> withInputs, std::span<const gc_node_ref_t> normInputs) {
    ASSERT(frame != nullptr, "Macro runtime argument binding requires a frame.");
    ASSERT(calleeGraph != nullptr, "Macro runtime argument binding requires a callee graph.");
    ASSERT(callerGraph != nullptr, "Macro runtime argument binding requires a caller graph.");
    const auto runtimeWithPorts = calleeGraph->withPorts();
    const auto runtimeNormPorts = calleeGraph->normPorts();
    ASSERT(runtimeWithPorts.size() == withInputs.size(), "Runtime with-port count mismatch.");
    ASSERT(runtimeNormPorts.size() == normInputs.size(), "Runtime norm-port count mismatch.");
    for (size_t i = 0; i < runtimeWithPorts.size(); ++i) {
        const auto *portRecord = calleeGraph->node(runtimeWithPorts[i]);
        ASSERT(portRecord != nullptr, "Macro runtime with-port record is missing.");
        frame->set(portRecord->dataIndex, getStaticRuntimeNodeSlot(callerGraph, withInputs[i]));
    }
    for (size_t i = 0; i < runtimeNormPorts.size(); ++i) {
        const auto *portRecord = calleeGraph->node(runtimeNormPorts[i]);
        ASSERT(portRecord != nullptr, "Macro runtime norm-port record is missing.");
        frame->set(portRecord->dataIndex, getStaticRuntimeNodeSlot(callerGraph, normInputs[i]));
    }
}

class MacroExecutor {
  public:
    explicit MacroExecutor(const context_ptr_t &context)
        : context_(context), framePool_(kMacroFramePoolSize) {}

    std::optional<slot_t>
    tryExecute(GCGraph *runtimeGraph, gc_node_ref_t nodeRef, std::ostream &os) {
        try {
            const auto *node = runtimeGraph ? runtimeGraph->node(nodeRef) : nullptr;
            if (node == nullptr) {
                return std::nullopt;
            }
            switch (node->kind) {
            case GCNodeKind::Func:
                return tryExecuteDirectFunc(runtimeGraph, nodeRef, os);
            case GCNodeKind::Call:
                return tryExecuteIndirectCall(runtimeGraph, nodeRef, os);
            default:
                return std::nullopt;
            }
        } catch (const MacroExecutionError &e) {
            os << "[macro] skip " << runtimeGraph->name() << "::ref#" << nodeRef << ": " << e.what()
               << "\n";
            return std::nullopt;
        } catch (const Diagnostic &d) {
            (void)d;
            os << "[macro] diagnostic while evaluating " << runtimeGraph->name() << "::ref#"
               << nodeRef << "\n";
            return std::nullopt;
        } catch (const std::exception &e) {
            os << "[macro] exception while evaluating " << runtimeGraph->name() << "::ref#"
               << nodeRef << ": " << e.what() << "\n";
            return std::nullopt;
        }
    }

  private:
    context_ptr_t context_;
    FramePool framePool_;
    size_t recursionDepth_ = 0;

    bool macroCallsFunctionParam(GCGraph *graph) const {
        ASSERT(graph != nullptr, "Macro function-parameter analysis requires a runtime graph.");
        std::unordered_set<gc_node_ref_t> paramNodes;
        for (gc_node_ref_t ref : graph->withPorts())
            paramNodes.insert(ref);
        for (gc_node_ref_t ref : graph->normPorts())
            paramNodes.insert(ref);
        for (gc_node_ref_t ref : graph->closureNodes())
            paramNodes.insert(ref);
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const gc_node_ref_t nodeRef = it.ref();
            const auto *node            = *it;
            if (!node || node->kind != GCNodeKind::Call) {
                continue;
            }
            const auto withInputs = graph->withInputsOf(nodeRef);
            if (!withInputs.empty() && paramNodes.contains(withInputs.front())) {
                return true;
            }
        }
        return false;
    }

    std::optional<slot_t>
    tryExecuteDirectFunc(GCGraph *ownerGraph, gc_node_ref_t nodeRef, std::ostream &os) {
        const auto *node = ownerGraph ? ownerGraph->node(nodeRef) : nullptr;
        if (!node || node->kind != GCNodeKind::Func || !node->isMacro()) {
            return std::nullopt;
        }
        if (!areStaticRuntimeDataInputs(ownerGraph, ownerGraph->withInputsOf(nodeRef)) ||
            !areStaticRuntimeDataInputs(ownerGraph, ownerGraph->normInputsOf(nodeRef))) {
            return std::nullopt;
        }
        auto *calleeGraph = ownerGraph->directCalleeGraphOf(nodeRef);
        if (!calleeGraph || macroCallsFunctionParam(calleeGraph)) {
            return std::nullopt;
        }
        os << "[macro] execute direct macro " << calleeGraph->name() << "\n";
        return executeFunction(
            ::Function::create(calleeGraph, calleeGraph->closureType(), mm::autoSpace()),
            [&](Frame *frame, GCGraph *runtimeGraph) {
                bindRuntimeGraphArgs(
                    frame,
                    runtimeGraph,
                    ownerGraph,
                    ownerGraph->withInputsOf(nodeRef),
                    ownerGraph->normInputsOf(nodeRef));
            });
    }

    std::optional<slot_t>
    tryExecuteIndirectCall(GCGraph *ownerGraph, gc_node_ref_t nodeRef, std::ostream &os) {
        const auto *node = ownerGraph ? ownerGraph->node(nodeRef) : nullptr;
        if (!node || node->kind != GCNodeKind::Call) {
            return std::nullopt;
        }
        const auto withInputs = ownerGraph->withInputsOf(nodeRef);
        if (withInputs.empty()) {
            return std::nullopt;
        }
        const auto *calleeNode = ownerGraph->node(withInputs.front());
        if (!calleeNode || calleeNode->kind != GCNodeKind::Data || calleeNode->dataIndex >= 0) {
            return std::nullopt;
        }
        auto *funcObj =
            fromSlot<::Function *>(getStaticRuntimeNodeSlot(ownerGraph, withInputs.front()));
        if (!funcObj || !isMacroFunction(funcObj) || macroCallsFunctionParam(funcObj->graph())) {
            return std::nullopt;
        }
        if (!areStaticRuntimeDataInputs(ownerGraph, withInputs, 1) ||
            !areStaticRuntimeDataInputs(ownerGraph, ownerGraph->normInputsOf(nodeRef))) {
            return std::nullopt;
        }
        os << "[macro] execute indirect macro " << macroFunctionName(funcObj) << "\n";
        return executeFunction(funcObj, [&](Frame *frame, GCGraph *runtimeGraph) {
            std::vector<gc_node_ref_t> withArgs;
            withArgs.reserve(withInputs.size() - 1);
            for (size_t i = 1; i < withInputs.size(); ++i) {
                withArgs.push_back(withInputs[i]);
            }
            bindRuntimeGraphArgs(
                frame,
                runtimeGraph,
                ownerGraph,
                withArgs,
                ownerGraph->normInputsOf(nodeRef));
        });
    }

    template <typename FillArgs> slot_t executeFunction(::Function *funcObj, FillArgs &&fillArgs) {
        if (!funcObj) {
            throw MacroExecutionError("Macro callee is null.");
        }
        auto *runtimeGraph = funcObj->graph();
        ASSERT(runtimeGraph != nullptr, "Macro function must carry a runtime graph.");
        if (!runtimeGraph->isMacro()) {
            throw MacroExecutionError(
                std::format("'{}' is not marked as macro.", runtimeGraph->name()));
        }
        if (recursionDepth_ >= kMaxMacroRecursionDepth) {
            throw MacroExecutionError(
                std::format("Macro recursion depth exceeded at '{}'.", runtimeGraph->name()));
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

    slot_t executeGraph(Frame *frame, camel::runtime::GCGraph *runtimeGraph) {
        ASSERT(runtimeGraph != nullptr, "Macro runtime graph execution requires a runtime graph.");
        if (recursionDepth_ > kMaxMacroRecursionDepth) {
            throw MacroExecutionError(
                std::format("Macro recursion depth exceeded at '{}'.", runtimeGraph->name()));
        }
        auto runtimeTopoIndices = camel::execute::buildReachableExecutionTopoIndices(runtimeGraph);
        std::optional<uint32_t> tillRuntimeIndex;
        std::optional<uint32_t> skipRuntimeIndex;
        std::optional<uint32_t> joinRuntimeIndex;

        auto executeNode = [&](uint32_t runtimeNodeIndex) {
            const auto *node = runtimeGraph->node(runtimeNodeIndex);
            ASSERT(node != nullptr, "Macro execution encountered a null runtime node.");

            switch (node->kind) {
            case GCNodeKind::Cast: {
                const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
                ASSERT(!normInputs.empty(), "CAST node must have one norm input.");
                data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
                Type *srcType     = frame->typeAt<Type>(srcIdx);
                Type *dstType     = node->dataType;
                slot_t value      = frame->get<slot_t>(srcIdx);
                frame->set(node->dataIndex, dstType->castSlotFrom(value, srcType));
            } break;

            case GCNodeKind::Copy: {
                const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
                ASSERT(!normInputs.empty(), "COPY node must have one norm input.");
                data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
                TypeCode srcCode  = frame->codeAt(srcIdx);
                if (isGCTraced(srcCode)) {
                    Object *srcData  = frame->get<Object *>(srcIdx);
                    Type *srcTypePtr = frame->typeAt<Type>(srcIdx);
                    frame->set(node->dataIndex, srcData->clone(mm::autoSpace(), srcTypePtr, false));
                } else {
                    frame->set(node->dataIndex, frame->get<slot_t>(srcIdx));
                }
            } break;

            case GCNodeKind::Fill: {
                const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
                const auto dataInputs = runtimeGraph->withInputsOf(runtimeNodeIndex);
                ASSERT(!normInputs.empty(), "FILL node must have one source input.");
                data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
                TypeCode srcCode  = frame->codeAt(srcIdx);
                Type *srcType     = frame->typeAt<Type>(srcIdx);
                ASSERT(isGCTraced(srcCode), "FILL target type is not GC-traced.");
                Object *srcObj =
                    frame->get<Object *>(srcIdx)->clone(mm::autoSpace(), srcType, false);
                ASSERT(srcObj != nullptr, "FILL target data is null.");

                switch (srcCode) {
                case TypeCode::Tuple: {
                    auto *type         = tt::as_ptr<TupleType>(srcType);
                    auto *tuple        = tt::as_ptr<::Tuple>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        tuple->set<slot_t>(
                            refs[i],
                            frame->get<slot_t>(dataIndexOf(runtimeGraph, dataInputs[i])));
                    }
                } break;
                case TypeCode::Array: {
                    auto *array = tt::as_ptr<::Array>(srcObj);
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        array->set<slot_t>(
                            i,
                            frame->get<slot_t>(dataIndexOf(runtimeGraph, dataInputs[i])));
                    }
                } break;
                case TypeCode::Struct: {
                    auto *type         = tt::as_ptr<StructType>(srcType);
                    auto *st           = tt::as_ptr<::Struct>(srcObj);
                    const size_t *refs = type->refs();
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        st->set<slot_t>(
                            refs[i],
                            frame->get<slot_t>(dataIndexOf(runtimeGraph, dataInputs[i])));
                    }
                } break;
                case TypeCode::Function: {
                    auto *func  = tt::as_ptr<::Function>(srcObj);
                    auto *tuple = func->tuple();
                    for (size_t i = 0; i < dataInputs.size(); ++i) {
                        tuple->set<slot_t>(
                            i,
                            frame->get<slot_t>(dataIndexOf(runtimeGraph, dataInputs[i])));
                    }
                } break;
                default:
                    throw MacroExecutionError(
                        std::format("Unsupported FILL target type '{}'.", srcType->toString()));
                }
                frame->set(node->dataIndex, srcObj);
            } break;

            case GCNodeKind::Accs: {
                const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
                ASSERT(!normInputs.empty(), "ACCS node must have one source input.");
                data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
                const auto *body  = runtimeGraph->nodeBodyAs<GCAccsBody>(runtimeNodeIndex);
                if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
                    size_t idx  = body->value;
                    auto *tuple = frame->get<::Tuple *>(srcIdx);
                    frame->set(node->dataIndex, tuple->get<slot_t>(idx));
                } else {
                    auto key         = std::string(body->key());
                    auto *st         = frame->get<::Struct *>(srcIdx);
                    Type *structType = frame->typeAt<Type>(srcIdx);
                    frame->set(node->dataIndex, st->get<slot_t>(key, structType));
                }
            } break;

            case GCNodeKind::Brch: {
                const size_t jumpIdx =
                    camel::execute::selectRuntimeBranchArm(runtimeGraph, runtimeNodeIndex, frame);
                frame->set(node->dataIndex, fromSlot<Int32>(static_cast<Int32>(jumpIdx)));
            } break;

            case GCNodeKind::Join: {
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
                frame->set(node->dataIndex, branchData);
            } break;

            case GCNodeKind::Call: {
                data_idx_t calleeSlot = 0;
                const auto withInputs = runtimeGraph->withInputsOf(runtimeNodeIndex);
                ASSERT(!withInputs.empty(), "Runtime CALL node must expose a callee input.");
                const auto *calleeRecord = runtimeGraph->node(withInputs.front());
                ASSERT(calleeRecord != nullptr, "Runtime CALL callee record is missing.");
                calleeSlot          = calleeRecord->dataIndex;
                ::Function *funcObj = nullptr;
                bool calleeIsMacro  = false;
                if (calleeSlot < 0) {
                    funcObj = fromSlot<::Function *>(
                        getStaticRuntimeNodeSlot(runtimeGraph, withInputs.front()));
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
                    frame->set(node->dataIndex, result);
                } catch (...) {
                    if (recursionDepth_ > 0) {
                        recursionDepth_--;
                    }
                    framePool_.release(calleeFrame);
                    throw;
                }
            } break;

            case GCNodeKind::Func: {
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
                    frame->set(node->dataIndex, result);
                } catch (...) {
                    if (recursionDepth_ > 0) {
                        recursionDepth_--;
                    }
                    framePool_.release(calleeFrame);
                    throw;
                }
            } break;

            case GCNodeKind::Oper: {
                const auto *body = runtimeGraph->nodeBodyAs<GCOperBody>(runtimeNodeIndex);
                operator_t op    = body->op;
                if (!op) {
                    const auto uri = std::string(body->uri());
                    auto found     = context_->execMgr().find(uri);
                    if (!found) {
                        throw MacroExecutionError(
                            std::format("Operator '{}' is unavailable in macro execution.", uri));
                    }
                    op = *found;
                }

                const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
                const auto withInputs = runtimeGraph->withInputsOf(runtimeNodeIndex);
                std::vector<data_idx_t> indices;
                indices.reserve(normInputs.size() + withInputs.size());
                for (gc_node_ref_t in : normInputs) {
                    indices.push_back(dataIndexOf(runtimeGraph, in));
                }
                size_t normCount = indices.size();
                for (gc_node_ref_t in : withInputs) {
                    indices.push_back(dataIndexOf(runtimeGraph, in));
                }

                data_arr_t nargs{indices.data(), normCount};
                data_arr_t wargs{indices.data() + normCount, indices.size() - normCount};
                FrameArgsView withView(*frame, wargs);
                FrameArgsView normView(*frame, nargs);
                slot_t result = (*op)(withView, normView, *context_);
                frame->set(node->dataIndex, result);
            } break;

            case GCNodeKind::Data: {
                frame->set(
                    node->dataIndex,
                    getStaticRuntimeNodeSlot(runtimeGraph, runtimeNodeIndex));
            } break;

            case GCNodeKind::Port:
            case GCNodeKind::Sync:
            case GCNodeKind::Gate:
                break;

            default:
                throw MacroExecutionError(
                    std::format(
                        "Unsupported runtime node kind {} in macro execution.",
                        static_cast<int>(node->kind)));
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

            executeNode(runtimeNodeIndex);

            const auto *record = runtimeGraph->node(runtimeNodeIndex);
            if (record != nullptr && record->kind == camel::runtime::GCNodeKind::Brch) {
                size_t jumpIdx = static_cast<size_t>(frame->get<Int32>(record->dataIndex));
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

camel::runtime::GCGraph *MacroRewritePass::apply(camel::runtime::GCGraph *graph, ostream &os) {
    if (!graph) {
        return nullptr;
    }
    camel::runtime::RuntimeGraphDraftSession runtimeSession(context_, graph);

    MacroExecutor executor(context_);

    while (true) {
        bool roundChanged = false;
        const std::vector<camel::runtime::GCGraph *> runtimeGraphs =
            runtimeSession.collectReachableRuntimeGraphs();
        for (camel::runtime::GCGraph *runtimeGraph : runtimeGraphs) {
            if (!runtimeGraph) {
                continue;
            }
            camel::runtime::GraphDraft &draft = runtimeSession.edit(runtimeGraph);
            for (auto it = runtimeGraph->nodes().begin(); it != runtimeGraph->nodes().end(); ++it) {
                const camel::runtime::gc_node_ref_t runtimeNodeRef = it.ref();
                const auto *runtimeNode                            = *it;
                auto result = executor.tryExecute(runtimeGraph, runtimeNodeRef, os);
                if (!result.has_value()) {
                    continue;
                }

                const camel::runtime::gc_node_ref_t draftNodeId =
                    draft.draftIdOfSourceRef(runtimeNodeRef);
                ASSERT(
                    draftNodeId != camel::runtime::kInvalidNodeRef,
                    "Macro rewrite lost runtime-to-draft node mapping.");
                const camel::runtime::gc_node_ref_t newNodeId = draft.materializeStaticValue(
                    *result,
                    runtimeNode->dataType,
                    runtimeNode->flags);
                draft.replaceAllValueUses(draftNodeId, newNodeId);
                draft.replaceAllCtrlUses(draftNodeId, newNodeId);
                if (draft.outputNode() == draftNodeId) {
                    draft.setOutputNode(newNodeId);
                }
                if (draft.returnNode() == draftNodeId) {
                    draft.setReturnNode(newNodeId, camel::runtime::GCReturnKind::Self);
                }
                draft.eraseNode(draftNodeId);
                roundChanged = true;
                os << "[macro] rewrote " << runtimeGraph->name() << "::ref#" << runtimeNodeRef
                   << " -> draft#" << newNodeId << "\n";
            }
        }

        if (!roundChanged) {
            break;
        }
        graph = runtimeSession.commit();
        if (!graph) {
            return nullptr;
        }
        runtimeSession = camel::runtime::RuntimeGraphDraftSession(context_, graph);
    }

    return graph;
}
