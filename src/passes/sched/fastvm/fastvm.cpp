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
 * Updated: Mar. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "camel/core/module/module.h"
#include "camel/utils/log.h"
#include "opperf.h"

#if ENABLE_FASTVM_JIT
#include "jit/backend/backend.h"
#include "jit/runtime/trampoline.h"

#include <cstdio>
#include <future>
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

void FastVMSchedPass::precompile(GIR::Graph *graph) {
    auto [bytecodes, _, offsetMap] = compileAndLink(
        context_,
        graph,
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });
    bytecodes_ = std::move(bytecodes);
    offsetMap_ = std::move(offsetMap);
}

void FastVMSchedPass::push(size_t pc, Frame *frame) {
    pcStack_.push_back(pc);
    frameStack_.push_back(frame);
    if (frameStack_.size() >= maxRecursionDepth_) {
        throwRuntimeFault(
            RuntimeDiag::MaxRecursionDepthExceeded,
            frame->graph()->name(),
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

graph_ptr_t FastVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        throw reportRuntimeFault(
            *context_,
            RuntimeFault::make(RuntimeDiag::MissingMainFunction, context_->mainModule()->name()),
            makeGraphExecutionSite(context_->sourceContext(), graph.get()));
    }

    precompile(graph.get());

    pcStack_.clear();
    frameStack_.clear();

#if ENABLE_FASTVM_JIT
    if (!jitBackend_) {
        jitBackend_ = createBackend();
    }
    JitContext jitCtx{this, bytecodes_.data()};
    currentJitCtx_ = &jitCtx;
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("JIT").debug(
            "JIT policy: {}",
            jitConfig_.policy == JitPolicy::Disabled
                ? "Disabled"
                : (jitConfig_.policy == JitPolicy::Always ? "Always" : "OnDemand")));
    // Always: 启动时全量预编译；OnDemand: 不预编译，运行时按需编译
    if (jitConfig_.policy == JitPolicy::Always) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT").info(
                "JIT Always: compiling {} graph(s) (async)",
                offsetMap_.size()));
        std::vector<std::pair<GIR::Graph *, std::future<CompiledCode *>>> futures;
        for (const auto &[g, entryPc] : offsetMap_) {
            if (jitCache_.count(g))
                continue;
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("JIT").debug(
                    "Submitting async compile: graph '{}' entryPc={}",
                    g->name(),
                    entryPc));
            auto *backend = jitBackend_.get();
            CompilationUnit unit{
                .graph          = g,
                .bytecodes      = std::span<const Bytecode>(bytecodes_.data(), bytecodes_.size()),
                .entryPc        = entryPc,
                .trampolineFunc = reinterpret_cast<void *>(&trampolineFunc),
                .trampolineTail = reinterpret_cast<void *>(&trampolineTail),
                .trampolineOper = reinterpret_cast<void *>(&trampolineOper),
                .trampolineCast = reinterpret_cast<void *>(&trampolineCast),
            };
            futures.emplace_back(
                g,
                std::async(std::launch::async, [backend, unit]() -> CompiledCode * {
                    auto compiled = backend->compile(unit);
                    return compiled.release();
                }));
        }
        for (auto &[g, fut] : futures) {
            CompiledCode *raw = fut.get();
            if (raw) {
                auto compiled                    = std::unique_ptr<CompiledCode>(raw);
                [[maybe_unused]] size_t codeSize = compiled->code.size();
                JitEntryFn fn                    = jitBackend_->load(std::move(compiled));
                if (fn) {
                    std::lock_guard lock(jitCacheMutex_);
                    jitCache_[g] = fn;
                    EXEC_WHEN_DEBUG(
                        GetDefaultLogger().in("JIT").info(
                            "Compiled & loaded: graph '{}' codeSize={} bytes",
                            g->name(),
                            codeSize));
                } else {
                    EXEC_WHEN_DEBUG(
                        GetDefaultLogger().in("JIT").warn("Load failed for graph '{}'", g->name()));
                }
            } else {
                EXEC_WHEN_DEBUG(
                    GetDefaultLogger().in("JIT").debug(
                        "Compile failed/skipped for graph '{}'",
                        g->name()));
            }
        }
    } else if (jitConfig_.policy == JitPolicy::OnDemand) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT").info(
                "JIT OnDemand: start with interpreter, compile on hot threshold"));
    }
    GIR::Graph *entryGraph = graph.get();
    auto jitIt             = jitCache_.find(entryGraph);
    if (jitIt != jitCache_.end()) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT").info(
                "Executing entry graph '{}' via JIT",
                entryGraph->name()));
        Frame *frame = framePool_.acquire(entryGraph);
        opperf::start();
        try {
            [[maybe_unused]] slot_t result = jitIt->second(frame->slotBase(), &jitCtx);
        } catch (...) {
            framePool_.release(frame);
            throw;
        }
        opperf::stop();
        opperf::report(std::cout);
        framePool_.release(frame);
        return Graph::null();
    }
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("JIT").info(
            "Entry graph '{}' not in JIT cache, falling back to interpreter",
            entryGraph->name()));
