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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "camel/runtime/exec_plan.h"
#include "camel/core/module/module.h"
#include "camel/execute/fault_isolation.h"
#include "camel/execute/loop_driver.h"
#include "camel/execute/observer.h"
#include "camel/profile/profile.h"
#include "camel/runtime/graph.h"
#include "camel/core/rtdata/aggregate.h"
#include "camel/core/rtdata/view.h"
#include "camel/utils/log.h"
#include "opperf.h"

#include <algorithm>
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

// Framed code reads the collector's request for a safepoint as one byte.
static_assert(sizeof(camel::core::mm::detail::autoSpaceSafepointSlowPath) == 1);

constexpr uintptr_t kFastVmFlagCompileFailed   = 1u << 0;
constexpr uintptr_t kFastVmFlagFailureReported = 1u << 1;

inline std::optional<size_t> fastVmEntryPcOf(camel::runtime::GCGraph *graph) {
    if (!graph)
        return std::nullopt;
    uintptr_t raw = graph->execPlan().fastVm.entryPc;
    if (raw == 0)
        return std::nullopt;
    return static_cast<size_t>(raw - 1);
}

inline void setFastVmEntryPcOf(camel::runtime::GCGraph *graph, size_t pc) {
    if (graph)
        graph->execPlan().fastVm.entryPc = static_cast<uintptr_t>(pc) + 1;
}

inline std::optional<size_t> fastVmGraphLengthOf(camel::runtime::GCGraph *graph) {
    if (!graph)
        return std::nullopt;
    uintptr_t raw = graph->execPlan().fastVm.graphLength;
    if (raw == 0)
        return std::nullopt;
    return static_cast<size_t>(raw - 1);
}

inline void setFastVmGraphLengthOf(camel::runtime::GCGraph *graph, size_t length) {
    if (graph)
        graph->execPlan().fastVm.graphLength = static_cast<uintptr_t>(length) + 1;
}

#if ENABLE_FASTVM_JIT
inline jit::JitEntryFn fastVmJitEntryOf(camel::runtime::GCGraph *graph) {
    return graph ? reinterpret_cast<jit::JitEntryFn>(graph->execPlan().fastVm.jitEntry)
                 : nullptr;
}

inline void setFastVmJitEntryOf(camel::runtime::GCGraph *graph, jit::JitEntryFn fn) {
    if (graph)
        graph->execPlan().fastVm.jitEntry = reinterpret_cast<uintptr_t>(fn);
}
#endif

inline uintptr_t fastVmFlagsOf(camel::runtime::GCGraph *graph) {
    return graph ? graph->execPlan().fastVm.flags : 0;
}

inline bool fastVmJitCompileFailedOf(camel::runtime::GCGraph *graph) {
    return (fastVmFlagsOf(graph) & kFastVmFlagCompileFailed) != 0;
}

inline void setFastVmJitCompileFailedOf(camel::runtime::GCGraph *graph, bool failed) {
    if (!graph)
        return;
    uintptr_t flags = fastVmFlagsOf(graph);
    flags = failed ? (flags | kFastVmFlagCompileFailed) : (flags & ~kFastVmFlagCompileFailed);
    graph->execPlan().fastVm.flags = flags;
}

inline bool fastVmJitFailureReportedOf(camel::runtime::GCGraph *graph) {
    return (fastVmFlagsOf(graph) & kFastVmFlagFailureReported) != 0;
}

inline void setFastVmJitFailureReportedOf(camel::runtime::GCGraph *graph, bool reported) {
    if (!graph)
        return;
    uintptr_t flags = fastVmFlagsOf(graph);
    flags = reported ? (flags | kFastVmFlagFailureReported) : (flags & ~kFastVmFlagFailureReported);
    graph->execPlan().fastVm.flags = flags;
}

#if ENABLE_FASTVM_JIT
constexpr uintptr_t kFastVmFlagJitFramed = 1u << 2; // the JIT entry is framed code

inline bool fastVmJitFramedOf(camel::runtime::GCGraph *graph) {
    return (fastVmFlagsOf(graph) & kFastVmFlagJitFramed) != 0;
}

inline void setFastVmJitFramedOf(camel::runtime::GCGraph *graph, bool framed) {
    if (!graph)
        return;
    uintptr_t flags = fastVmFlagsOf(graph);
    flags           = framed ? (flags | kFastVmFlagJitFramed) : (flags & ~kFastVmFlagJitFramed);
    graph->execPlan().fastVm.flags = flags;
}

/// Whether the graph runs a loop of regions (`39_loops.md`, section 4; ExecPlan::loops). Such a
/// graph is compiled at its first call even under the on-demand threshold: its cost is the
/// rounds, which a call count cannot see, and the entry function of a program — entered once —
/// can be where a ten-million-round loop is written.
inline bool graphHasLoopRegions(const camel::runtime::GCGraph *graph) {
    return graph != nullptr && !graph->execPlan().loops.empty();
}
#endif

inline uint32_t incFastVmIndirectCallCountOf(camel::runtime::GCGraph *graph) {
    if (!graph) {
        return 0;
    }
    const uintptr_t raw = graph->execPlan().fastVm.indirectCallCount;
    const uint32_t next = raw >= static_cast<uintptr_t>(std::numeric_limits<uint32_t>::max())
                              ? std::numeric_limits<uint32_t>::max()
                              : static_cast<uint32_t>(raw) + 1;
    graph->execPlan().fastVm.indirectCallCount = static_cast<uintptr_t>(next);
    return next;
}

// Direct-call layouts encode callee port slots during bytecode linking. Most calls move scalar
// values between positive dynamic slots, so copying through slotBase() avoids the generic
// Frame::get/set routing while preserving static-slot fallback for closures and constants.
inline slot_t readCallSlot(Frame *frame, data_idx_t index) {
    return LIKELY(index > 0) ? frame->slotBase()[static_cast<size_t>(index)]
                             : frame->get<slot_t>(index);
}

