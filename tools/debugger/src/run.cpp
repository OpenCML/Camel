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

#include "camel/core/error/diagnostics.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/snapshot.h"
#include "camel/utils/log.h"
#include "compile.h"
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
}

RunOutcome runScriptOnce(const std::string &targetFile) {
    {
        // 每次执行前清空上一次的逐 pass 快照。
        auto &store = getPassGraphStore();
        std::lock_guard<std::mutex> lock(store.mutex);
        store.entries.clear();
    }

    auto file = std::make_unique<std::ifstream>(targetFile);
    if (!file->is_open()) {
        std::cout << "Error: cannot open file " << targetFile << std::endl;
        getTaskState() = "loaded";
        return RunOutcome::Failed;
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

    std::string runMsg = "Running " + targetFile;
    std::cout << runMsg << std::endl;
    Logger::WriteToAllStreams(runMsg);
    getTaskState() = "running";

    CompilationState comp = createCompilationStateForPath(targetFile);
    auto &st              = getState();
    st.ctx                = comp.ctx;
    st.parser             = comp.parser;
    st.mainModule         = comp.mainModule;

    try {
        st.parser->parse(*file);
        st.mainModule->compile(CompileStage::Done);

        if (!st.mainModule->loaded()) {
            st.ctx->dumpAllModuleDiagnostics(std::cout, false);
            getTaskState() = "loaded";
            return RunOutcome::Failed;
        }

        std::vector<std::string> passes = getState().runPasses;
        static const std::vector<std::string> defaultFallback{"std::default"};
        auto *graph = st.ctx->runtimeRootGraph();

        // 逐 pass GIR 快照：先存入口图（index 0，pass 为空），再安装快照钩子收集各 pass 的
        // 输出图（index 从 1 开始）。guard 保证所有退出路径都清除全局钩子。
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
        if (result.failed()) {
            const auto &diags = st.ctx->runtimeDiagSink();
            if (diags->hasErrors())
                diags->dump(std::cout, false);
            getTaskState() = "loaded";
            return RunOutcome::Failed;
        }
        if (result.consumed())
            markLastPassGraphConsumed();
        if (!result.consumed()) {
            auto fallbackResult = applyPassesDetailed(graph, defaultFallback, st.ctx, std::cout);
            graph               = fallbackResult.graph;
            if (fallbackResult.failed()) {
                const auto &diags = st.ctx->runtimeDiagSink();
                if (diags->hasErrors())
                    diags->dump(std::cout, false);
                getTaskState() = "loaded";
                return RunOutcome::Failed;
            }
            if (fallbackResult.consumed())
                markLastPassGraphConsumed();
        }
        std::cout << "Run completed." << std::endl;
        getTaskState() = "completed";
        return RunOutcome::Completed;
    } catch (TerminateRequestedException &) {
        std::cout << "Task terminated." << std::endl;
        Logger::WriteToAllStreams("Task terminated.");
        getTaskState() = "terminated";
        return RunOutcome::Terminated;
    } catch (RestartRequestedException &) {
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
