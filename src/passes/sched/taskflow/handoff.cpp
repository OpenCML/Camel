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
 * Created: Oct. 14, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// The bytecode hand-off of the parallel scheduler: which graphs are serial
// (prepareBytecodeHandoff; the rule is stated at "Bytecode hand-off" in taskflow.h), and the
// per-thread interpreters that run them (TaskflowBytecodeHandoff, handoff.h).

#include "handoff.h"

#include "camel/core/global_config.h"
#include "camel/core/operator_table.h"
#include "camel/execute/executor.h"
#include "camel/execute/observer.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"

#include <cstdlib>
#include <format>
#include <iostream>
#include <unordered_map>

using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;

// ---------------------------------------------------------------------------------------------
// TaskflowBytecodeHandoff
// ---------------------------------------------------------------------------------------------

TaskflowBytecodeHandoff::TaskflowBytecodeHandoff(ctx::context_ptr_t context)
    : context_(std::move(context)) {}

// The interpreters go before the image they run (members are destroyed in reverse order).
TaskflowBytecodeHandoff::~TaskflowBytecodeHandoff() = default;

uint64_t TaskflowBytecodeHandoff::nextRunId() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

void TaskflowBytecodeHandoff::build(std::span<GCGraph *const> serialGraphs) {
    image_.build(context_, serialGraphs);
}

TaskflowBytecodeHandoff::Interpreter &TaskflowBytecodeHandoff::interpreterOfThisThread() {
    struct Slot {
        uint64_t runId           = 0; // never a run: ids start at 1
        Interpreter *interpreter = nullptr;
    };
    thread_local Slot slot;
    if (slot.runId == runId_) [[likely]] {
        return *slot.interpreter;
    }
    // The thread's first hosted call of this run. Constructing the interpreter registers its
    // frame pool with the allocator (under the allocator's own lock); the pool is empty until
    // the caller takes a frame from it.
    auto owned = std::make_unique<Interpreter>(context_, image_.codes());
    slot       = {runId_, owned.get()};
    std::lock_guard lock(interpretersMutex_);
    interpreters_.push_back(std::move(owned));
    return *slot.interpreter;
}

void TaskflowBytecodeHandoff::pollSafepoint(void *host, std::string_view reason) {
    // What evalGraph does at a graph boundary, and then back into the region: the interpreter
    // goes on reading frames as soon as this returns.
    auto &region = *static_cast<TaskflowGcRegion *>(host);
    region.safepoint(reason);
    region.enter();
}

TaskflowBytecodeHandoff::Stats TaskflowBytecodeHandoff::stats() {
    Stats total;
    total.graphs    = image_.entries().size();
    total.bytecodes = image_.codes().size();
    std::lock_guard lock(interpretersMutex_);
    total.interpreters = interpreters_.size();
    for (const auto &interpreter : interpreters_) {
        total.calls += interpreter->calls;
    }
    return total;
}

// ---------------------------------------------------------------------------------------------
// Which graphs are serial
// ---------------------------------------------------------------------------------------------

TaskflowExecSchedPass::TaskflowExecSchedPass(
    const ctx::context_ptr_t &ctx, size_t max_concurrent_tasks)
    : RuntimeGraphSchedulePass(ctx), executor_(workerCount(ctx, max_concurrent_tasks)) {
    if (ctx) {
        // The same count as on the other schedulers, with the same default (design
        // principles 3.9).
        maxRecursionDepth_ = camel::execute::recursionLimit(*ctx).frames;
    }
}

TaskflowExecSchedPass::~TaskflowExecSchedPass() = default;

std::string_view TaskflowExecSchedPass::serialObstacle(const GraphPlan &plan) const {
    GCGraph *graph = plan.graph;
    if (!graph->hasFrameLayout()) {
        return "it has no frame layout";
    }
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        const auto *node = *it;
        if (node == nullptr) {
            continue;
        }
        switch (node->kind) {
        case GCNodeKind::Call:
            return "it calls a function value";
        case GCNodeKind::Func:
            if (graph->directCalleeGraphOf(it.ref()) == nullptr) {
                return "a direct call has no callee graph";
            }
            break;
        case GCNodeKind::Oper: {
            const auto *body = graph->nodeBodyAs<GCOperBody>(it.ref());
            if (body == nullptr) {
                return "an operator has no body";
            }
            // The higher-order operators and `attempt` declare that they call function values
            // (OperatorTraits::callsGraphs), and a marked operator is always one of them.
            const auto &table = camel::core::OperatorTable::instance();
            const auto traits = table.find(body->oper);
            if (traits && traits->callsGraphs) {
                return "it has a higher-order operator";
            }
            if (!context_->execMgr().findWithoutImport(std::string(body->uri()))) {
                return "an operator of it is not loaded yet";
            }
            break;
        }
        default:
            break;
        }
    }
    // A plan that is not structured is evaluated node by node in the linear order: nothing of
    // it runs as a task graph.
    if (plan.structured) {
        for (const auto &scope : plan.scopes) {
            if (!scope->sequential) {
                return "a scope of it runs as a task graph";
            }
        }
    }
    return {};
}

