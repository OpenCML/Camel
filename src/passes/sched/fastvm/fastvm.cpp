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
 * Created: Sep. 08, 2025
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "camel/core/module/module.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "opperf.h"

#include <limits>

#if ENABLE_FASTVM_JIT
#include "jit/backend/backend.h"
#include "jit/runtime/trampoline.h"

#include <cstdio>
#include <optional>
#endif

using namespace std;
using namespace camel::core::error;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using namespace camel::core::context;

#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif

namespace {

constexpr size_t kFastVmEntryPcSlot            = 1;
constexpr size_t kFastVmJitEntrySlot           = 2;
constexpr size_t kFastVmFlagsSlot              = 3;
constexpr size_t kFastVmIndirectCallCountSlot  = 5;
constexpr size_t kFastVmGraphLengthSlot        = 6;
constexpr uintptr_t kFastVmFlagCompileFailed   = 1u << 0;
constexpr uintptr_t kFastVmFlagFailureReported = 1u << 1;

inline std::optional<size_t> fastVmEntryPcOf(camel::runtime::GCGraph *graph) {
    if (!graph)
        return std::nullopt;
    uintptr_t raw = graph->extraSlot(kFastVmEntryPcSlot);
    if (raw == 0)
        return std::nullopt;
    return static_cast<size_t>(raw - 1);
}

inline void setFastVmEntryPcOf(camel::runtime::GCGraph *graph, size_t pc) {
    if (graph)
        graph->setExtraSlot(kFastVmEntryPcSlot, static_cast<uintptr_t>(pc) + 1);
}

inline std::optional<size_t> fastVmGraphLengthOf(camel::runtime::GCGraph *graph) {
    if (!graph)
        return std::nullopt;
    uintptr_t raw = graph->extraSlot(kFastVmGraphLengthSlot);
    if (raw == 0)
        return std::nullopt;
    return static_cast<size_t>(raw - 1);
}

inline void setFastVmGraphLengthOf(camel::runtime::GCGraph *graph, size_t length) {
    if (graph)
        graph->setExtraSlot(kFastVmGraphLengthSlot, static_cast<uintptr_t>(length) + 1);
}

inline jit::JitEntryFn fastVmJitEntryOf(camel::runtime::GCGraph *graph) {
    return graph ? reinterpret_cast<jit::JitEntryFn>(graph->extraSlot(kFastVmJitEntrySlot))
                 : nullptr;
}

inline void setFastVmJitEntryOf(camel::runtime::GCGraph *graph, jit::JitEntryFn fn) {
    if (graph)
        graph->setExtraSlot(kFastVmJitEntrySlot, reinterpret_cast<uintptr_t>(fn));
}

inline uintptr_t fastVmFlagsOf(camel::runtime::GCGraph *graph) {
    return graph ? graph->extraSlot(kFastVmFlagsSlot) : 0;
}

inline bool fastVmJitCompileFailedOf(camel::runtime::GCGraph *graph) {
    return (fastVmFlagsOf(graph) & kFastVmFlagCompileFailed) != 0;
}

inline void setFastVmJitCompileFailedOf(camel::runtime::GCGraph *graph, bool failed) {
    if (!graph)
        return;
    uintptr_t flags = fastVmFlagsOf(graph);
    flags = failed ? (flags | kFastVmFlagCompileFailed) : (flags & ~kFastVmFlagCompileFailed);
    graph->setExtraSlot(kFastVmFlagsSlot, flags);
}

inline bool fastVmJitFailureReportedOf(camel::runtime::GCGraph *graph) {
    return (fastVmFlagsOf(graph) & kFastVmFlagFailureReported) != 0;
}

inline void setFastVmJitFailureReportedOf(camel::runtime::GCGraph *graph, bool reported) {
    if (!graph)
        return;
    uintptr_t flags = fastVmFlagsOf(graph);
    flags = reported ? (flags | kFastVmFlagFailureReported) : (flags & ~kFastVmFlagFailureReported);
    graph->setExtraSlot(kFastVmFlagsSlot, flags);
}

inline uint32_t incFastVmIndirectCallCountOf(camel::runtime::GCGraph *graph) {
    if (!graph) {
        return 0;
    }
    const uintptr_t raw = graph->extraSlot(kFastVmIndirectCallCountSlot);
    const uint32_t next = raw >= static_cast<uintptr_t>(std::numeric_limits<uint32_t>::max())
                              ? std::numeric_limits<uint32_t>::max()
                              : static_cast<uint32_t>(raw) + 1;
    graph->setExtraSlot(kFastVmIndirectCallCountSlot, static_cast<uintptr_t>(next));
    return next;
}

struct HigherOrderCallSite {
    camel::runtime::GCGraph *runtimeGraph = nullptr;
    size_t entryPc                        = 0;
};

inline HigherOrderCallSite makeHigherOrderCallSite(Function *func) {
    ASSERT(func != nullptr, "Higher-order call target function is null.");
    camel::runtime::GCGraph *runtimeGraph = func->graph();
    ASSERT(
        runtimeGraph != nullptr,
        "FastVM higher-order runtime call requires a materialized runtime graph.");
    const auto entryPc = fastVmEntryPcOf(runtimeGraph);
    ASSERT(
        entryPc.has_value(),
        std::format("Runtime graph '{}' has no FastVM entry pc.", runtimeGraph->name()));
    return {
        .runtimeGraph = runtimeGraph,
        .entryPc      = *entryPc,
    };
}

} // namespace