inline void writeCallSlot(Frame *frame, data_idx_t index, slot_t value) {
    if (LIKELY(index > 0)) {
        frame->slotBase()[static_cast<size_t>(index)] = value;
        return;
    }
    frame->set(index, value);
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
        writeCallSlot(calleeFrame, dstSlots[0], readCallSlot(callerFrame, srcArgs[0]));
        return;
    }
    for (size_t argIndex = 0; argIndex < srcArgs.size(); ++argIndex) {
        writeCallSlot(
            calleeFrame,
            dstSlots[argIndex],
            readCallSlot(callerFrame, srcArgs[argIndex]));
    }
}

void FastVMSchedPass::captureCallArgValues(
    Frame *callerFrame, data_arr_t srcArgs, std::vector<slot_t> &out) {
    ASSERT(callerFrame != nullptr, "FastVM direct call source frame is null.");
    out.resize(srcArgs.size());
    if (srcArgs.size() == 1) {
        out[0] = readCallSlot(callerFrame, srcArgs[0]);
        return;
    }
    for (size_t argIndex = 0; argIndex < srcArgs.size(); ++argIndex) {
        out[argIndex] = readCallSlot(callerFrame, srcArgs[argIndex]);
    }
}

void FastVMSchedPass::populateDirectCallFrameFromValues(
    Frame *calleeFrame, data_arr_t dstSlots, std::span<const slot_t> argValues) {
    ASSERT(calleeFrame != nullptr, "FastVM direct call target frame is null.");
    ASSERT(
        argValues.size() == dstSlots.size(),
        "FastVM encoded direct-call layout is arity-mismatched.");
    if (argValues.size() == 1) {
        writeCallSlot(calleeFrame, dstSlots[0], argValues[0]);
        return;
    }
    for (size_t argIndex = 0; argIndex < argValues.size(); ++argIndex) {
        writeCallSlot(calleeFrame, dstSlots[argIndex], argValues[argIndex]);
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

namespace {

const CompileStrategy kFastVmCompileStrategy{
    .enableTailCallDetection = true,
    .enableInlineOperators   = true,
    .optimizationStrategies  = OptimizationStrategyCode::All,
};

// Graphs created while the program runs are compiled into the same bytecode vector, which the
// interpreter and the JIT address by pointer while frames are active: it must never move. Its
// capacity is reserved up front (untouched pages cost no memory).
constexpr size_t kLateBytecodeReserve = size_t{1} << 22; // 4M bytecodes, 32 MiB

} // namespace

void FastVMSchedPass::precompile(
    const camel::runtime::Program &program, camel::runtime::GCGraph *runtimeRoot) {
    ASSERT(runtimeRoot != nullptr, "Runtime root graph is null.");
    auto linked = compileAndLink(context_, program, runtimeRoot, kFastVmCompileStrategy);
    codeSpace_  = std::make_shared<CodeSpace>();
    bytecodes_  = std::move(linked.codes);
    bytecodes_.reserve(bytecodes_.size() + std::max(kLateBytecodeReserve, bytecodes_.size() * 4));
    offsetMap_ = std::move(linked.offsetMap);
    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        if (!runtimeGraph) {
            continue;
        }
        setFastVmEntryPcOf(runtimeGraph, offset);
        setFastVmGraphLengthOf(runtimeGraph, length);
        leaseCode(runtimeGraph, offset, length);
    }
}

void FastVMSchedPass::leaseCode(camel::runtime::GCGraph *graph, size_t start, size_t length) {
    if (&bytecodes_ != &ownBytecodes_) {
        return; // a host's image
    }
    graph->execPlan().fastVm.codeLease =
        std::make_shared<CodeLease>(codeSpace_, start, length);
#if ENABLE_FASTVM_JIT
    // A version that already has its JIT entry (a second lease of the same graph) keeps the entry
    // cleared with the new lease.
    attachJitEntry(graph, fastVmJitEntryOf(graph));
#endif
}

void FastVMSchedPass::compileLate(camel::runtime::GCGraph *graph) {
    // What is compiled is read from the graphs themselves, not from a table keyed by address: a
    // graph created at run time can be collected and its address reused by a new one.
    // Bytecode a freed graph version left is used first. The size is not known before the graph
    // is compiled, so with a free range to try, it is compiled once to be measured (nothing
    // registered), then again where it goes.
    const bool own = &bytecodes_ == &ownBytecodes_;
    size_t at      = bytecodes_.size();
    bool inGap     = false;
    if (own && codeSpace_->hasFree()) {
        const auto probe = compileAndLinkFrom(
            context_,
            programs_.of(graph),
            graph,
            kFastVmCompileStrategy,
            fastVmEntryPcOf,
            at,
            /*registerOrigins=*/false);
        if (const auto start = codeSpace_->allocate(probe.codes.size())) {
            at    = *start;
            inGap = true;
        }
    }
    auto linked = compileAndLinkFrom(
        context_,
        programs_.of(graph),
        graph,
        kFastVmCompileStrategy,
        fastVmEntryPcOf,
        at);
    if (inGap) {
        std::copy(linked.codes.begin(), linked.codes.end(), bytecodes_.begin() + at);
    } else if (bytecodes_.size() + linked.codes.size() > bytecodes_.capacity()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::format(
                "FastVM: no room left to compile graph '{}' created at run time ({} bytecodes in "
                "use)",
                graph->name(),
                bytecodes_.size()));
    }
    if (!inGap) {
        bytecodes_.insert(bytecodes_.end(), linked.codes.begin(), linked.codes.end());
    }
    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        setFastVmEntryPcOf(runtimeGraph, offset);
        setFastVmGraphLengthOf(runtimeGraph, length);
        leaseCode(runtimeGraph, offset, length);
#if ENABLE_FASTVM_JIT
        setJitCompileFailedOf(runtimeGraph, false, true);
#endif
        statsNoteGraph(runtimeGraph, offset);
    }
}