#endif

    opperf::start();
    size_t pc    = offsetMap_.at(graph.get());
    Frame *frame = framePool_.acquire(graph.get());
    try {
        call(pc, frame);
        framePool_.release(frame);
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
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    Array *res = Array::create(mm::autoSpace(), arr->size());

    slot_t *from = arr->data();
    slot_t *to   = res->data();
    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, from[i]); // 设置第一个参数
        // 如果有闭包
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

#if ENABLE_FASTVM_JIT
        to[i] = invokeCallOrJit(offsetMap_.at(func->graph()), func->graph(), frame, currentJitCtx_);
#else
        to[i] = call(offsetMap_.at(func->graph()), frame);
#endif
        framePool_.release(frame);
    }

    currFrame.set(self, res);
}

void FastVMSchedPass::evalMarkedOperator_apply_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    slot_t *data = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, data[i]);
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

#if ENABLE_FASTVM_JIT
        data[i] =
            invokeCallOrJit(offsetMap_.at(func->graph()), func->graph(), frame, currentJitCtx_);
#else
        data[i] = call(offsetMap_.at(func->graph()), frame);
#endif
        framePool_.release(frame);
    }

    currFrame.set(self, arr);
}

void FastVMSchedPass::evalMarkedOperator_filter_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    Array *filtered = Array::create(mm::autoSpace(), arr->size());

    slot_t *from = arr->data();
    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, from[i]);
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

#if ENABLE_FASTVM_JIT
        slot_t result =
            invokeCallOrJit(offsetMap_.at(func->graph()), func->graph(), frame, currentJitCtx_);
#else
        slot_t result = call(offsetMap_.at(func->graph()), frame);
#endif
        framePool_.release(frame);

        if (fromSlot<bool>(result)) {
            filtered->append(from[i]);
        }
    }

    filtered->shrinkToFit();

    currFrame.set(self, filtered);
}

void FastVMSchedPass::evalMarkedOperator_reduce_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    slot_t init    = currFrame.get<slot_t>(wargs[1]);
    Tuple *closure = func->tuple();

    // 空数组直接返回初始值
    if (arr->size() == 0) {
        currFrame.set(self, init);
        return;
    }

    slot_t acc   = init;
    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        // reduce(acc, cur)
        frame->set(1, acc);
        frame->set(2, from[i]);

        // 如果有闭包参数
        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 3, closure->get<slot_t>(j));
            }
        }

#if ENABLE_FASTVM_JIT
        acc = invokeCallOrJit(offsetMap_.at(func->graph()), func->graph(), frame, currentJitCtx_);
#else
        acc = call(offsetMap_.at(func->graph()), frame);
#endif
        framePool_.release(frame);
    }

    currFrame.set(self, acc);
}

void FastVMSchedPass::evalMarkedOperator_foreach_arr(
    data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &currFrame) {
    Array *arr     = currFrame.get<Array *>(nargs[0]);
    Function *func = currFrame.get<Function *>(wargs[0]);
    Tuple *closure = func->tuple();

    slot_t *from = arr->data();

    for (size_t i = 0; i < arr->size(); ++i) {
        Frame *frame = framePool_.acquire(func->graph());

        frame->set(1, from[i]);

        if (closure->size() > 0) {
            for (size_t j = 0; j < closure->size(); ++j) {
                frame->set(j + 2, closure->get<slot_t>(j));
            }
        }

#if ENABLE_FASTVM_JIT
        invokeCallOrJit(offsetMap_.at(func->graph()), func->graph(), frame, currentJitCtx_);
#else
        call(offsetMap_.at(func->graph()), frame);
#endif
        framePool_.release(frame);
    }

    // foreach 无返回值
    currFrame.set(self, NullSlot);
}

#if ENABLE_FASTVM_JIT
Frame *FastVMSchedPass::acquireFrameForCall(GIR::Graph *graph) { return framePool_.acquire(graph); }