FastVMSchedPass::~FastVMSchedPass() = default;

void FastVMSchedPass::populateDirectCallFrame(
    Frame *callerFrame, Frame *calleeFrame, data_arr_t srcArgs, data_arr_t dstSlots) {
    ASSERT(callerFrame != nullptr && calleeFrame != nullptr, "FastVM call frame is null.");
    ASSERT(
        srcArgs.size() == dstSlots.size(),
        "FastVM encoded direct-call layout is arity-mismatched.");
    if (srcArgs.size() == 1) {
        calleeFrame->set(dstSlots[0], callerFrame->get<slot_t>(srcArgs[0]));
        return;
    }
    for (size_t argIndex = 0; argIndex < srcArgs.size(); ++argIndex) {
        calleeFrame->set(dstSlots[argIndex], callerFrame->get<slot_t>(srcArgs[argIndex]));
    }
}

void FastVMSchedPass::captureCallArgValues(
    Frame *callerFrame, data_arr_t srcArgs, std::vector<slot_t> &out) {
    ASSERT(callerFrame != nullptr, "FastVM direct call source frame is null.");
    out.resize(srcArgs.size());
    if (srcArgs.size() == 1) {
        out[0] = callerFrame->get<slot_t>(srcArgs[0]);
        return;
    }
    for (size_t argIndex = 0; argIndex < srcArgs.size(); ++argIndex) {
        out[argIndex] = callerFrame->get<slot_t>(srcArgs[argIndex]);
    }
}

void FastVMSchedPass::populateDirectCallFrameFromValues(
    Frame *calleeFrame, data_arr_t dstSlots, std::span<const slot_t> argValues) {
    ASSERT(calleeFrame != nullptr, "FastVM direct call target frame is null.");
    ASSERT(
        argValues.size() == dstSlots.size(),
        "FastVM encoded direct-call layout is arity-mismatched.");
    if (argValues.size() == 1) {
        calleeFrame->set(dstSlots[0], argValues[0]);
        return;
    }
    for (size_t argIndex = 0; argIndex < argValues.size(); ++argIndex) {
        calleeFrame->set(dstSlots[argIndex], argValues[argIndex]);
    }
}

void FastVMSchedPass::populateFunctionClosureFrame(Frame *calleeFrame, Function *func) {
    ASSERT(calleeFrame != nullptr, "FastVM closure target frame is null.");
    ASSERT(func != nullptr, "FastVM closure Function is null.");
    auto *targetGraph = func->graph();
    ASSERT(targetGraph != nullptr, "FastVM closure target graph is null.");

    const auto closureNodes = targetGraph->closureNodes();
    if (closureNodes.empty()) {
        return;
    }

    Tuple *closure = func->tuple();
    ASSERT(closure != nullptr, "FastVM closure tuple is null.");
    ASSERT(
        closure->size() == closureNodes.size(),
        std::format(
            "FastVM closure-arity mismatch for graph '{}': expected {}, got {}.",
            targetGraph->name(),
            closureNodes.size(),
            closure->size()));
    for (size_t i = 0; i < closureNodes.size(); ++i) {
        const auto *port = targetGraph->node(closureNodes[i]);
        ASSERT(port != nullptr, "FastVM closure port is null.");
        calleeFrame->set(port->dataIndex, closure->get<slot_t>(i));
    }
}

