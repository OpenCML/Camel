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
 * Updated: Apr. 10, 2026
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
using namespace GIR;
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
constexpr size_t kFastVmRootCacheSlot          = 4;
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

inline FastVMRuntimeRootCache *fastVmRootCacheOf(camel::runtime::GCGraph *graph) {
    return graph
               ? reinterpret_cast<FastVMRuntimeRootCache *>(graph->extraSlot(kFastVmRootCacheSlot))
               : nullptr;
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

inline void setFastVmRootCacheOf(camel::runtime::GCGraph *graph, FastVMRuntimeRootCache *cache) {
    if (graph)
        graph->setExtraSlot(kFastVmRootCacheSlot, reinterpret_cast<uintptr_t>(cache));
}

struct HigherOrderCallSite {
    camel::runtime::GCGraph *runtimeGraph = nullptr;
    size_t entryPc                        = 0;
    const slot_t *closure                 = nullptr;
    size_t closureSize                    = 0;
};

inline HigherOrderCallSite makeHigherOrderCallSite(Function *func) {
    ASSERT(func != nullptr, "Higher-order call target function is null.");
    Tuple *closure                        = func->tuple();
    const size_t closureSize              = closure ? closure->size() : 0;
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
        .closure      = closureSize == 0 ? nullptr : closure->data(),
        .closureSize  = closureSize,
    };
}

inline void seedClosureSlots(Frame *frame, const HigherOrderCallSite &site, size_t baseSlot) {
    for (size_t i = 0; i < site.closureSize; ++i) {
        frame->set(baseSlot + i, site.closure[i]);
    }
}

} // namespace

FastVMSchedPass::~FastVMSchedPass() = default;

void FastVMSchedPass::precompile(camel::runtime::GCGraph *runtimeRoot) {
    ASSERT(runtimeRoot != nullptr, "Runtime root graph is null.");
    runtimeRoot_ = runtimeRoot;
    auto linked  = compileAndLink(
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
    auto rootCache = std::make_unique<FastVMRuntimeRootCache>();
    rootCache->callTargetsByPc.resize(bytecodes_.size(), nullptr);
    for (size_t pc = 0; pc < bytecodes_.size();) {
        const Bytecode &bc = bytecodes_[pc];
        if (bc.opcode == OpCode::FUNC || bc.opcode == OpCode::TAIL) {
            rootCache->callTargetsByPc[pc] = getFuncExtraRuntimeGraph(&bc);
        }
        pc += bc.opsize;
    }
    FastVMRuntimeRootCache *rootCacheRaw = rootCache.get();
    runtimeRootCaches_.push_back(std::move(rootCache));
    setFastVmRootCacheOf(runtimeRoot_, rootCacheRaw);
}

void FastVMSchedPass::push(size_t pc, Frame *frame) {
    pcStack_.push_back(pc);
    frameStack_.push_back(frame);
    if (frameStack_.size() >= maxRecursionDepth_) {
        throwRuntimeFault(
            RuntimeDiag::MaxRecursionDepthExceeded,
            frame->graphName(),
            maxRecursionDepth_);
    }
}

std::pair<size_t, Frame *> FastVMSchedPass::pop() {
    size_t pc = pcStack_.back();
    pcStack_.pop_back();
    Frame *frame = frameStack_.back();
    frameStack_.pop_back();
    return {pc, frame};
}

graph_ptr_t FastVMSchedPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    (void)os;
    ASSERT(graph != nullptr, "FastVM requires a non-null runtime root graph.");

    camel::runtime::GCGraph *runtimeRoot = graph;
    runtimeRootCaches_.clear();
    precompile(runtimeRoot);

    pcStack_.clear();
    frameStack_.clear();

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
        return Graph::null();
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
        pcStack_.clear();
        frameStack_.clear();
        throw;
    }
    opperf::stop();
    opperf::report(std::cout);
    return Graph::null();
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
    Array *arr                     = currFrame.get<Array *>(nargs[0]);
    Function *func                 = currFrame.get<Function *>(wargs[0]);
    const HigherOrderCallSite site = makeHigherOrderCallSite(func);

    Array *res = Array::create(mm::autoSpace(), arr->size());

    slot_t *from = arr->data();
    slot_t *to   = res->data();
    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(site.runtimeGraph);

        frame->set(1, from[i]); // Bind the current element to the callee's first argument slot.
        if (site.closureSize != 0) {
            seedClosureSlots(frame, site, 2);
        }

