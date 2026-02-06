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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fastvm.h"
#include "opperf.h"
#include "utils/log.h"

#if ENABLE_JIT
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

#if ENABLE_JIT
    if (!jitBackend_) {
        jitBackend_ = camel::jit::createBackend();
    }
    camel::jit::JitContext jitCtx{this, bytecodes_.data()};
    EXEC_WHEN_DEBUG(l.in("JIT").debug(
        "JIT policy: {}",
        jitConfig_.policy == camel::jit::JitPolicy::Disabled ? "Disabled" : "OnDemand"));
    if (jitConfig_.policy != camel::jit::JitPolicy::Disabled) {
        EXEC_WHEN_DEBUG(l.in("JIT").info(
            "JIT enabled, compiling {} graph(s) (policy=OnDemand, async)",
            offsetMap_.size()));
        // 异步并行编译：在 worker 线程执行 compile()，避免主线程阻塞
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
                    return compiled.release(); // caller owns
                }));
        }
        // 主线程收集编译结果并 load（load 涉及内存分配，顺序执行）
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
    }
    GraphIR::Graph *entryGraph = graph.get();
    auto jitIt                 = jitCache_.find(entryGraph);
    if (jitIt != jitCache_.end()) {
        EXEC_WHEN_DEBUG(l.in("JIT").info("Executing entry graph '{}' via JIT", entryGraph->name()));
        Frame *frame = framePool_.acquire(entryGraph);
        opperf::start();
        [[maybe_unused]] slot_t result = jitIt->second(frame, &jitCtx);
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

        to[i] = call(offsetMap_.at(func->graph()), frame);
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

        data[i] = call(offsetMap_.at(func->graph()), frame);
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

        slot_t result = call(offsetMap_.at(func->graph()), frame);
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

        acc = call(offsetMap_.at(func->graph()), frame);
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

        call(offsetMap_.at(func->graph()), frame);
        framePool_.release(frame);
    }

    // foreach 无返回值
    currFrame.set(self, NullSlot);
}

#if ENABLE_JIT
slot_t
FastVMSchedPass::invokeCallOrJit(size_t pc, GraphIR::Graph *graph, Frame *frame, void *jitCtx) {
    auto it = jitCache_.find(graph);
    if (it != jitCache_.end()) {
        EXEC_WHEN_DEBUG(
            l.in("JIT").debug("invokeCallOrJit: graph '{}' pc={} -> JIT", graph->name(), pc));
        return it->second(frame, jitCtx);
    }
    EXEC_WHEN_DEBUG(
        l.in("JIT").debug("invokeCallOrJit: graph '{}' pc={} -> interpreter", graph->name(), pc));
    return call(pc, frame);
}
#endif