void FastVMSchedPass::populateIndirectCallFrame(
    Frame *callerFrame, Frame *calleeFrame, Function *func, data_arr_t nargs, data_arr_t wargs) {
    ASSERT(callerFrame != nullptr, "FastVM indirect call source frame is null.");
    ASSERT(calleeFrame != nullptr, "FastVM indirect call target frame is null.");
    ASSERT(func != nullptr, "FastVM indirect call callee Function is null.");
    auto *targetGraph = func->graph();
    ASSERT(targetGraph != nullptr, "FastVM indirect call target graph is null.");
    ASSERT(!wargs.empty(), "FastVM indirect CALL requires with-arg[0] as Function.");

    const auto normPorts = targetGraph->normPorts();
    ASSERT(
        nargs.size() == normPorts.size(),
        std::format(
            "FastVM indirect call norm-arity mismatch for graph '{}': expected {}, got {}.",
            targetGraph->name(),
            normPorts.size(),
            nargs.size()));
    for (size_t i = 0; i < normPorts.size(); ++i) {
        const auto *port = targetGraph->node(normPorts[i]);
        ASSERT(port != nullptr, "FastVM indirect call norm port is null.");
        calleeFrame->set(port->dataIndex, callerFrame->get<slot_t>(nargs[i]));
    }

    const auto withPorts = targetGraph->withPorts();
    ASSERT(
        wargs.size() - 1 == withPorts.size(),
        std::format(
            "FastVM indirect call with-arity mismatch for graph '{}': expected {}, got {}.",
            targetGraph->name(),
            withPorts.size(),
            wargs.size() - 1));
    for (size_t i = 0; i < withPorts.size(); ++i) {
        const auto *port = targetGraph->node(withPorts[i]);
        ASSERT(port != nullptr, "FastVM indirect call with port is null.");
        calleeFrame->set(port->dataIndex, callerFrame->get<slot_t>(wargs[i + 1]));
    }

    populateFunctionClosureFrame(calleeFrame, func);
}

void FastVMSchedPass::populateMarkedCallFrame(
    Frame *calleeFrame, Function *func, std::span<const slot_t> normArgs,
    std::span<const slot_t> withArgs) {
    ASSERT(calleeFrame != nullptr, "FastVM marked call target frame is null.");
    ASSERT(func != nullptr, "FastVM marked call Function is null.");
    auto *targetGraph = func->graph();
    ASSERT(targetGraph != nullptr, "FastVM marked call target graph is null.");

    const auto normPorts = targetGraph->normPorts();
    const auto withPorts = targetGraph->withPorts();
    ASSERT(
        normArgs.size() == normPorts.size(),
        std::format(
            "FastVM marked call norm-arity mismatch for graph '{}': expected {}, got {}.",
            targetGraph->name(),
            normPorts.size(),
            normArgs.size()));
    ASSERT(
        withArgs.size() == withPorts.size(),
        std::format(
            "FastVM marked call with-arity mismatch for graph '{}': expected {}, got {}.",
            targetGraph->name(),
            withPorts.size(),
            withArgs.size()));

    for (size_t i = 0; i < normPorts.size(); ++i) {
        const auto *port = targetGraph->node(normPorts[i]);
        ASSERT(port != nullptr, "FastVM marked call norm port is null.");
        calleeFrame->set(port->dataIndex, normArgs[i]);
    }
    for (size_t i = 0; i < withPorts.size(); ++i) {
        const auto *port = targetGraph->node(withPorts[i]);
        ASSERT(port != nullptr, "FastVM marked call with port is null.");
        calleeFrame->set(port->dataIndex, withArgs[i]);
    }

    populateFunctionClosureFrame(calleeFrame, func);
}