void FastVMSchedPass::push(size_t pc, Frame *frame) {
    if (stackDepth_ + nestedCalls_ >= maxRecursionDepth_ + camel::execute::kEntryCalls) {
        camel::execute::throwRecursionLimitReached(frame->graphName(), maxRecursionDepth_);
    }
    if (stackDepth_ == pcStack_.size()) [[unlikely]] {
        const size_t grown =
            std::min(pcStack_.size() * 2, maxRecursionDepth_ + camel::execute::kEntryCalls);
        pcStack_.resize(grown);
        frameStack_.resize(grown);
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

void FastVMSchedPass::requireNestedCallRoom(const Frame *caller) const {
    // The entry graph's call of main is open too and is not counted (kEntryCalls).
    const size_t openCalls = stackDepth_ + nestedCalls_;
    if (openCalls >= maxRecursionDepth_ + camel::execute::kEntryCalls) {
        camel::execute::throwRecursionLimitReached(caller->graphName(), maxRecursionDepth_);
    }
    if (camel::execute::nativeStackNearlyExhausted()) [[unlikely]] {
        camel::execute::throwNativeStackExhausted(caller->graphName(), openCalls);
    }
}

slot_t FastVMSchedPass::callElement(const HigherOrderCallSite &site, Frame *frame) {
    ++nestedCalls_;
#if ENABLE_FASTVM_JIT
    slot_t result = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
    slot_t result = call(site.entryPc, frame);
#endif
    --nestedCalls_;
    // The result is handed straight to its store: `map` copies the element bytes into the output
    // array, `filter` reads a boolean, `foreach` drops it. A view is therefore harmless here; the
    // stores where a value outlives the element materialise it (H29).
    return result;
}

void FastVMSchedPass::inspectInstruction(size_t pc, Frame *frame) {
    camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
    if (auto sourceContext = context_->sourceContext()) {
        origin = sourceContext->debugMap().pcOrigin(pc);
        sourceContext->setCurrentRuntimeOrigin(origin);
    }
    auto *observer = camel::execute::executionObserver();
    if (observer == nullptr) {
        return;
    }
    const Bytecode &bc = bytecodes_[pc];
    camel::execute::InstructionSite site;
    site.pc     = pc;
    site.frame  = frame;
    site.origin = origin;
    switch (bc.opcode) {
    case OpCode::FUNC:
        site.effect     = camel::execute::InstructionEffect::Call;
        site.resultSlot = bc.result;
        break;
    case OpCode::RETN:
        site.effect = camel::execute::InstructionEffect::Return;
        break;
    case OpCode::TAIL:
        site.effect = camel::execute::InstructionEffect::TailCall;
        break;
    case OpCode::JUMP:
    case OpCode::LTST:
    case OpCode::LEND:
        site.effect = camel::execute::InstructionEffect::Jump; // its result field is no slot
        break;
    default:
        site.resultSlot = bc.result;
        break;
    }
    observer->beforeInstruction(site);
}

void FastVMSchedPass::observeFault(const camel::core::error::RuntimeFault &fault) {
    if (auto *observer = camel::execute::executionObserver()) [[unlikely]] {
        observer->faultRaised(fault, faultIsolationDepth() != 0 && isIsolatableFault(fault.diag()));
    }
}

const camel::runtime::Program *
FastVMSchedPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    (void)os;
    ASSERT(program->root() != nullptr, "FastVM requires a non-null runtime root graph.");

    if (camel::profile::enabled()) {
#if ENABLE_FASTVM_JIT
        camel::profile::setBackend(jitConfig_.policy == jit::JitPolicy::Disabled ? "std::fvm"
                                                                                : "std::jit");
#else
        // A build without the JIT runs this pass as `std::fvm`, whichever name it was given.
        camel::profile::setBackend("std::fvm");
#endif
    }

    camel::runtime::GCGraph *runtimeRoot = program->root();
    camel::execute::RunScope runScope(programs_, *context_, program);
    precompile(*program, runtimeRoot);

    stackDepth_  = 0;
    nestedCalls_ = 0;
    // Written however the run ends (no-op unless std::fvm is given stats="file").
    struct StatsAtExit {
        FastVMSchedPass *vm;
        ~StatsAtExit() {
            vm->statsWrite();
            if (std::getenv("CAMEL_GC_STATS") != nullptr) {
                std::cerr << "fvm_code_bytecodes=" << vm->bytecodes_.size() << '\n';
#if ENABLE_FASTVM_JIT
                // The entries name the graph versions that were compiled on the way; one whose
                // version the collector freed must not be here (it would name a dead graph and
                // the map would grow with every run-time specialization).
                std::cerr << "fvm_jit_entries=" << vm->jitEntryCount() << '\n';
#endif
            }
        }
    } statsAtExit{this};

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
    statsBegin();
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
#else
    statsBegin();
#endif

    opperf::start();
    size_t pc    = graphEntryPc(runtimeRoot);
    Frame *frame = framePool_.acquire(runtimeRoot);
    try {
        slot_t result = call(pc, frame);
        context_->captureProcessExitCode(runtimeRoot, result);
    } catch (...) {
        stackDepth_  = 0;
        nestedCalls_ = 0;
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
    case MarkOpCode::FilterArr:
        evalMarkedOperator_filter_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ReduceArr:
        evalMarkedOperator_reduce_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::ForeachArr:
        evalMarkedOperator_foreach_arr(self, nargs, wargs, currFrame);
        break;
    case MarkOpCode::Isolate:
        evalMarkedOperator_isolate(self, nargs, wargs, currFrame);
        break;
    default:
        ASSERT(false, "Unsupported marked operator in FastVM.");
    }
}

void FastVMSchedPass::evalMarkedOperator_map_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *source = currFrame.get<Array *>(nargs[0]);
    // One decision for the whole call (Array::elementRead): whether an element is read as a
    // view into the array's block, out of the slot layout, or out of its own width.
    const Array::ElementRead readClass = source->elementRead();
    const bool useViews                = readClass == Array::ElementRead::View;
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        // The view points into the block; an inline array is promoted once before the loop.
        source->ensureBlock();
    }
    const size_t arrSize = currFrame.get<Array *>(nargs[0])->size();
    auto *resultType     = currFrame.typeAt<ArrayType>(self);

    Array *res = Array::create(mm::autoSpace(), resultType, arrSize);
    currFrame.set(self, res);

    // The source's read class and the result's write class are fixed here, once; the arrays are
    // re-read on every round because an element callback is a safepoint and may move them.
    dispatchElementAccess(readClass, res->elementWrite(), [&](auto readTag, auto writeTag) {
        constexpr Array::ElementRead kRead   = readTag.value;
        constexpr Array::ElementWrite kWrite = writeTag.value;
        for (size_t i = 0; i < arrSize; ++i) {
            Array *arr                     = currFrame.get<Array *>(nargs[0]);
            Function *func                 = currFrame.get<Function *>(wargs[0]);
            const HigherOrderCallSite site = higherOrderCallSite(func);
            slot_t element                 = arr->readElement<kRead>(i, viewLayout);
            requireNestedCallRoom(&currFrame);
            Frame *frame = framePool_.acquire(site.runtimeGraph);
            populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

            slot_t result = callElement(site, frame);
            currFrame.get<Array *>(self)->writeElement<kWrite>(i, result, resultType);
        }
    });
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *source = currFrame.get<Array *>(nargs[0]);
    const Array::ElementRead readClass = source->elementRead();
    const bool useViews                = readClass == Array::ElementRead::View;
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        source->ensureBlock();
    }
    const size_t arrSize = currFrame.get<Array *>(nargs[0])->size();
    auto *resultType     = currFrame.typeAt<ArrayType>(self);

    // Starts empty: Array::create(n) makes n null elements, and the kept ones were appended
    // after them.
    Array *filtered = Array::create(mm::autoSpace(), resultType, 0);
    currFrame.set(self, filtered);

    dispatchElementRead(readClass, [&](auto readTag) {
        constexpr Array::ElementRead kRead = readTag.value;
        for (size_t i = 0; i < arrSize; ++i) {
            Array *arr                     = currFrame.get<Array *>(nargs[0]);
            Function *func                 = currFrame.get<Function *>(wargs[0]);
            const HigherOrderCallSite site = higherOrderCallSite(func);
            slot_t element                 = arr->readElement<kRead>(i, viewLayout);
            requireNestedCallRoom(&currFrame);
            Frame *frame = framePool_.acquire(site.runtimeGraph);
            populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

            slot_t result = callElement(site, frame);

            if (fromSlot<bool>(result)) {
                currFrame.get<Array *>(self)->append(
                    currFrame.get<Array *>(nargs[0])->readElement<kRead>(i, viewLayout),
                    resultType);
            }
        }
    });

    currFrame.get<Array *>(self)->shrinkToFit(resultType);
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *source = currFrame.get<Array *>(nargs[0]);
    const Array::ElementRead readClass = source->elementRead();
    const bool useViews                = readClass == Array::ElementRead::View;
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        source->ensureBlock();
    }
    const size_t arrSize = currFrame.get<Array *>(nargs[0])->size();
    slot_t init          = currFrame.get<slot_t>(wargs[1]);

    // The accumulator type, resolved once: a value stored into the accumulator outlives the
    // element read that produced it, so the store materialises it (H29).
    const type::Type *accumulatorType = nullptr;
    if (Function *fn = currFrame.get<Function *>(wargs[0]); fn != nullptr) {
        accumulatorType = fn->graph() != nullptr && fn->graph()->funcType() != nullptr
                              ? fn->graph()->funcType()->exitType()
                              : nullptr;
    }

    // Preserve left-fold semantics: an empty array returns the initial value immediately.
    if (arrSize == 0) {
        currFrame.set(self, init);
        return;
    }

    currFrame.set(self, init);

    dispatchElementRead(readClass, [&](auto readTag) {
        constexpr Array::ElementRead kRead = readTag.value;
        for (size_t i = 0; i < arrSize; ++i) {
            Array *arr                     = currFrame.get<Array *>(nargs[0]);
            Function *func                 = currFrame.get<Function *>(wargs[0]);
            const HigherOrderCallSite site = higherOrderCallSite(func);
            const slot_t args[]            = {currFrame.get<slot_t>(self), arr->readElement<kRead>(i, viewLayout)};
            requireNestedCallRoom(&currFrame);
            Frame *frame = framePool_.acquire(site.runtimeGraph);
            populateMarkedCallFrame(frame, func, std::span<const slot_t>(args, 2));

            // The accumulator is the value the next round reads, so it is a value, not a view of the
            // element this round read (H29). The store is the boundary: materialise here.
            const slot_t next = camel::core::rtdata::materializeCallResult(
                callElement(site, frame), accumulatorType);
            CAMEL_ASSERT_NOT_VIEW(next, accumulatorType, "the reduce accumulator");
            currFrame.set(self, next);
        }
    });
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *source = currFrame.get<Array *>(nargs[0]);
    const Array::ElementRead readClass = source->elementRead();
    const bool useViews                = readClass == Array::ElementRead::View;
    const type::DataLayout *viewLayout =
        useViews ? type::pureDataLayout(source->elemType()) : nullptr;
    if (useViews) {
        source->ensureBlock();
    }
    const size_t arrSize = currFrame.get<Array *>(nargs[0])->size();

    dispatchElementRead(readClass, [&](auto readTag) {
        constexpr Array::ElementRead kRead = readTag.value;
        for (size_t i = 0; i < arrSize; ++i) {
            Array *arr                     = currFrame.get<Array *>(nargs[0]);
            Function *func                 = currFrame.get<Function *>(wargs[0]);
            const HigherOrderCallSite site = higherOrderCallSite(func);
            slot_t element                 = arr->readElement<kRead>(i, viewLayout);
            requireNestedCallRoom(&currFrame);
            Frame *frame = framePool_.acquire(site.runtimeGraph);
            populateMarkedCallFrame(frame, func, std::span<const slot_t>(&element, 1));

            callElement(site, frame);
        }
    });

    // foreach has no aggregate return value.
    currFrame.set(self, NullSlot);
}

