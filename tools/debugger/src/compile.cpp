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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "windows_parser_guard.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/userdef.h"
#include "camel/parse/parse.h"
#include "camel/utils/env.h"
#include "compile.h"
#include "gir_json.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace debugger {

CompilationState createCompilationStateForPath(const std::string &path) {
    fs::path camelPath = fs::current_path();
    fs::path entryPath(path);
    std::string entryDir = fs::absolute(entryPath).parent_path().string();
    auto addIfNonEmpty   = [](std::vector<std::string> &v, const std::string &s) {
        if (!s.empty())
            v.push_back(fs::absolute(fs::path(s)).string());
    };
    std::vector<std::string> searchPaths;
    searchPaths.push_back(entryDir);
    addIfNonEmpty(searchPaths, getEnv("CAMEL_PACKAGES"));
    addIfNonEmpty(searchPaths, getEnv("CAMEL_STD_LIB"));
    addIfNonEmpty(searchPaths, (camelPath / "stdlib").string());

    auto ctx = Context::create(
        EntryConfig{.entryDir = entryDir, .entryFile = path, .searchPaths = std::move(searchPaths)},
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});
    ctx->setRuntimeDiagMode(RuntimeDiagMode::DebuggerAttached);
    auto diagnostics = std::make_shared<Diagnostics>("main", path, ctx->sourceContext());
    diagnostics->setConfig(
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});
    auto parser = std::make_shared<CamelParser>(diagnostics);
    parser->setSourceContext(ctx->sourceContext());
    auto mainModule = std::make_shared<UserDefinedModule>("main", path, ctx, parser);
    ctx->setMainModule(mainModule);
    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();
    return {ctx, parser, mainModule};
}

namespace {

std::pair<std::string, std::string> getGirJsonFromCurrentState(const std::string &graphId) {
    auto &st = getState();
    if (!st.ctx)
        return {"", "run first"};
    if (!st.mainModule || !st.mainModule->loaded())
        return {"", "run first"};
    auto graph = st.ctx->rootGraph();
    if (!graph)
        return {"", "no graph"};
    return getGirJson(graph, graphId);
}

std::pair<std::string, std::string>
getGirJsonByPath(const std::string &path, const std::string &graphId) {
    if (!fs::exists(path))
        return {"", "file not found"};
    std::ifstream file(path);
    if (!file.is_open())
        return {"", "cannot open file"};
    CompilationState state = createCompilationStateForPath(path);
    try {
        state.parser->parse(file);
        state.mainModule->compile(CompileStage::Done);
        if (!state.mainModule->loaded())
            return {"", "compile failed"};
        auto graph = state.ctx->rootGraph();
        if (!graph)
            return {"", "no graph"};
        return getGirJson(graph, graphId);
    } catch (const std::exception &e) {
        return {"", std::string(e.what())};
    }
}

} // namespace

std::pair<std::string, std::string>
getGirJson(const std::string &path, const std::string &graphId) {
    if (!path.empty())
        return getGirJsonByPath(path, graphId);
    auto fromState = getGirJsonFromCurrentState(graphId);
    if (!fromState.second.empty() && fromState.second != "run first")
        return fromState;
    if (fromState.second == "run first" && getState().hasFile())
        return getGirJsonByPath(getState().targetFile, graphId);
    return fromState;
}

} // namespace debugger
