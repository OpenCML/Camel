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
 * Updated: Mar. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "windows_parser_guard.h"

#include "camel/core/error/diagnostics.h"
#include "camel/execute/pass/base.h"
#include "camel/utils/log.h"
#include "compile.h"
#include "run.h"
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

RunOutcome runScriptOnce(const std::string &targetFile) {
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
        int retCode                     = applyPasses(passes, st.ctx, std::cout);
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