// The fault-isolation node (docs/technical/33_fault_isolation.md): a call of the function value
// given first, in which a fault of the program becomes the failed form of the node's `T!`.
//
// The callee runs in a nested activation of call() (or in compiled code entered through
// invokeOwnedJitFrame), so the boundary is one activation: its handlers release every frame it
// pushed and its own frame, and bring stackDepth_ back to its base, before the fault arrives
// here. Compiled code never has a C++ exception cross it: it returns to its wrapper with the
// fault recorded, and the wrapper's caller throws.
void FastVMSchedPass::evalMarkedOperator_isolate(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    (void)wargs;
    Function *func = currFrame.get<Function *>(nargs[0]);
    ASSERT(func != nullptr && func->graph() != nullptr, "Fault-isolation callee is null.");
    const HigherOrderCallSite site = higherOrderCallSite(func);

    std::array<slot_t, 8> inlineArgs;
    std::vector<slot_t> heapArgs;
    const size_t argCount = nargs.size() - 1;
    slot_t *args          = inlineArgs.data();
    if (argCount > inlineArgs.size()) {
        heapArgs.resize(argCount);
        args = heapArgs.data();
    }
    for (size_t i = 0; i < argCount; ++i) {
        args[i] = currFrame.get<slot_t>(nargs[i + 1]);
    }

    // The room for the call is the caller's: a recursion limit reached here is a fault of the
    // enclosing graph, outside the boundary.
    requireNestedCallRoom(&currFrame);
    const size_t stackDepthBefore  = stackDepth_;
    const size_t nestedCallsBefore = nestedCalls_;
    Frame *frame                   = framePool_.acquire(site.runtimeGraph);
    populateMarkedCallFrame(frame, func, std::span<const slot_t>(args, argCount));

    slot_t value = NullSlot;
    try {
        camel::core::error::FaultIsolationScope boundary;
        value = callElement(site, frame);
    } catch (const Diagnostic &fault) {
        if (!camel::core::error::isIsolatedFault(fault)) {
            throw;
        }
        ASSERT(
            stackDepth_ == stackDepthBefore,
            "A fault left the call stack changed at its isolation boundary.");
        (void)stackDepthBefore;
        // callElement counts its nested activation without a guard.
        nestedCalls_ = nestedCallsBefore;
        currFrame.set(self, camel::execute::isolatedFaultToError(fault));
        return;
    }
    const auto *calleeType   = site.runtimeGraph->funcType();
    const Type *calleeResult = calleeType ? calleeType->exitType() : nullptr;
    currFrame.set(
        self,
        camel::execute::wrapIsolatedResult(value, calleeResult, currFrame.typeAt<Type>(self)));
}