void FastVMSchedPass::releaseFrameForCall(Frame *frame) { framePool_.release(frame); }

Frame *FastVMSchedPass::acquireFrameForTail(GIR::Graph *graph) {
    Frame *f = framePool_._acquire(graph);
    framePool_._resetTop();
    return f;
}

void FastVMSchedPass::releaseFrameForTail(Frame *frame) { framePool_.release(frame); }

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
slot_t FastVMSchedPass::invokeOwnedJitFrame(JitEntryFn fn, Frame *frame, void *jitCtx) {
    slot_t *slots = frame->slotBase();
    slots[0]      = reinterpret_cast<slot_t>(frame);
    slot_t result = fn(slots, jitCtx);
    framePool_.release(frame);
    return result;
}

Context &FastVMSchedPass::context() { return *context_; }

GIR::Graph *FastVMSchedPass::jitFnToGraph(JitEntryFn fn) const {
    auto it = jitFnToGraph_.find(fn);
    return it != jitFnToGraph_.end() ? it->second : nullptr;
}

void FastVMSchedPass::compileAndCacheGraph(GIR::Graph *graph, size_t entryPc) {
    if (jitCache_.count(graph))
        return;
    std::lock_guard lock(jitCacheMutex_);
    if (jitCache_.count(graph))
        return;
    FrameMeta *meta = graph->getExtra<FrameMeta, 0>();
    if (!meta)
        meta = installFrameMetaInfoForGraph(graph);

    CompilationUnit unit{
        .graph          = graph,
        .frameMeta      = meta,
        .bytecodes      = std::span<const Bytecode>(bytecodes_.data(), bytecodes_.size()),
        .entryPc        = entryPc,
        .trampolineFunc = reinterpret_cast<void *>(&trampolineFunc),
        .trampolineTail = reinterpret_cast<void *>(&trampolineTail),
        .trampolineOper = reinterpret_cast<void *>(&trampolineOper),
        .trampolineCast = reinterpret_cast<void *>(&trampolineCast),
    };
    auto compiled = jitBackend_->compile(unit);
    if (!compiled)
        return;
    // Debug 模式下将实际执行的 code 按 16 进制打印到日志，便于与 bindump 对比
    EXEC_WHEN_DEBUG({
        if (!compiled->code.empty()) {
            const uint8_t *p              = compiled->code.data();
            const size_t size             = compiled->code.size();
            constexpr size_t bytesPerLine = 16;
            GetDefaultLogger().in("JIT").info(
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
                GetDefaultLogger().in("JIT").info("  [{:4}] {}", static_cast<unsigned>(i), line);
            }
        }
    });
    auto fn = jitBackend_->load(std::move(compiled));
    if (fn) {
        jitCache_[graph]  = fn;
        jitFnToGraph_[fn] = graph;
        for (size_t pc = 0; pc < bytecodes_.size();) {
            Bytecode &bc = bytecodes_[pc];
            if (bc.opcode == OpCode::FUNC || bc.opcode == OpCode::TAIL) {
                if (bc.fastop[1] != 0 && getFuncExtraGraph(&bc) == graph)
                    setFuncExtraFn(&bc, reinterpret_cast<void *>(fn));
            }
            pc += bc.opsize;
        }
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT").info(
                "OnDemand: compiled & cached graph '{}'",
                graph->name()));
    }
}

slot_t FastVMSchedPass::invokeCallOrJit(
    size_t pc, GIR::Graph *graph, Frame *frame, void *jitCtx, uint32_t callCount) {
    currentJitCtx_ = jitCtx;
    // closure 等无 bytecode 路径仍走 jitCache_
    auto it = jitCache_.find(graph);
    if (it != jitCache_.end()) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("JIT").debug(
                "invokeCallOrJit: graph '{}' pc={} -> JIT",
                graph->name(),
                pc));
        return it->second(frame->slotBase(), jitCtx);
    }
    if (tierPolicy_.shouldJit(callCount)) {
        compileAndCacheGraph(graph, pc);
        it = jitCache_.find(graph);
        if (it != jitCache_.end()) {
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("JIT").debug(
                    "invokeCallOrJit: graph '{}' pc={} -> JIT (after compile)",
                    graph->name(),
                    pc));
            return it->second(frame->slotBase(), jitCtx);
        }
    }
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("JIT").debug(
            "invokeCallOrJit: graph '{}' pc={} -> interpreter",
            graph->name(),
            pc));
    return call(pc, frame);
}
#endif
