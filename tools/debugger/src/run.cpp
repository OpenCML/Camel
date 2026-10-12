/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Feb. 22, 2026
 * Updated: Oct. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/windows_parser_guard.h"

// Before any header that pulls in ANTLR, which undefines EOF.
#include "nlohmann/json.hpp"

#include "camel/core/error/diagnostics.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/snapshot.h"
#include "camel/utils/log.h"
#include "camel/execute/entry_args.h"
#include "compile.h"
#include "core/debug_core.h"
#include "run.h"
#include "server.h"
#include "state.h"

#include "passes/trans/dot/graphviz.h"

using namespace camel::core::error;

#ifndef NDEBUG
#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/debug_hook.h"
#endif

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace debugger {

namespace {

/// Pass 快照收集器：经 setPassSnapshotHandler 的 userData 传入（进程内静态，生命周期覆盖
/// runScriptOnce）。钩子回调内立刻把图序列化为 DOT 存入 getPassGraphStore()，不保留
/// GCGraph 指针（GC 会移动/回收）。
struct PassGraphCollector {
    PassGraphStore *store = nullptr;
    context_ptr_t ctx;
    size_t nextIndex = 1; // index 0 保留给 pass 管线之前的入口图
};

void passGraphSnapshotHandler(
    const char *passName, size_t passOutputIndex, camel::runtime::GCGraph *graph, void *userData) {
    (void)passOutputIndex; // 以收集器的单调 nextIndex 为准，跨多次 applyPassesDetailed 不重复
    auto *collector = static_cast<PassGraphCollector *>(userData);
    if (!collector || !collector->store)
        return;
    std::string dot = dumpReadableRuntimeGraph(graph, collector->ctx);
    if (dot.empty())
        return;
    PassGraphEntry entry;
    entry.index = collector->nextIndex++;
    entry.pass  = passName ? passName : "";
    entry.dot   = std::move(dot);
    {
        std::lock_guard<std::mutex> lock(collector->store->mutex);
        collector->store->entries.push_back(std::move(entry));
    }
}

PassGraphCollector &passGraphCollector() {
    static PassGraphCollector collector;
    return collector;
}

/// 退出 runScriptOnce 时清除全局 pass 快照钩子，避免残留回调引用已结束的收集上下文。
struct PassSnapshotGuard {
    ~PassSnapshotGuard() {
        setPassSnapshotHandler(nullptr, nullptr);
        auto &collector = passGraphCollector();
        collector.store = nullptr;
        collector.ctx.reset();
    }
};

/// 管线被某 pass 消费时，最近一条快照的图即为该 pass 的输入，标记 consumed。
void markLastPassGraphConsumed() {
    auto &store = getPassGraphStore();
    std::lock_guard<std::mutex> lock(store.mutex);
    if (!store.entries.empty())
        store.entries.back().consumed = true;
}

} // namespace

void clearRunState() {
    auto &st = getState();
    st.ctx.reset();
    st.parser.reset();
    st.mainModule.reset();
    st.compiledPending = false;
}

namespace {

using debugger::core::DebugCore;

/// The pass sequence as it runs under the debugger, and what the user is told when it is not
/// the one that was asked for (empty otherwise). Only the sequential interpreters have hooks
/// (docs/technical/34_debugger.md, 7.2): compiled code cannot be stopped, so std::jit runs as
/// the interpreter it is built on; the parallel scheduler runs several nodes at once and has no
/// single current node, so it is replaced by std::fvm, one of the orders it may choose.
std::vector<std::string>
debuggablePasses(const std::vector<std::string> &passes, std::string &notice) {
    std::vector<std::string> out;
    for (const std::string &pass : passes) {
        std::string reason;
        if (pass == "std::jit" || pass == "std::fastvm::jit") {
            reason = "compiled code cannot be stopped";
        } else if (pass == "std::tf" || pass == "std::prl" || pass == "std::parallel" ||
                   pass == "std::taskflow") {
            // DESIGN-QUESTION(parallel-debugging)
            reason = "the parallel scheduler runs several nodes at once, so there is no single "
                     "current node; the order of evaluation under the debugger is the "
                     "sequential one of std::fvm";
        }
        if (reason.empty()) {
            out.push_back(pass);
            continue;
        }
        if (!notice.empty()) {
            notice += "\n";
        }
        notice += "[camel-db] " + pass + " runs as std::fvm under the debugger: " + reason + ".";
        out.push_back("std::fvm");
    }
    return out;
}

/// Compiles `targetFile` into the debugger state. The core records the names of the program
/// while the builders run.
bool compileInto(const std::string &targetFile) {
    auto &st = getState();
    st.compiledPending = false;
    auto file = std::make_unique<std::ifstream>(targetFile);
    if (!file->is_open()) {
        std::cout << "Error: cannot open file " << targetFile << std::endl;
        return false;
    }
    CompilationState comp = createCompilationStateForPath(targetFile);
    st.ctx                = comp.ctx;
    st.parser             = comp.parser;
    st.mainModule         = comp.mainModule;

    auto &core = DebugCore::instance();
    core.beginCompile(st.ctx);
    bool ok = false;
    try {
        st.parser->parse(*file);
        st.mainModule->compile(CompileStage::Done);
        ok = st.mainModule->loaded();
        if (!ok) {
            st.ctx->dumpAllModuleDiagnostics(std::cout, false);
        } else {
            // As the command line does: warnings (a call that is never executed is one) go
            // to standard error before anything runs.
            st.ctx->dumpModuleWarnings(std::cerr, false);
        }
    } catch (::Diagnostic &d) {
        std::cout << "Diagnostic error: " << d.toText() << std::endl;
    } catch (std::exception &e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
    core.endCompile(ok ? st.ctx->runtimeRootGraph() : nullptr);
    st.compiledPending = ok;
    return ok;
}

} // namespace

bool compileScript(const std::string &targetFile) {
    const bool ok  = compileInto(targetFile);
    getTaskState() = "loaded";
    return ok;
}

RunOutcome runScriptOnce(const std::string &targetFile) {
    {
        // The snapshots of the previous run are gone with it.
        auto &store = getPassGraphStore();
        std::lock_guard<std::mutex> lock(store.mutex);
        store.entries.clear();
    }

    auto &srv = getServer();
    if (srv.isRunning()) {
        srv.startMemoryScan();
        bool hasAllocBreakSpaces = !srv.getAllocBreakSpaces().empty();
        srv.enableAllocStep(hasAllocBreakSpaces);
        EXEC_WHEN_DEBUG({
            if (hasAllocBreakSpaces) {
                camel::DebugBreakpoint::EnableType("alloc_before");
                camel::DebugBreakpoint::EnableType("alloc");
            } else {
                camel::DebugBreakpoint::DisableType("alloc_before");
                camel::DebugBreakpoint::DisableType("alloc");
            }
        });
    }

    auto &st         = getState();
    const bool quiet = st.quiet;
    if (!quiet) {
        std::string runMsg = "Running " + targetFile;
        std::cout << runMsg << std::endl;
        Logger::WriteToAllStreams(runMsg);
    }
    getTaskState() = "running";

    auto &core = DebugCore::instance();
    // A compilation made for this run (compileScript) is used; otherwise compile now.
    if (!st.compiledPending && !compileInto(targetFile)) {
        getTaskState() = "loaded";
        core.endRun("failed", std::nullopt);
        return RunOutcome::Failed;
    }
    st.compiledPending = false;

    std::string notice;
    const std::vector<std::string> passes = debuggablePasses(st.runPasses, notice);
    // A debug adapter shows the notice from the core's event; other clients read the log.
    if (!notice.empty() && !quiet)
        std::cerr << notice << std::endl;
    core.beginRun(notice);
    // Every exit reports the end of the run to the core.
    std::string outcome = "failed";
    struct EndRun {
        DebugCore &core;
        const std::string &outcome;
        const context_ptr_t &ctx;
        ~EndRun() {
            std::cout << std::flush;
            std::cerr << std::flush;
            core.endRun(
                outcome,
                outcome == "completed" && ctx ? std::optional<int>(ctx->processExitCodeOr(0))
                                              : std::nullopt);
        }
    } endRun{core, outcome, st.ctx};

    const auto failed = [&] {
        const auto &diags = st.ctx->runtimeDiagSink();
        if (diags->hasErrors())
            diags->dump(std::cout, false);
        getTaskState() = "loaded";
        return RunOutcome::Failed;
    };

    try {
        static const std::vector<std::string> defaultFallback{"std::default"};
        st.ctx->clearProcessExitCode();
        requireKnownPasses(passes, st.ctx);
        auto *graph = st.ctx->runtimeRootGraph();
        // The values given for the parameters of `main` become constants of the entry graph
        // before the first pass sees it; what `main` returns goes to standard output.
        graph = camel::execute::bindEntryArgumentSpecs(st.ctx, graph, st.runArgs);
        if (graph == nullptr)
            return failed();
        camel::execute::writeEntryResultTo(st.ctx, std::string());

        // The graph before the passes is snapshot 0 (no pass name); the hook collects the
        // graph each pass returns (from 1). The guard clears the global hook on every exit.
        auto &passGraphStore = getPassGraphStore();
        if (graph && graph->hasNodePayload()) {
            std::string dot = dumpReadableRuntimeGraph(graph, st.ctx);
            if (!dot.empty()) {
                PassGraphEntry entry;
                entry.index = 0;
                entry.dot   = std::move(dot);
                std::lock_guard<std::mutex> lock(passGraphStore.mutex);
                passGraphStore.entries.push_back(std::move(entry));
            }
        }
        auto &collector     = passGraphCollector();
        collector.store     = &passGraphStore;
        collector.ctx       = st.ctx;
        collector.nextIndex = 1;
        setPassSnapshotHandler(&passGraphSnapshotHandler, &collector);
        PassSnapshotGuard snapshotGuard;

        auto result = applyPassesDetailed(graph, passes, st.ctx, std::cout);
        graph       = result.graph;
        if (result.failed())
            return failed();
        if (result.consumed())
            markLastPassGraphConsumed();
        if (!result.consumed()) {
            auto fallbackResult = applyPassesDetailed(graph, defaultFallback, st.ctx, std::cout);
            graph               = fallbackResult.graph;
            if (fallbackResult.failed())
                return failed();
            if (fallbackResult.consumed())
                markLastPassGraphConsumed();
        }
        if (!quiet)
            std::cout << "Run completed." << std::endl;
        getTaskState() = "completed";
        outcome        = "completed";
        return RunOutcome::Completed;
    } catch (const debugger::core::RunAborted &aborted) {
        outcome = "terminated";
        if (aborted.restart)
            return RunOutcome::RestartRequested;
        if (!quiet) {
            std::cout << "Task terminated." << std::endl;
            Logger::WriteToAllStreams("Task terminated.");
        }
        getTaskState() = "terminated";
        return RunOutcome::Terminated;
    } catch (TerminateRequestedException &) {
        outcome = "terminated";
        std::cout << "Task terminated." << std::endl;
        Logger::WriteToAllStreams("Task terminated.");
        getTaskState() = "terminated";
        return RunOutcome::Terminated;
    } catch (RestartRequestedException &) {
        outcome = "terminated";
        return RunOutcome::RestartRequested;
    } catch (::Diagnostic &d) {
        if (!d.persisted && st.ctx) {
            st.ctx->runtimeDiagSink()->add(Diagnostic(d));
        }
        std::cout << "Diagnostic error: " << d.toText() << std::endl;
        getTaskState() = "loaded";
        return RunOutcome::Failed;
    } catch (std::exception &e) {
        std::cout << "Error: " << e.what() << std::endl;
        getTaskState() = "loaded";
        return RunOutcome::Failed;
    }
}

} // namespace debugger