#if ENABLE_FASTVM_JIT

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
slot_t FastVMSchedPass::invokeOwnedJitFrame(JitEntryFn fn, Frame *frame, void *jitCtx) {
    // Framed code has a driver of its own. What follows is for frameless code only: code that
    // calls nothing, keeps frames on the machine stack, and so must not have a safepoint of the
    // collector under it.
    if (fastVmJitFramedOf(frame->graph())) {
        return runFramedJit(fn, frame, jitCtx);
    }
    slot_t *slots = frame->slotBase();
    slots[0]      = reinterpret_cast<slot_t>(frame);
    auto *jc      = static_cast<JitContext *>(jitCtx);
    // Compiled code counts its own nested calls against what the interpreter's stack has left,
    // so that a recursion ends at the same call whichever of the two runs it.
    jc->depthBudget = maxRecursionDepth_ - std::min(stackDepth_, maxRecursionDepth_);
    // The count does not bound the machine stack its native calls use: they also stop above
    // the reserve of the stack of the thread that runs them, as the interpreters' nested calls.
    jc->stackLimit = camel::execute::nativeStackFloor();
    slot_t result;
    // No safepoint may be reached until the compiled code returns (see mm::detail).
    camel::core::mm::detail::nativeCodeDepth.fetch_add(1, std::memory_order_relaxed);
    try {
        result = fn(slots, jitCtx);
    } catch (...) {
        camel::core::mm::detail::nativeCodeDepth.fetch_sub(1, std::memory_order_relaxed);
        if (framePool_.isActive(frame))
            framePool_.release(frame);
        throw;
    }
    camel::core::mm::detail::nativeCodeDepth.fetch_sub(1, std::memory_order_relaxed);
    // Compiled code cannot throw: it reports a fault by returning with it recorded.
    if (jc->faultKind != 0) [[unlikely]]
        raiseJitFault(jitCtx, frame);
    if (framePool_.isActive(frame))
        framePool_.release(frame);
    return result;
}