#if ENABLE_FASTVM_JIT
        to[i] = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        to[i] = call(site.entryPc, frame);
#endif
    }

    currFrame.set(self, res);
}

void FastVMSchedPass::evalMarkedOperator_apply_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr                     = currFrame.get<Array *>(nargs[0]);
    Function *func                 = currFrame.get<Function *>(wargs[0]);
    const HigherOrderCallSite site = makeHigherOrderCallSite(func);

    slot_t *data = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(site.runtimeGraph);

        frame->set(1, data[i]);
        if (site.closureSize != 0) {
            seedClosureSlots(frame, site, 2);
        }

#if ENABLE_FASTVM_JIT
        data[i] = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        data[i] = call(site.entryPc, frame);
#endif
    }

    currFrame.set(self, arr);
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr                     = currFrame.get<Array *>(nargs[0]);
    Function *func                 = currFrame.get<Function *>(wargs[0]);
    const HigherOrderCallSite site = makeHigherOrderCallSite(func);

    Array *filtered = Array::create(mm::autoSpace(), arr->size());

    slot_t *from = arr->data();
    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(site.runtimeGraph);

        frame->set(1, from[i]);
        if (site.closureSize != 0) {
            seedClosureSlots(frame, site, 2);
        }

#if ENABLE_FASTVM_JIT
        slot_t result = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        slot_t result = call(site.entryPc, frame);
#endif

        if (fromSlot<bool>(result)) {
            filtered->append(from[i]);
        }
    }

    filtered->shrinkToFit();

    currFrame.set(self, filtered);
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr                     = currFrame.get<Array *>(nargs[0]);
    Function *func                 = currFrame.get<Function *>(wargs[0]);
    slot_t init                    = currFrame.get<slot_t>(wargs[1]);
    const HigherOrderCallSite site = makeHigherOrderCallSite(func);

    // Preserve left-fold semantics: an empty array returns the initial value immediately.
    if (arr->size() == 0) {
        currFrame.set(self, init);
        return;
    }

    slot_t acc   = init;
    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(site.runtimeGraph);

        // reduce(acc, cur)
        frame->set(1, acc);
        frame->set(2, from[i]);

        if (site.closureSize != 0) {
            seedClosureSlots(frame, site, 3);
        }

#if ENABLE_FASTVM_JIT
        acc = invokeCallOrJit(site.entryPc, site.runtimeGraph, frame, currentJitCtx_);
#else
        acc = call(site.entryPc, frame);
#endif
    }

    currFrame.set(self, acc);
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr                     = currFrame.get<Array *>(nargs[0]);
    Function *func                 = currFrame.get<Function *>(wargs[0]);
    const HigherOrderCallSite site = makeHigherOrderCallSite(func);

    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(site.runtimeGraph);

        frame->set(1, from[i]);

        if (site.closureSize != 0) {
            seedClosureSlots(frame, site, 2);
        }

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
                if (bc.fastop[1] >= 0 && runtimeCallTarget(pc) == graph)
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
    if (!bc || bc->fastop[1] < 0) {
        return bc;
    }
    if (!jitEnabled()) {
        return bc;
    }
    auto *runtimeTarget = runtimeCallTarget(pc);
    ASSERT(runtimeTarget != nullptr, "FastVM direct call target is not materialized.");
    if (jitCompileFailedOf(runtimeTarget)) {
        return bc;
    }
    size_t targetPc = static_cast<size_t>(bc->fastop[1]);
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

camel::runtime::GCGraph *FastVMSchedPass::runtimeCallTarget(size_t pc) const {
    if (auto *cache = fastVmRootCacheOf(runtimeRoot_)) {
        return pc < cache->callTargetsByPc.size() ? cache->callTargetsByPc[pc] : nullptr;
    }
    return nullptr;
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
