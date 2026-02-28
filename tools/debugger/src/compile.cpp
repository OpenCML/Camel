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
 * Updated: Feb. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "windows_parser_guard.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/userdef.h"
#include "camel/parse/parse.h"
#include "camel/utils/env.h"
#include "compile.h"
#include "passes/trans/dot/graphviz.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace debugger {

CompilationState createCompilationStateForPath(const std::string &path) {
    auto diagnostics = std::make_shared<Diagnostics>("main", path);
    diagnostics->setConfig(
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});
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
    auto parser     = std::make_shared<CamelParser>(diagnostics);
    auto mainModule = std::make_shared<UserDefinedModule>("main", path, ctx, parser);
    ctx->setMainModule(mainModule);
    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();
    return {ctx, parser, mainModule};
}

namespace {

std::pair<std::string, std::string> getGirDotFromCurrentState() {
    auto &st = getState();
    if (!st.ctx)
        return {"", "run first"};
    if (!st.mainModule || !st.mainModule->loaded())
        return {"", "run first"};
    auto graph = st.ctx->rootGraph();
    if (!graph)
        return {"", "no graph"};
    GraphVizDumpPass pass(st.ctx);
    std::ostringstream os;
    pass.apply(graph, os);
    return {os.str(), ""};
}

std::pair<std::string, std::string> getGirDotByPath(const std::string &path) {
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
        GraphVizDumpPass pass(state.ctx);
        std::ostringstream os;
        pass.apply(graph, os);
        return {os.str(), ""};
    } catch (const std::exception &e) {
        return {"", std::string(e.what())};
    }
}

} // namespace

std::pair<std::string, std::string> getGirDot(const std::string &path) {
    if (!path.empty())
        return getGirDotByPath(path);
    auto fromState = getGirDotFromCurrentState();
    if (!fromState.second.empty() && fromState.second != "run first")
        return fromState;
    if (fromState.second == "run first" && getState().hasFile())
        return getGirDotByPath(getState().targetFile);
    return fromState;
}

} // namespace debugger