void FastVMSchedPass::raiseJitFault(void *jitCtx, Frame *frame) {
    auto *jc        = static_cast<JitContext *>(jitCtx);
    const auto kind = static_cast<x64::JitFaultKind>(jc->faultKind);
    const size_t pc = jc->faultPc;
    // Only meaningful for NativeStackExhausted: the refused call had taken its unit already.
    const size_t budgetLeft = jc->depthBudget;
    jc->faultKind           = 0;
    jc->faultPc             = 0;
    if (kind == x64::JitFaultKind::PendingException) {
        // What a helper of framed code caught: located already where it is a fault.
        std::exception_ptr pending = std::move(jc->pending);
        jc->pending                = nullptr;
        if (framePool_.isActive(frame))
            framePool_.release(frame);
        std::rethrow_exception(pending);
    }
    // The fault is in the graph the frame belongs to (compiled code calls only itself), at the
    // bytecode it recorded: the position the interpreter reports for the same fault.
    const ExecutionSite site =
        makePcExecutionSite(context_->sourceContext(), frame, pc, 0, "", ExecutionSiteKind::JitPc);
    const std::string graphName = frame->graphName();
    if (framePool_.isActive(frame))
        framePool_.release(frame);
    const RuntimeFault fault =
        kind == x64::JitFaultKind::DivisionByZero ? RuntimeFault::make(RuntimeDiag::DivisionByZero)
        : kind == x64::JitFaultKind::MaxRecursionDepthExceeded
            ? RuntimeFault::make(
                  RuntimeDiag::MaxRecursionDepthExceeded,
                  graphName,
                  maxRecursionDepth_)
        : kind == x64::JitFaultKind::NativeStackExhausted
            ? RuntimeFault::make(
                  RuntimeDiag::MaxRecursionDepthExceeded,
                  graphName,
                  camel::execute::nativeStackExhaustedDetail(
                      maxRecursionDepth_ - std::min(budgetLeft + 1, maxRecursionDepth_) +
                      nestedCalls_))
            : RuntimeFault::make(
                  RuntimeDiag::RuntimeError,
                  std::format(
                      "compiled code reported an unknown fault ({})",
                      static_cast<uint32_t>(kind)));
    throw reportRuntimeFault(*context_, fault, site);
}

bool FastVMSchedPass::jitFramedOf(camel::runtime::GCGraph *graph) const {
    return fastVmJitFramedOf(graph);
}