void FastVMSchedPass::precompile(camel::runtime::GCGraph *runtimeRoot) {
    ASSERT(runtimeRoot != nullptr, "Runtime root graph is null.");
    auto linked = compileAndLink(
        context_,
        runtimeRoot,
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });
    bytecodes_ = std::move(linked.codes);
    offsetMap_ = std::move(linked.offsetMap);
    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        if (!runtimeGraph) {
            continue;
        }
        setFastVmEntryPcOf(runtimeGraph, offset);
        setFastVmGraphLengthOf(runtimeGraph, length);
    }
}

void FastVMSchedPass::push(size_t pc, Frame *frame) {
    if (stackDepth_ >= maxRecursionDepth_) {
        throwRuntimeFault(
            RuntimeDiag::MaxRecursionDepthExceeded,
            frame->graphName(),
            maxRecursionDepth_);
    }
    pcStack_[stackDepth_]    = pc;
    frameStack_[stackDepth_] = frame;
    ++stackDepth_;
}

std::pair<size_t, Frame *> FastVMSchedPass::pop() {
    ASSERT(stackDepth_ > 0, "FastVM call stack underflow.");
    --stackDepth_;
    size_t pc    = pcStack_[stackDepth_];
    Frame *frame = frameStack_[stackDepth_];
    return {pc, frame};
}

camel::runtime::GCGraph *FastVMSchedPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    (void)os;
    ASSERT(graph != nullptr, "FastVM requires a non-null runtime root graph.");

    camel::runtime::GCGraph *runtimeRoot = graph;
    precompile(runtimeRoot);

    stackDepth_ = 0;

#if ENABLE_FASTVM_JIT
    if (jitConfig_.policy != JitPolicy::Disabled && !jitBackend_) {
        jitBackend_ = createBackend();
    }
    for (const auto &[g, _] : offsetMap_) {
        if (g) {
            setJitCompileFailedOf(g, false, true);
        }
    }
    JitContext jitCtx{this, bytecodes_.data()};
    currentJitCtx_ = &jitCtx;
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "JIT",
        "JIT policy: {}",
        jitConfig_.policy == JitPolicy::Disabled
            ? "Disabled"
            : (jitConfig_.policy == JitPolicy::Always ? "Always" : "OnDemand")));
    if (jitConfig_.policy == JitPolicy::Always) {
        CAMEL_LOG_INFO_S(
            "JIT",
            "JIT Always: switched to lazy compile-by-touch (no startup full-graph compile)");
        compileAndCacheGraph(runtimeRoot, offsetMap_.at(runtimeRoot));
    } else if (jitConfig_.policy == JitPolicy::OnDemand) {
        CAMEL_LOG_INFO_S("JIT", "JIT OnDemand: start with interpreter, compile on hot threshold");
    }
    JitEntryFn entryJitFn = jitFnOf(runtimeRoot);
    if (jitBackend_ && entryJitFn) {
        CAMEL_LOG_INFO_S("JIT", "Executing entry graph '{}' via JIT", runtimeRoot->name());
        Frame *frame = framePool_.acquire(runtimeRoot);
        opperf::start();
        slot_t result = invokeOwnedJitFrame(entryJitFn, frame, &jitCtx);
        opperf::stop();
        opperf::report(std::cout);
        context_->captureProcessExitCode(runtimeRoot, result);
        return nullptr;
    }
    CAMEL_LOG_INFO_S(
        "JIT",
        "Entry graph '{}' not in JIT cache, falling back to interpreter",
        runtimeRoot->name());
#endif

    opperf::start();
    size_t pc    = graphEntryPc(runtimeRoot);
    Frame *frame = framePool_.acquire(runtimeRoot);
    try {
        slot_t result = call(pc, frame);
        context_->captureProcessExitCode(runtimeRoot, result);
    } catch (...) {
        stackDepth_ = 0;
        throw;
    }
    opperf::stop();
    opperf::report(std::cout);
    return nullptr;
}

void FastVMSchedPass::evalMarkedOperator(
    const MarkOpCode op, data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    switch (op) {
    case MarkOpCode::MapArr:
        evalMarkedOperator_map_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ApplyArr:
        evalMarkedOperator_apply_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::FilterArr:
        evalMarkedOperator_filter_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ReduceArr:
        evalMarkedOperator_reduce_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ForeachArr:
        evalMarkedOperator_foreach_arr(self, nargs, wargs, currFrame);
        break;
    default:
        ASSERT(false, "Unsupported marked operator in FastVM.");
    }
}

