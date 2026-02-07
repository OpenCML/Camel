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
 * Updated: Feb. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "opperf.h"
#include "utils/log.h"

#if ENABLE_FASTVM_JIT
#include "jit/backend/backend.h"
#include "jit/runtime/trampoline.h"

#include <future>
#endif

using namespace std;
using namespace GraphIR;

graph_ptr_t FastVMSchedPass::apply(graph_ptr_t &graph, std::ostream &os) {
    if (!graph->hasOutput()) {
        context_->rtmDiags()
            ->of(RuntimeDiag::MissingMainFunction)
            .commit(context_->mainModule()->name());
    }

    precompile(graph.get());

    pcStack_.clear();
    frameStack_.clear();

#if ENABLE_FASTVM_JIT
    if (!jitBackend_) {
        jitBackend_ = camel::jit::createBackend();
    }
    camel::jit::JitContext jitCtx{this, bytecodes_.data()};
    currentJitCtx_ = &jitCtx;
    EXEC_WHEN_DEBUG(l.in("JIT").debug(
        "JIT policy: {}",
        jitConfig_.policy == camel::jit::JitPolicy::Disabled
            ? "Disabled"
            : (jitConfig_.policy == camel::jit::JitPolicy::Always ? "Always" : "OnDemand")));
    // Always: 启动时全量预编译；OnDemand: 不预编译，运行时按需编译
    if (jitConfig_.policy == camel::jit::JitPolicy::Always) {
        EXEC_WHEN_DEBUG(
            l.in("JIT").info("JIT Always: compiling {} graph(s) (async)", offsetMap_.size()));
        std::vector<std::pair<GraphIR::Graph *, std::future<camel::jit::CompiledCode *>>> futures;
        for (const auto &[g, entryPc] : offsetMap_) {
            if (jitCache_.count(g))
                continue;
            EXEC_WHEN_DEBUG(l.in("JIT").debug(
                "Submitting async compile: graph '{}' entryPc={}",
                g->name(),
                entryPc));
            auto *backend = jitBackend_.get();
            camel::jit::CompilationUnit unit{
                .graph          = g,
                .bytecodes      = std::span<const Bytecode>(bytecodes_.data(), bytecodes_.size()),
                .entryPc        = entryPc,
                .trampolineFunc = reinterpret_cast<void *>(&trampolineFunc),
                .trampolineTail = reinterpret_cast<void *>(&trampolineTail),
            };
            futures.emplace_back(
                g,
                std::async(std::launch::async, [backend, unit]() -> camel::jit::CompiledCode * {
                    auto compiled = backend->compile(unit);
                    return compiled.release();
                }));
        }
        for (auto &[g, fut] : futures) {
            camel::jit::CompiledCode *raw = fut.get();
            if (raw) {
                auto compiled                    = std::unique_ptr<camel::jit::CompiledCode>(raw);
                [[maybe_unused]] size_t codeSize = compiled->code.size();
                camel::jit::JitEntryFn fn        = jitBackend_->load(std::move(compiled));
                if (fn) {
                    std::lock_guard lock(jitCacheMutex_);
                    jitCache_[g] = fn;
                    EXEC_WHEN_DEBUG(l.in("JIT").info(
                        "Compiled & loaded: graph '{}' codeSize={} bytes",
                        g->name(),
                        codeSize));
                } else {
                    EXEC_WHEN_DEBUG(l.in("JIT").warn("Load failed for graph '{}'", g->name()));
                }
            } else {
                EXEC_WHEN_DEBUG(
                    l.in("JIT").debug("Compile failed/skipped for graph '{}'", g->name()));
            }
        }
    } else if (jitConfig_.policy == camel::jit::JitPolicy::OnDemand) {
        EXEC_WHEN_DEBUG(
            l.in("JIT").info("JIT OnDemand: start with interpreter, compile on hot threshold"));
    }
    GraphIR::Graph *entryGraph = graph.get();
    auto jitIt                 = jitCache_.find(entryGraph);
    if (jitIt != jitCache_.end()) {
        EXEC_WHEN_DEBUG(l.in("JIT").info("Executing entry graph '{}' via JIT", entryGraph->name()));
        Frame *frame = framePool_.acquire(entryGraph);
        opperf::start();
        [[maybe_unused]] slot_t result = jitIt->second(frame->slotBase(), &jitCtx);
        opperf::stop();
        opperf::report(std::cout);
        framePool_.release(frame);
        return Graph::null();
    }
    EXEC_WHEN_DEBUG(l.in("JIT").info(
        "Entry graph '{}' not in JIT cache, falling back to interpreter",
        entryGraph->name()));
#endif

    opperf::start();

    size_t pc    = offsetMap_.at(graph.get());
    Frame *frame = framePool_.acquire(graph.get());
    push(pc, frame);
    call(pc, frame);
    pop();
    framePool_.release(frame);

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

    const auto &retArrType = currFrame.typeAt<ArrayType>(self);
    Array *filtered        = Array::create(mm::autoSpace(), arr->size());

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
            filtered->append(from[i], retArrType);
        }
    }

    filtered->shrinkToFit(retArrType);

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
void FastVMSchedPass::compileAndCacheGraph(GraphIR::Graph *graph, size_t entryPc) {
    if (jitCache_.count(graph))
        return;
    std::lock_guard lock(jitCacheMutex_);
    if (jitCache_.count(graph))
        return;
    FrameMeta *meta = graph->getExtra<FrameMeta, 0>();
    if (!meta)
        meta = installFrameMetaInfoForGraph(graph);

    camel::jit::CompilationUnit unit{
        .graph          = graph,
        .frameMeta      = meta,
        .bytecodes      = std::span<const Bytecode>(bytecodes_.data(), bytecodes_.size()),
        .entryPc        = entryPc,
        .trampolineFunc = reinterpret_cast<void *>(&trampolineFunc),
        .trampolineTail = reinterpret_cast<void *>(&trampolineTail),
        .trampolineOper = reinterpret_cast<void *>(&trampolineOper),
    };
    auto compiled = jitBackend_->compile(unit);
    if (!compiled)
        return;
    auto fn = jitBackend_->load(std::move(compiled));
    if (fn) {
        jitCache_[graph]  = fn;
        jitFnToGraph_[fn] = graph;
        // 回写所有 FUNC 字节码：将 JIT 地址写入 extra，fastop[1]=0 标识已替换
        auto fnPtr = reinterpret_cast<void *>(fn);
        for (size_t pc = 0; pc < bytecodes_.size();) {
            Bytecode &bc = bytecodes_[pc];
            if (bc.opcode == OpCode::FUNC || bc.opcode == OpCode::TAIL) {
                if (bc.fastop[1] != 0 &&
                    getFuncExtraPtr(*bc.extra()) ==
                        reinterpret_cast<void *>(
                            reinterpret_cast<uintptr_t>(graph) & kFuncExtraPtrMask)) {
                    uint32_t count = getFuncExtraCount(*bc.extra());
                    setFuncExtraPacked(*bc.extra(), fnPtr, count);
                    bc.fastop[1] = 0;
                }
            }
            pc += bc.opsize;
        }
        EXEC_WHEN_DEBUG(l.in("JIT").info("OnDemand: compiled & cached graph '{}'", graph->name()));
    }
}

slot_t FastVMSchedPass::invokeCallOrJit(
    size_t pc, GraphIR::Graph *graph, Frame *frame, void *jitCtx, uint32_t callCount) {
    currentJitCtx_ = jitCtx;
    // closure 等无 bytecode 路径仍走 jitCache_
    auto it = jitCache_.find(graph);
    if (it != jitCache_.end()) {
        EXEC_WHEN_DEBUG(
            l.in("JIT").debug("invokeCallOrJit: graph '{}' pc={} -> JIT", graph->name(), pc));
        return it->second(frame->slotBase(), jitCtx);
    }
    if (tierPolicy_.shouldJit(callCount)) {
        compileAndCacheGraph(graph, pc);
        it = jitCache_.find(graph);
        if (it != jitCache_.end()) {
            EXEC_WHEN_DEBUG(l.in("JIT").debug(
                "invokeCallOrJit: graph '{}' pc={} -> JIT (after compile)",
                graph->name(),
                pc));
            return it->second(frame->slotBase(), jitCtx);
        }
    }
    EXEC_WHEN_DEBUG(
        l.in("JIT").debug("invokeCallOrJit: graph '{}' pc={} -> interpreter", graph->name(), pc));
    return call(pc, frame);
}
#endif