slot_t FastVMSchedPass::runFramedJit(JitEntryFn fn, Frame *frame, void *jitCtx) {
    auto *jc                = static_cast<JitContext *>(jitCtx);
    const size_t depthBase  = stackDepth_;
    const size_t nestedBase = nestedCalls_;
    // The activation that runs, its code, and where in the code it continues (0: its entry).
    Frame *cur       = frame;
    JitEntryFn curFn = fn;
    uint64_t resume  = 0;
    // The bytecode the driver is at, for the position of a fault raised here.
    size_t pc = fastVmEntryPcOf(frame->graph()).value_or(0);

    // Starts `callee` (a frame filled with its arguments) as the next activation. If its graph
    // has framed code, that code becomes `cur` and nothing is returned: the loop enters it.
    // Otherwise the callee runs to its end here, in frameless code or in the interpreter, and
    // its result is returned.
    auto startCallee = [&](camel::runtime::GCGraph *target,
                           Frame *callee) -> std::optional<slot_t> {
        for (;;) {
            JitEntryFn targetFn = jitFnOf(target);
            if (targetFn != nullptr && fastVmJitFramedOf(target)) {
                cur    = callee;
                curFn  = targetFn;
                resume = 0;
                return std::nullopt;
            }
            cur = callee;
            if (targetFn != nullptr) {
                return invokeOwnedJitFrame(targetFn, callee, jitCtx);
            }
            // The interpreter nests on the native stack here; like its own nested calls it
            // is refused when the stack is nearly full.
            if (camel::execute::nativeStackNearlyExhausted()) [[unlikely]] {
                camel::execute::throwNativeStackExhausted(
                    callee->graphName(),
                    stackDepth_ + nestedCalls_);
            }
            if (camel::execute::executionObserver() != nullptr) [[unlikely]] {
                // An observer wants every interpreted activation on its loop (see call()).
                return call(graphEntryPc(target), callee);
            }
            CallResult interpreted = callBorrowed(graphEntryPc(target), callee);
            if (interpreted.tailFn == nullptr) {
                if (interpreted.rootFrame)
                    framePool_.release(interpreted.rootFrame);
                return interpreted.result;
            }
            // It ended in a tail call of compiled code: that code is the callee now.
            callee = interpreted.rootFrame;
            target = callee->graph();
        }
    };

    try {
        for (;;) {
            slot_t *slots  = cur->slotBase();
            slots[0]       = reinterpret_cast<slot_t>(cur);
            jc->exitKind   = static_cast<uint32_t>(x64::JitExitKind::Return);
            jc->resumeAddr = resume;
            slot_t result  = curFn(slots, jitCtx);
            if (jc->faultKind != 0) [[unlikely]] {
                pc = jc->faultPc;
                raiseJitFault(jitCtx, cur);
            }
            const auto exitKind  = static_cast<x64::JitExitKind>(jc->exitKind);
            bool activationEnded = false;
            if (exitKind == x64::JitExitKind::Return) {
                framePool_.release(cur);
                activationEnded = true;
            } else {
                pc           = jc->exitPc;
                resume       = jc->resumeAddr;
                Bytecode *bc = &bytecodes_[pc];
                switch (exitKind) {
                case x64::JitExitKind::Call: {
                    fastVmCallBoundarySafepoint();
                    bc                 = materializeCallTarget(pc, bc);
                    auto *target       = getFuncExtraRuntimeGraph(bc);
                    const size_t depth = stackDepth_;
                    push(pc, cur);
                    if (resumeStack_.size() < pcStack_.size())
                        resumeStack_.resize(pcStack_.size());
                    resumeStack_[depth] = resume;
                    Frame *caller       = cur;
                    Frame *callee       = framePool_.acquire(target);
                    cur                 = callee;
                    populateDirectCallFrame(
                        caller,
                        callee,
                        bc->directCallSrcArgs(),
                        bc->directCallDstSlots());
                    if (auto value = startCallee(target, callee)) {
                        result          = *value;
                        activationEnded = true;
                    }
                    break;
                }
                case x64::JitExitKind::Tail: {
                    fastVmCallBoundarySafepoint();
                    bc           = materializeCallTarget(pc, bc);
                    auto *target = getFuncExtraRuntimeGraph(bc);
                    captureCallArgValues(cur, bc->directCallSrcArgs(), tailArgValuesScratch_);
                    framePool_.release(cur);
                    Frame *callee = framePool_.acquire(target);
                    cur           = callee;
                    populateDirectCallFrameFromValues(
                        callee,
                        bc->directCallDstSlots(),
                        tailArgValuesScratch_);
                    if (auto value = startCallee(target, callee)) {
                        result          = *value;
                        activationEnded = true;
                    }
                    break;
                }
                case x64::JitExitKind::Interp: {
                    const data_arr_t nargs = bc->nargs();
                    const data_arr_t wargs = bc->wargs();
                    if (bc->opcode == OpCode::SCHD) {
                        evalMarkedOperator(bc->extra()->mark, bc->result, nargs, wargs, *cur);
                    } else {
                        ASSERT(bc->opcode == OpCode::CALL, "Unexpected bytecode at a JIT exit.");
                        // A call through a function value, as the interpreters make it: nested
                        // on the native stack and counted.
                        fastVmCallBoundarySafepoint();
                        auto *function = cur->get<Function *>(wargs[0]);
                        ASSERT(function != nullptr, "JIT CALL resolved a null Function callee.");
                        auto *target = programs_.graphOf(function);
                        ASSERT(target != nullptr, "JIT CALL target has no runtime graph.");
                        const uint32_t callCount = noteIndirectCall(target);
                        requireNestedCallRoom(cur);
                        Frame *callee = framePool_.acquire(target);
                        populateIndirectCallFrame(cur, callee, function, nargs, wargs);
                        ++nestedCalls_;
                        const slot_t value = invokeCallOrJit(
                            graphEntryPc(target),
                            target,
                            callee,
                            jitCtx,
                            callCount);
                        --nestedCalls_;
                        cur->set(bc->result, value);
                    }
                    break;
                }
                default:
                    ASSERT(false, "Unknown exit kind of framed JIT code.");
                }
            }
            if (!activationEnded) {
                continue;
            }
            // `result` is the value of an activation whose frame is released. Either it was
            // the one this driver was entered for, or its caller is a framed activation that
            // left for the call: store the result and continue behind the call.
            if (stackDepth_ == depthBase) {
                return result;
            }
            auto [callPc, caller] = pop();
            pc                    = callPc;
            writeCallSlot(caller, bytecodes_[callPc].result, result);
            cur    = caller;
            curFn  = jitFnOf(caller->graph());
            resume = resumeStack_[stackDepth_];
        }
    } catch (...) {
        // As the interpreters: release what this driver holds, bring the call stack back to
        // where it was entered, and pass the fault on; a fault that has no position yet gets
        // the one of the bytecode the driver was at.
        Frame *siteFrame = cur;
        if (framePool_.isActive(cur))
            framePool_.release(cur);
        nestedCalls_ = nestedBase;
        while (stackDepth_ > depthBase) {
            Frame *held = frameStack_[stackDepth_ - 1];
            if (framePool_.isActive(held))
                framePool_.release(held);
            --stackDepth_;
        }
        try {
            throw;
        } catch (const RuntimeFault &fault) {
            throw reportRuntimeFault(
                *context_,
                fault,
                makePcExecutionSite(context_->sourceContext(), siteFrame, pc));
        }
    }
}

camel::runtime::GCGraph *FastVMSchedPass::jitFnToGraph(JitEntryFn fn) const {
    std::lock_guard lock(jitCache_->mutex);
    auto it = jitCache_->byFn.find(fn);
    return it != jitCache_->byFn.end() ? it->second : nullptr;
}

size_t FastVMSchedPass::jitEntryCount() const {
    std::lock_guard lock(jitCache_->mutex);
    return jitCache_->byFn.size();
}