void FastVMSchedPass::evalMarkedOperator_map_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);

    Array *res = Array::create(mm::autoSpace(), arr->size());
    currFrame.set(self, res);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr                            = currFrame.get<Array *>(nargs[0]);
        func                           = currFrame.get<Function *>(wargs[0]);
        const HigherOrderCallSite site = makeHigherOrderCallSite(func);
        slot_t element                 = arr->data()[i];
        Frame *frame                   = framePool_.acquire(site.runtimeGraph);
        populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

#if ENABLE_FASTVM_JIT
        currFrame.get<Array *>(self)->data()[i] =
            invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        currFrame.get<Array *>(self)->data()[i] = call(site.entryPc, frame);
#endif
    }
}

void FastVMSchedPass::evalMarkedOperator_apply_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr                            = currFrame.get<Array *>(nargs[0]);
        func                           = currFrame.get<Function *>(wargs[0]);
        const HigherOrderCallSite site = makeHigherOrderCallSite(func);
        slot_t element                 = arr->data()[i];
        Frame *frame                   = framePool_.acquire(site.runtimeGraph);
        populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

#if ENABLE_FASTVM_JIT
        arr->data()[i] = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        arr->data()[i] = call(site.entryPc, frame);
#endif
    }

    currFrame.set(self, currFrame.get<Array *>(nargs[0]));
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);

    Array *filtered = Array::create(mm::autoSpace(), arr->size());
    currFrame.set(self, filtered);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr                            = currFrame.get<Array *>(nargs[0]);
        func                           = currFrame.get<Function *>(wargs[0]);
        const HigherOrderCallSite site = makeHigherOrderCallSite(func);
        slot_t element                 = arr->data()[i];
        Frame *frame                   = framePool_.acquire(site.runtimeGraph);
        populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

#if ENABLE_FASTVM_JIT
        slot_t result = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        slot_t result = call(site.entryPc, frame);
#endif

        if (fromSlot<bool>(result)) {
            currFrame.get<Array *>(self)->append(arr->data()[i]);
        }
    }

    currFrame.get<Array *>(self)->shrinkToFit();
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    slot_t init    = currFrame.get<slot_t>(wargs[1]);

    // Preserve left-fold semantics: an empty array returns the initial value immediately.
    if (arr->size() == 0) {
        currFrame.set(self, init);
        return;
    }

    currFrame.set(self, init);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr                            = currFrame.get<Array *>(nargs[0]);
        func                           = currFrame.get<Function *>(wargs[0]);
        const HigherOrderCallSite site = makeHigherOrderCallSite(func);
        const slot_t args[]            = {currFrame.get<slot_t>(self), arr->data()[i]};
        Frame *frame                   = framePool_.acquire(site.runtimeGraph);
        populateMarkedCallFrame(frame, func, std::span<const slot_t>(args, 2));

#if ENABLE_FASTVM_JIT
        currFrame.set(
            self,
            invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_));
#else
        currFrame.set(self, call(site.entryPc, frame));
#endif
    }
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);

    for (size_t i = 0; i < arr->size(); ++i) {
        arr                            = currFrame.get<Array *>(nargs[0]);
        func                           = currFrame.get<Function *>(wargs[0]);
        const HigherOrderCallSite site = makeHigherOrderCallSite(func);
        slot_t element                 = arr->data()[i];
        Frame *frame                   = framePool_.acquire(site.runtimeGraph);
        populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

#if ENABLE_FASTVM_JIT
        invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        call(site.entryPc, frame);
#endif
    }

    // foreach has no aggregate return value.
    currFrame.set(self, NullSlot);
}

#if ENABLE_FASTVM_JIT

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
slot_t FastVMSchedPass::invokeOwnedJitFrame(JitEntryFn fn, Frame *frame, void *jitCtx) {
    slot_t *slots = frame->slotBase();
    slots[0]      = reinterpret_cast<slot_t>(frame);
    try {
        slot_t result = fn(slots, jitCtx);
        if (framePool_.isActive(frame))
            framePool_.release(frame);
        return result;
    } catch (...) {
        if (framePool_.isActive(frame))
            framePool_.release(frame);
        throw;
    }
}

