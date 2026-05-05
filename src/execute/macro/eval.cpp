/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Macro compile-time evaluator.
 *
 * The evaluator borrows runtime traversal and operator dispatch rules, but it
 * does not execute against the source graph's mutable static area. Each frame
 * receives a private static-area snapshot so closure binding and negative-slot
 * writes cannot leak back into the source runtime closure.
 */

#include "runtime.h"

#include "camel/core/mm.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/executor.h"
#include "camel/execute/graph_runtime_support.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <unordered_set>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace mm = camel::core::mm;

using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace camel::execute::macro_runtime {

namespace {

constexpr size_t kMaxMacroRecursionDepth = 64;
constexpr size_t kMacroFramePoolSize     = 8 * mm::MB;

class MacroExecutionError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

using camel::runtime::gc_data_idx_t;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCAccsBody;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;

inline gc_data_idx_t dataIndexOf(const GCGraph *graph, gc_node_ref_t nodeRef) {
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

inline bool isNativeMacroOperUri(std::string_view uri) { return uri == "nn:apply_gradients"; }

inline std::string macroFunctionName(const ::Function *funcObj) {
    ASSERT(funcObj != nullptr, "Macro function is null.");
    auto *runtimeGraph = funcObj->graph();
    ASSERT(runtimeGraph != nullptr, "Macro function is missing runtime graph materialization.");
    return runtimeGraph->name();
}

inline const TupleType *ensureMacroClosureType(const GCGraph *graph) {
    ASSERT(graph != nullptr, "Macro closure type lookup requires a runtime graph.");
    return graph->closureType() ? graph->closureType() : TupleType::create();
}

inline void bindRuntimeGraphArgs(
    Frame *frame, GCGraph *calleeGraph, GCGraph *callerGraph,
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

#ifdef _WIN32
void *alignedAlloc(size_t size, size_t align) { return _aligned_malloc(size, align); }
void alignedFree(void *ptr) { _aligned_free(ptr); }
#else
void *alignedAlloc(size_t size, size_t align) {
    return std::aligned_alloc(align, mm::alignUp(size, align));
}
void alignedFree(void *ptr) { std::free(ptr); }
#endif

} // namespace

class MacroStaticAreaStore final : public mm::IAllocator {
  public:
    ~MacroStaticAreaStore() override {
        for (void *ptr : allocations_) {
            alignedFree(ptr);
        }
    }

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        void *ptr = alignedAlloc(size, align);
        if (!ptr) {
            throw std::bad_alloc();
        }
        allocations_.push_back(ptr);
        return ptr;
    }

    void free(void *ptr) override {
        if (!ptr) {
            return;
        }
        auto it = std::find(allocations_.begin(), allocations_.end(), ptr);
        if (it != allocations_.end()) {
            allocations_.erase(it);
        }
        alignedFree(ptr);
    }

    ::Tuple *cloneStaticArea(const GCGraph *graph) {
        ASSERT(graph != nullptr, "Macro static-area snapshot requires a runtime graph.");
        const auto slots = graph->staticSlots();
        auto *snapshot   = ::Tuple::create(slots.size(), *this);
        for (size_t i = 0; i < slots.size(); ++i) {
            snapshot->set<slot_t>(i, slots[i]);
        }
        return snapshot;
    }

  private:
    std::vector<void *> allocations_;
};

struct MacroEvaluator::ValueRoot {
    slot_t value = NullSlot;
    Type *type   = nullptr;
};

MacroEvaluator::MacroEvaluator(const context_ptr_t &context)
    : context_(context), framePool_(kMacroFramePoolSize),
      staticAreas_(std::make_unique<MacroStaticAreaStore>()) {
    framePool_.registerGcTracer();
    mm::autoSpace().registerExternalRootTracer(
        this,
        [this](const mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
            traceValueRoots(relocate);
        });
}

MacroEvaluator::~MacroEvaluator() { mm::autoSpace().unregisterExternalRootTracer(this); }

MacroEvalResult MacroEvaluator::anchorResult(slot_t value, Type *type, uint8_t flags) {
    auto root               = std::make_unique<ValueRoot>();
    root->value             = value;
    root->type              = type;
    const slot_t *valueSlot = &root->value;
    valueRoots_.push_back(std::move(root));
    return MacroEvalResult{.valueSlot = valueSlot, .type = type, .runtimeFlags = flags};
}

void MacroEvaluator::traceValueRoots(
    const mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
    for (auto &root : valueRoots_) {
        if (!root || !root->type || !root->type->isGCTraced() || root->value == NullSlot) {
            continue;
        }
        auto *relocated = relocate(fromSlot<Object *>(root->value));
        root->value     = toSlot(relocated);
    }
}

std::optional<MacroEvalResult>
MacroEvaluator::tryEvaluate(const MacroCandidate &candidate, std::ostream &os) {
    try {
        GCGraph *runtimeGraph = candidate.runtimeGraph;
        const auto *node      = runtimeGraph ? runtimeGraph->node(candidate.nodeRef) : nullptr;
        if (node == nullptr) {
            return std::nullopt;
        }
        switch (node->kind) {
        case GCNodeKind::Func:
            return tryExecuteDirectFunc(runtimeGraph, candidate.nodeRef, os);
        case GCNodeKind::Call:
            return tryExecuteIndirectCall(runtimeGraph, candidate.nodeRef, os);
        case GCNodeKind::Oper:
            return tryExecuteStaticOper(runtimeGraph, candidate.nodeRef, os);
        default:
            return std::nullopt;
        }
    } catch (const MacroExecutionError &e) {
        (void)os;
        CAMEL_LOG_DEBUG_S(
            "Macro",
            "Skip {}::ref#{}: {}",
            candidate.runtimeGraph ? candidate.runtimeGraph->name() : "(null)",
            candidate.nodeRef,
            e.what());
        return std::nullopt;
    } catch (const Diagnostic &d) {
        (void)os;
        CAMEL_LOG_WARN_S(
            "Macro",
            "Diagnostic while evaluating {}::ref#{}: {} | {}",
            candidate.runtimeGraph ? candidate.runtimeGraph->name() : "(null)",
            candidate.nodeRef,
            d.name,
            d.message);
        return std::nullopt;
    } catch (const std::exception &e) {
        (void)os;
        CAMEL_LOG_WARN_S(
            "Macro",
            "Exception while evaluating {}::ref#{}: {}",
            candidate.runtimeGraph ? candidate.runtimeGraph->name() : "(null)",
            candidate.nodeRef,
            e.what());
        return std::nullopt;
    }
}

bool MacroEvaluator::macroCallsFunctionParam(GCGraph *graph) const {
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

std::optional<MacroEvalResult>
MacroEvaluator::tryExecuteDirectFunc(GCGraph *ownerGraph, gc_node_ref_t nodeRef, std::ostream &os) {
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
    (void)os;
    CAMEL_LOG_INFO_S("Macro", "Execute direct macro '{}'.", calleeGraph->name());
    const slot_t value = executeFunction(
        ::Function::create(calleeGraph, ensureMacroClosureType(calleeGraph), mm::autoSpace()),
        [&](Frame *frame, GCGraph *runtimeGraph) {
            bindRuntimeGraphArgs(
                frame,
                runtimeGraph,
                ownerGraph,
                ownerGraph->withInputsOf(nodeRef),
                ownerGraph->normInputsOf(nodeRef));
        },
        true);
    return anchorResult(value, node->dataType, node->flags);
}

std::optional<MacroEvalResult> MacroEvaluator::tryExecuteIndirectCall(
    GCGraph *ownerGraph, gc_node_ref_t nodeRef, std::ostream &os) {
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
    (void)os;
    CAMEL_LOG_INFO_S("Macro", "Execute indirect macro '{}'.", macroFunctionName(funcObj));
    const slot_t value = executeFunction(
        funcObj,
        [&](Frame *frame, GCGraph *runtimeGraph) {
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
        },
        true);
    return anchorResult(value, node->dataType, node->flags);
}

std::optional<MacroEvalResult>
MacroEvaluator::tryExecuteStaticOper(GCGraph *ownerGraph, gc_node_ref_t nodeRef, std::ostream &os) {
    const auto *node = ownerGraph ? ownerGraph->node(nodeRef) : nullptr;
    if (!node || node->kind != GCNodeKind::Oper) {
        return std::nullopt;
    }
    const auto *body = ownerGraph->nodeBodyAs<GCOperBody>(nodeRef);
    if (!body || (!node->isMacro() && !isNativeMacroOperUri(body->uri()))) {
        return std::nullopt;
    }
    const bool nativeMacroOper = isNativeMacroOperUri(body->uri());
    if ((!nativeMacroOper &&
         !areStaticRuntimeDataInputs(ownerGraph, ownerGraph->withInputsOf(nodeRef))) ||
        !areStaticRuntimeDataInputs(ownerGraph, ownerGraph->normInputsOf(nodeRef))) {
        return std::nullopt;
    }

    operator_t op = body->op;
    if (!op) {
        const auto uri = std::string(body->uri());
        auto found     = context_->execMgr().find(uri);
        if (!found) {
            throw MacroExecutionError(
                std::format("Operator '{}' is unavailable in macro execution.", uri));
        }
        op = *found;
    }

    ::Tuple *staticAreaSnapshot = staticAreas_->cloneStaticArea(ownerGraph);
    Frame *frame                = framePool_.acquire(ownerGraph, staticAreaSnapshot);
    try {
        const auto normInputs = ownerGraph->normInputsOf(nodeRef);
        const auto withInputs =
            nativeMacroOper ? std::span<const gc_node_ref_t>{} : ownerGraph->withInputsOf(nodeRef);
        std::vector<gc_data_idx_t> indices;
        indices.reserve(normInputs.size() + withInputs.size());
        for (gc_node_ref_t in : normInputs) {
            indices.push_back(dataIndexOf(ownerGraph, in));
        }
        size_t normCount = indices.size();
        for (gc_node_ref_t in : withInputs) {
            indices.push_back(dataIndexOf(ownerGraph, in));
        }

        data_arr_t nargs{indices.data(), normCount};
        data_arr_t wargs{indices.data() + normCount, indices.size() - normCount};
        FrameArgsView withView(*frame, wargs);
        FrameArgsView normView(*frame, nargs);
        (void)os;
        CAMEL_LOG_INFO_S("Macro", "Execute static operator '{}'.", std::string(body->uri()));
        slot_t value = (*op)(withView, normView, *context_);
        framePool_.release(frame);
        return anchorResult(value, node->dataType, node->flags);
    } catch (...) {
        framePool_.release(frame);
        throw;
    }
}

slot_t MacroEvaluator::executeFunction(
    ::Function *funcObj, const std::function<void(Frame *, GCGraph *)> &fillArgs,
    bool requireMacroGraph) {
    if (!funcObj) {
        throw MacroExecutionError("Macro callee is null.");
    }
    auto *runtimeGraph = funcObj->graph();
    ASSERT(runtimeGraph != nullptr, "Macro function must carry a runtime graph.");
    if (requireMacroGraph && !runtimeGraph->isMacro()) {
        throw MacroExecutionError(
            std::format("'{}' is not marked as macro.", runtimeGraph->name()));
    }
    if (recursionDepth_ >= kMaxMacroRecursionDepth) {
        throw MacroExecutionError(
            std::format("Macro recursion depth exceeded at '{}'.", runtimeGraph->name()));
    }

    ::Tuple *staticAreaSnapshot = staticAreas_->cloneStaticArea(runtimeGraph);
    Frame *frame                = framePool_.acquire(runtimeGraph, staticAreaSnapshot);
    bool enteredExecution       = false;
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
        enteredExecution = true;
        slot_t result    = executeGraph(frame, runtimeGraph);
        recursionDepth_--;
        framePool_.release(frame);
        return result;
    } catch (...) {
        if (enteredExecution) {
            recursionDepth_--;
        }
        framePool_.release(frame);
        throw;
    }
}

slot_t MacroEvaluator::executeGraph(Frame *frame, GCGraph *runtimeGraph) {
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
            gc_data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
            Type *srcType        = frame->typeAt<Type>(srcIdx);
            Type *dstType        = node->dataType;
            slot_t value         = frame->get<slot_t>(srcIdx);
            frame->set(node->dataIndex, dstType->castSlotFrom(value, srcType));
        } break;

        case GCNodeKind::Copy: {
            const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
            ASSERT(!normInputs.empty(), "COPY node must have one norm input.");
            gc_data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
            TypeCode srcCode     = frame->codeAt(srcIdx);
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
            gc_data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
            Type *srcType        = node->dataType;
            ASSERT(isGCTraced(srcType->code()), "FILL target type is not GC-traced.");
            Object *sourceObj = frame->get<Object *>(srcIdx);
            ASSERT(sourceObj != nullptr, "FILL source object is null.");
            Object *srcObj = sourceObj->clone(mm::autoSpace(), srcType, false);
            ASSERT(srcObj != nullptr, "FILL target data is null.");
            std::vector<slot_t> fillValues;
            fillValues.reserve(dataInputs.size());
            for (auto input : dataInputs) {
                fillValues.push_back(frame->get<slot_t>(dataIndexOf(runtimeGraph, input)));
            }
            camel::execute::writeRuntimeFillSlots(
                srcObj,
                srcType,
                runtimeGraph->nodeBodyAs<camel::runtime::GCFillBody>(runtimeNodeIndex),
                fillValues);
            frame->set(node->dataIndex, srcObj);
        } break;

        case GCNodeKind::Accs: {
            const auto normInputs = runtimeGraph->normInputsOf(runtimeNodeIndex);
            ASSERT(!normInputs.empty(), "ACCS node must have one source input.");
            gc_data_idx_t srcIdx = dataIndexOf(runtimeGraph, normInputs.front());
            const auto *body     = runtimeGraph->nodeBodyAs<GCAccsBody>(runtimeNodeIndex);
            if (body->accsKind == camel::runtime::GCAccsKind::TupleIndex) {
                size_t idx  = body->value;
                auto *tuple = frame->get<::Tuple *>(srcIdx);
                frame->set(node->dataIndex, tuple->get<slot_t>(idx));
            } else {
                const std::string_view keyView = body->key();
                auto key                       = std::string(keyView.data(), keyView.size());
                auto *st                       = frame->get<::Struct *>(srcIdx);
                Type *structType               = frame->typeAt<Type>(srcIdx);
                frame->set(node->dataIndex, st->get<slot_t>(key, structType));
            }
        } break;

        case GCNodeKind::Brch: {
            const size_t jumpIdx =
                camel::execute::selectRuntimeBranchArm(runtimeGraph, runtimeNodeIndex, frame);
            frame->set(node->dataIndex, fromSlot<Int32>(static_cast<Int32>(jumpIdx)));
        } break;

        case GCNodeKind::Join: {
            std::vector<gc_data_idx_t> nargs;
            std::vector<gc_data_idx_t> wargs;
            for (uint32_t inputIndex : runtimeGraph->normInputsOf(runtimeNodeIndex)) {
                const auto *inputRecord = runtimeGraph->node(inputIndex);
                ASSERT(inputRecord != nullptr, "Macro runtime JOIN norm input record is missing.");
                nargs.push_back(inputRecord->dataIndex);
            }
            for (uint32_t inputIndex : runtimeGraph->withInputsOf(runtimeNodeIndex)) {
                const auto *inputRecord = runtimeGraph->node(inputIndex);
                ASSERT(inputRecord != nullptr, "Macro runtime JOIN with input record is missing.");
                wargs.push_back(inputRecord->dataIndex);
            }
            int32_t brIndex = frame->get<int32_t>(nargs.front());
            if (node->dataIndex == 0) {
                break;
            }
            if (node->dataType == Type::Void()) {
                frame->set(node->dataIndex, NullSlot);
                break;
            }
            slot_t branchData = frame->get<slot_t>(wargs[static_cast<size_t>(brIndex)]);
            frame->set(node->dataIndex, branchData);
        } break;

        case GCNodeKind::Call: {
            const auto withInputs = runtimeGraph->withInputsOf(runtimeNodeIndex);
            ASSERT(!withInputs.empty(), "Runtime CALL node must expose a callee input.");
            const auto *calleeRecord = runtimeGraph->node(withInputs.front());
            ASSERT(calleeRecord != nullptr, "Runtime CALL callee record is missing.");
            const gc_data_idx_t calleeSlot = calleeRecord->dataIndex;
            ::Function *funcObj            = nullptr;
            bool calleeIsMacro             = false;
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
                    "Macro cannot call non-macro function at compile time (use runtime instead).");
            }
            ASSERT(funcObj->graph() != nullptr, "Macro CALL callee is missing runtime graph.");
            slot_t result = executeFunction(
                funcObj,
                [&](Frame *calleeFrame, GCGraph *) {
                    camel::execute::fillFrameForIndirectCall(
                        frame,
                        calleeFrame,
                        runtimeGraph,
                        runtimeNodeIndex);
                },
                true);
            frame->set(node->dataIndex, result);
        } break;

        case GCNodeKind::Func: {
            auto *calleeRuntimeGraph = runtimeGraph->directCalleeGraphOf(runtimeNodeIndex);
            ASSERT(
                calleeRuntimeGraph != nullptr,
                "Runtime FUNC node must have a direct runtime callee graph.");
            auto *funcObj = ::Function::create(
                calleeRuntimeGraph,
                ensureMacroClosureType(calleeRuntimeGraph),
                mm::autoSpace());
            slot_t result = executeFunction(
                funcObj,
                [&](Frame *calleeFrame, GCGraph *) {
                    camel::execute::fillFrameForDirectInvoke(
                        frame,
                        calleeFrame,
                        runtimeGraph,
                        runtimeNodeIndex);
                },
                false);
            frame->set(node->dataIndex, result);
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
            std::vector<gc_data_idx_t> indices;
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

        case GCNodeKind::Data:
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

} // namespace camel::execute::macro_runtime