void FastVMSchedPass::attachJitEntry(camel::runtime::GCGraph *graph, JitEntryFn fn) {
    if (!graph || !fn) {
        return;
    }
    {
        std::lock_guard lock(jitCache_->mutex);
        jitCache_->byFn[fn] = graph;
    }
    // The entry names the version: the lease of its bytecode range clears the entry when the
    // collector frees the version. The hook holds the cache weakly, as the version may outlive
    // the pass (and, with it, the cache it names).
    auto *lease = static_cast<CodeLease *>(graph->execPlan().fastVm.codeLease.get());
    if (lease == nullptr) {
        return;
    }
    const std::weak_ptr<JitGraphCache> cache = jitCache_;
    lease->onRelease = [cache, fn] {
        if (const auto alive = cache.lock()) {
            alive->forget(fn);
        }
    };
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

#endif

size_t FastVMSchedPass::graphLengthOf(camel::runtime::GCGraph *graph) const {
    return fastVmGraphLengthOf(graph).value_or(0);
}

#if ENABLE_FASTVM_JIT
CompilationUnit FastVMSchedPass::jitUnitFor(
    camel::runtime::GCGraph *graph, size_t entryPc, const CompilationDebugOptions *debug) const {
    return CompilationUnit{
        .runtimeGraph             = graph,
        .bytecodes                = std::span<const Bytecode>(bytecodes_.data(), bytecodes_.size()),
        .entryPc                  = entryPc,
        .graphLength              = fastVmGraphLengthOf(graph).value_or(0),
        .trampolineFunc           = reinterpret_cast<void *>(&trampolineFunc),
        .trampolineTail           = reinterpret_cast<void *>(&trampolineTail),
        .trampolineOper           = reinterpret_cast<void *>(&trampolineOper),
        .trampolineCast           = reinterpret_cast<void *>(&trampolineCast),
        .trampolineBytecode       = reinterpret_cast<void *>(&trampolineBytecode),
        .poolTopAddr              = const_cast<ctx::FramePool &>(framePool_).topAddr(),
        .directSelfFuncInvokeAddr = reinterpret_cast<void *>(&directSelfFuncInvoke),
        .safepointFlagAddr        = &camel::core::mm::detail::autoSpaceSafepointSlowPath,
        .debug                    = debug,
    };
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
    const CompilationUnit unit =
        jitUnitFor(graph, entryPc, enableJitTraceMir_ ? &debugOptions : nullptr);
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
                "JIT executed code for graph '{}' ({} bytes, pc {} length {}):",
                graph->name(),
                size,
                entryPc,
                fastVmGraphLengthOf(graph).value_or(0));
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
    const bool framed = compiled->framed;
    auto fn           = jitBackend_->load(std::move(compiled));
    if (fn) {
        setFastVmJitFramedOf(graph, framed);
        setJitFnOf(graph, fn);
        attachJitEntry(graph, fn);
        statsNoteCompiled(entryPc);
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
        // What the rounds of the graph's loops became: native code where the shape allowed it,
        // the loop helpers where it did not (and why). The statistics carry the same detail.
        std::string loopSummary;
        {
            std::vector<std::pair<std::string, size_t>> byDetail;
            for (const auto &construct : jitBackend_->runtimeConstructs(unit)) {
                if (construct.kind != "LOOP")
                    continue;
                auto it = std::find_if(
                    byDetail.begin(),
                    byDetail.end(),
                    [&](const auto &entry) { return entry.first == construct.detail; });
                if (it == byDetail.end())
                    byDetail.emplace_back(construct.detail, 1);
                else
                    ++it->second;
            }
            for (const auto &[detail, count] : byDetail) {
                if (!loopSummary.empty())
                    loopSummary += ", ";
                loopSummary += detail == "native" ? "native rounds" : detail;
                if (count > 1)
                    loopSummary += std::format(" ({} loops)", count);
            }
        }
        CAMEL_LOG_INFO_S(
            "JIT",
            "OnDemand: compiled & cached graph '{}' as {} code{}",
            graph->name(),
            framed ? "framed" : "frameless",
            loopSummary.empty() ? std::string() : "; loop rounds: " + loopSummary);
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
    if (tierPolicy_.shouldJit(count, graphHasLoopRegions(runtimeTarget))) {
        compileAndCacheGraph(runtimeTarget, targetPc);
        return &bytecodes_[pc];
    }
    return bc;
}

#endif // ENABLE_FASTVM_JIT

// The four functions below are the interpreter's own: a build without the JIT needs them too.
slot_t FastVMSchedPass::call(size_t pc, Frame *rootFrame) {
#if ENABLE_FASTVM_COMPUTED_GOTO
    // With an execution observer (a debugger) installed, the activation is reported and runs
    // on the loop that has the observer's hooks (inspectvm.cpp). One load of a pointer per
    // nested activation otherwise; the loop of a normal run has no hook at all.
    if (camel::execute::executionObserver() != nullptr) [[unlikely]] {
        camel::execute::ActivationScope observedActivation;
        CallResult inspected = callBorrowedInspected(pc, rootFrame);
        if (inspected.rootFrame)
            framePool_.release(inspected.rootFrame);
        return inspected.result;
    }
#endif
    CallResult result = callBorrowed(pc, rootFrame);
#if ENABLE_FASTVM_JIT
    if (result.tailFn != nullptr) {
        // The interpreted activation ended in a tail call of compiled code (it has returned
        // from the native stack by now).
        return invokeOwnedJitFrame(
            reinterpret_cast<JitEntryFn>(result.tailFn),
            result.rootFrame,
            currentJitCtx_);
    }
#endif
    if (result.rootFrame)
        framePool_.release(result.rootFrame);
    return result.result;
}

HigherOrderCallSite FastVMSchedPass::higherOrderCallSite(Function *func) {
    ASSERT(func != nullptr, "Higher-order call target function is null.");
    camel::runtime::GCGraph *runtimeGraph = programs_.graphOf(func);
    ASSERT(
        runtimeGraph != nullptr,
        "FastVM higher-order runtime call requires a materialized runtime graph.");
    return {.runtimeGraph = runtimeGraph, .entryPc = graphEntryPc(runtimeGraph)};
}

size_t FastVMSchedPass::graphEntryPc(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null.");
    auto pc = fastVmEntryPcOf(graph);
    if (!pc) [[unlikely]] {
        compileLate(graph);
        pc = fastVmEntryPcOf(graph);
    }
    return *pc;
}

uint32_t FastVMSchedPass::noteIndirectCall(camel::runtime::GCGraph *graph) const {
    return incFastVmIndirectCallCountOf(graph);
}

#if ENABLE_FASTVM_JIT

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
    if (!compileFailed && tierPolicy_.shouldJit(callCount, graphHasLoopRegions(runtimeGraph))) {
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