void TaskflowExecSchedPass::prepareBytecodeHandoff(GCGraph *root) {
    handoff_.reset();
    const bool diag = options_.diag;
    const auto off  = [&](std::string_view why) {
        if (diag) {
            std::cerr << std::format("[taskflow] bytecode hand-off is off: {}\n", why);
        }
    };
    if (!options_.bytecode) {
        return off("std::tf(bytecode=false)");
    }
    if (forceLinear_) {
        return off("std::tf(linear=true) asks for the node-by-node evaluator");
    }
    // The interpreter reports to an execution observer from whichever thread runs it, and in
    // inspection mode it keeps one "current origin" for the whole process: both are for one
    // thread. (A debugger runs a program on std::fvm instead of this scheduler anyway.)
    if (camel::execute::executionObserver() != nullptr ||
        InternalGlobalConfig::IsInspectionMode()) {
        return off("an execution observer or the inspection mode is active");
    }

    // Every graph the run can reach without creating one, in a fixed order (the order of the
    // image). Their plans are built here; the run would build them at their first call.
    const std::vector<GCGraph *> graphs = camel::runtime::collectReachableGraphs(root);
    std::unordered_map<GCGraph *, size_t> indexOf;
    indexOf.reserve(graphs.size());
    for (size_t i = 0; i < graphs.size(); ++i) {
        indexOf.emplace(graphs[i], i);
    }

    // Conditions 1 to 4 for each graph by itself, and who calls whom directly.
    std::vector<std::string_view> obstacle(graphs.size());
    std::vector<std::vector<size_t>> callersOf(graphs.size());
    std::vector<size_t> spoiled; // not serial, callers not yet told
    for (size_t i = 0; i < graphs.size(); ++i) {
        GCGraph *graph = graphs[i];
        // Original graphs retained by a private copy are reachable too. A runtime
        // callback may hand one back, but its LOOP must first get a private execution
        // graph; do not attach bytecode for the source to that execution graph's plan.
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if ((*it)->kind == GCNodeKind::Loop) {
                obstacle[i] = "a source loop requires private lowering when called";
                break;
            }
        }
        if (obstacle[i].empty()) {
            obstacle[i] = serialObstacle(*planFor(graph));
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto *node = *it;
            if (node == nullptr || node->kind != GCNodeKind::Func) {
                continue;
            }
            const auto callee = indexOf.find(graph->directCalleeGraphOf(it.ref()));
            if (callee != indexOf.end()) {
                callersOf[callee->second].push_back(i);
            } else if (obstacle[i].empty()) {
                obstacle[i] = "it calls a graph outside the program's graphs";
            }
        }
        if (!obstacle[i].empty()) {
            spoiled.push_back(i);
        }
    }
    // Closure under direct calls: a graph that calls a graph that is not serial is not serial.
    // What is left is the largest set that satisfies the definition; a recursion whose bodies
    // all pass stays in it.
    while (!spoiled.empty()) {
        const size_t callee = spoiled.back();
        spoiled.pop_back();
        for (size_t caller : callersOf[callee]) {
            if (obstacle[caller].empty()) {
                obstacle[caller] = "it calls a graph that is not serial";
                spoiled.push_back(caller);
            }
        }
    }

    std::vector<GCGraph *> serial;
    for (size_t i = 0; i < graphs.size(); ++i) {
        if (obstacle[i].empty()) {
            serial.push_back(graphs[i]);
        }
    }
    if (diag) {
        for (size_t i = 0; i < graphs.size(); ++i) {
            if (obstacle[i].empty()) {
                std::cerr << std::format(
                    "[taskflow] graph '{}' is serial: runs as bytecode\n",
                    graphs[i]->name());
            } else {
                std::cerr << std::format(
                    "[taskflow] graph '{}' stays on the scheduler: {}\n",
                    graphs[i]->name(),
                    obstacle[i]);
            }
        }
    }
    if (serial.empty()) {
        return;
    }

    auto handoff = std::make_unique<TaskflowBytecodeHandoff>(context_);
    try {
        handoff->build(serial);
    } catch (const std::exception &error) {
        // The hand-off is an optimisation and the evaluator runs every graph: a graph the
        // bytecode compiler refuses (std::fvm would refuse the program) is no reason to fail a
        // run that may never reach it.
        return off(std::format("the bytecode compiler failed: {}", error.what()));
    }
    for (const auto &[graph, pc] : handoff->entries()) {
        planFor(graph)->bytecodeEntry = pc;
    }
    handoff_ = std::move(handoff);
}