camel::runtime::GCGraph *FastVMSchedPass::jitFnToGraph(JitEntryFn fn) const {
    auto it = jitFnToGraph_.find(fn);
    return it != jitFnToGraph_.end() ? it->second : nullptr;
}

JitEntryFn FastVMSchedPass::jitFnOf(camel::runtime::GCGraph *graph) const {
    return graph ? fastVmJitEntryOf(graph) : nullptr;
}

void FastVMSchedPass::setJitFnOf(camel::runtime::GCGraph *graph, JitEntryFn fn) {
    if (graph) {
        setFastVmJitEntryOf(graph, fn);
    }
}

bool FastVMSchedPass::jitCompileFailedOf(camel::runtime::GCGraph *graph) const {
    return graph ? fastVmJitCompileFailedOf(graph) : false;
}

void FastVMSchedPass::setJitCompileFailedOf(
    camel::runtime::GCGraph *graph, bool failed, bool resetReport) {
    if (!graph) {
        return;
    }
    setFastVmJitCompileFailedOf(graph, failed);
    if (resetReport) {
        setFastVmJitFailureReportedOf(graph, false);
    }
}

bool FastVMSchedPass::jitFailureReportedOf(camel::runtime::GCGraph *graph) const {
    return graph ? fastVmJitFailureReportedOf(graph) : false;
}

void FastVMSchedPass::setJitFailureReportedOf(camel::runtime::GCGraph *graph, bool reported) {
    if (graph) {
        setFastVmJitFailureReportedOf(graph, reported);
    }
}

void FastVMSchedPass::compileAndCacheGraph(camel::runtime::GCGraph *graph, size_t entryPc) {
    if (!graph) {
        return;
    }
    std::lock_guard lock(jitCacheMutex_);
    if (jitFnOf(graph) || jitCompileFailedOf(graph)) {
        return;
    }
    ASSERT(
        graph->hasFrameLayout(),
        std::format("Graph '{}' has no finalized frame layout.", graph->name()));

    CompilationDebugOptions debugOptions{
        .enableDebugTrace = enableJitTraceMir_,
    };
    CompilationUnit unit{
        .runtimeGraph             = graph,
        .bytecodes                = std::span<const Bytecode>(bytecodes_.data(), bytecodes_.size()),
        .entryPc                  = entryPc,
        .graphLength              = fastVmGraphLengthOf(graph).value_or(0),
        .trampolineFunc           = reinterpret_cast<void *>(&trampolineFunc),
        .trampolineTail           = reinterpret_cast<void *>(&trampolineTail),
        .trampolineOper           = reinterpret_cast<void *>(&trampolineOper),
        .trampolineCast           = reinterpret_cast<void *>(&trampolineCast),
        .trampolineBytecode       = reinterpret_cast<void *>(&trampolineBytecode),
        .poolTopAddr              = framePool_.topAddr(),
        .directSelfFuncInvokeAddr = reinterpret_cast<void *>(&directSelfFuncInvoke),
        .debug                    = enableJitTraceMir_ ? &debugOptions : nullptr,
    };
    std::string failureReason;
    auto compiled = jitBackend_->compile(unit, &failureReason);
    if (!compiled) {
        setJitCompileFailedOf(graph, true);
        if (!jitFailureReportedOf(graph)) {
            CAMEL_LOG_WARN_S(
                "JIT",
                "OnDemand compile skipped for graph '{}': {}",
                graph->name(),
                failureReason.empty() ? "unknown failure" : failureReason);
            setJitFailureReportedOf(graph, true);
        }
        return;
    }
    // Hex dump of generated machine code (only when Info for "JIT" would emit; avoids work when
    // off)
    if (Logger::ShouldEmit(LogLevel::Info, "JIT")) {
        if (!compiled->code.empty()) {
            const uint8_t *p              = compiled->code.data();
            const size_t size             = compiled->code.size();
            constexpr size_t bytesPerLine = 16;
            CAMEL_LOG_INFO_S(
                "JIT",
                "JIT executed code for graph '{}' ({} bytes):",
                graph->name(),
                size);
            for (size_t i = 0; i < size; i += bytesPerLine) {
                std::string line;
                for (size_t j = 0; j < bytesPerLine && i + j < size; ++j) {
                    char buf[4];
                    snprintf(buf, sizeof(buf), "%02x ", p[i + j]);
                    line += buf;
                }
                CAMEL_LOG_INFO_S("JIT", "  [{:4}] {}", static_cast<unsigned>(i), line);
            }
        }
    }
    auto fn = jitBackend_->load(std::move(compiled));
    if (fn) {
        setJitFnOf(graph, fn);
        jitFnToGraph_[fn] = graph;
        setJitCompileFailedOf(graph, false);
        setJitFailureReportedOf(graph, false);
        for (size_t pc = 0; pc < bytecodes_.size();) {
            Bytecode &bc = bytecodes_[pc];
            if (bc.opcode == OpCode::FUNC || bc.opcode == OpCode::TAIL) {
                if (getFuncExtraFn(&bc) == nullptr && getFuncExtraRuntimeGraph(&bc) == graph)
                    setFuncExtraFn(&bc, reinterpret_cast<void *>(fn));
            }
            pc += bc.opsize;
        }
        CAMEL_LOG_INFO_S("JIT", "OnDemand: compiled & cached graph '{}'", graph->name());
    } else {
        setJitCompileFailedOf(graph, true);
        if (!jitFailureReportedOf(graph)) {
            CAMEL_LOG_WARN_S("JIT", "OnDemand compile load failed for graph '{}'.", graph->name());
            setJitFailureReportedOf(graph, true);
        }
    }
}

