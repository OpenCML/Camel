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
 * Updated: Feb. 26, 2026
 * Supported by: National Key Research and Development Program of China
 */
#include "run.h"
#include "camel/core/error/diagnostics.h"
#include "camel/execute/pass/base.h"
#include "camel/utils/log.h"
#include "compile.h"
#include "server.h"
#include "state.h"

#ifndef NDEBUG
#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/debug_hook.h"
#endif

#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace debugger {

void clearRunState() {
    auto &st = getState();
    st.ctx.reset();
    st.parser.reset();
    st.mainModule.reset();
}

RunOutcome
runScriptOnce(const std::string &targetFile, bool enableMemoryMonitor, bool enableAllocStep) {
    auto file = std::make_unique<std::ifstream>(targetFile);
    if (!file->is_open()) {
        std::cout << "Error: cannot open file " << targetFile << std::endl;
        getTaskState() = "loaded";
        return RunOutcome::Failed;
    }

    auto &srv = getServer();
    if (srv.isRunning()) {
        if (enableMemoryMonitor)
            srv.startMemoryScan();
        if (enableAllocStep) {
            srv.enableAllocStep(true);
#ifndef NDEBUG
            camel::DebugBreakpoint::EnableType("alloc_before");
            camel::DebugBreakpoint::EnableType("alloc");
#endif
        } else {
            srv.enableAllocStep(false);
#ifndef NDEBUG
            camel::DebugBreakpoint::DisableType("alloc_before");
            camel::DebugBreakpoint::DisableType("alloc");
#endif
        }
    }

    std::string runMsg = "Running " + targetFile +
                         " (memory monitor=" + (enableMemoryMonitor ? "on" : "off") +
                         ", alloc step=" + (enableAllocStep ? "on" : "off") + ")";
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

        std::vector<std::string> passes;
        int retCode = applyPasses(passes, st.ctx, std::cout);
        if (retCode != 0) {
            const auto &diags = st.ctx->rtmDiags();
            if (diags->hasErrors())
                diags->dump(std::cout, false);
            getTaskState() = "loaded";
            return RunOutcome::Failed;
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