Bytecode *FastVMSchedPass::materializeCallTarget(size_t pc, Bytecode *bc) {
    if (!bc || getFuncExtraFn(bc) != nullptr) {
        return bc;
    }
    if (!jitEnabled()) {
        return bc;
    }
    auto *runtimeTarget = getFuncExtraRuntimeGraph(bc);
    ASSERT(runtimeTarget != nullptr, "FastVM direct call target is not materialized.");
    if (jitCompileFailedOf(runtimeTarget)) {
        return bc;
    }
    size_t targetPc = getFuncExtraTargetPc(bc);
    uint32_t count  = incFuncExtraCount(bc);
    if (tierPolicy_.shouldJit(count)) {
        compileAndCacheGraph(runtimeTarget, targetPc);
        return &bytecodes_[pc];
    }
    return bc;
}

slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
    CallResult result = callBorrowed(pc, rootFrame);
    if (result.rootFrame)
        framePool_.release(result.rootFrame);
    return result.result;
}

size_t FastVMSchedPass::graphEntryPc(camel::runtime::GCGraph *graph) const {
    ASSERT(graph != nullptr, "Runtime graph is null.");
    auto pc = fastVmEntryPcOf(graph);
    ASSERT(
        pc.has_value(),
        std::format("Runtime graph '{}' has no FastVM entry pc.", graph->name()));
    return *pc;
}

uint32_t FastVMSchedPass::noteIndirectCall(camel::runtime::GCGraph *graph) const {
    return incFastVmIndirectCallCountOf(graph);
}

slot_t FastVMSchedPass::invokeCallOrJit(
    size_t pc, camel::runtime::GCGraph *runtimeGraph, Frame *frame, void *jitCtx,
    uint32_t callCount) {
    ASSERT(runtimeGraph != nullptr, "FastVM JIT/invoke target runtime graph is null.");
    currentJitCtx_ = jitCtx;
    JitEntryFn fn  = jitFnOf(runtimeGraph);
    if (fn) {
        EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
            "JIT",
            "invokeCallOrJit: graph '{}' pc={} -> JIT",
            runtimeGraph->name(),
            pc));
        return invokeOwnedJitFrame(fn, frame, jitCtx);
    }
    const bool compileFailed = jitCompileFailedOf(runtimeGraph);
    if (!compileFailed && tierPolicy_.shouldJit(callCount)) {
        compileAndCacheGraph(runtimeGraph, pc);
        fn = jitFnOf(runtimeGraph);
        if (fn) {
            EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
                "JIT",
                "invokeCallOrJit: graph '{}' pc={} -> JIT (after compile)",
                runtimeGraph->name(),
                pc));
            return invokeOwnedJitFrame(fn, frame, jitCtx);
        }
    }
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "JIT",
        "invokeCallOrJit: graph '{}' pc={} -> interpreter",
        runtimeGraph->name(),
        pc));
    return call(pc, frame);
}
#endif
