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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/init.h"
#include "camel/parse/parse.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/log.h"
#include "service/codegen/source/generator.h"
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::parse;
using namespace camel::core::context;
using namespace camel::core::module;

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    camel::ScopedRuntime camelRuntime;
    std::string inputPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: camel-codegen [options] <file>\n";
            return 0;
        }
        if (arg[0] != '-') {
            inputPath = arg;
            break;
        }
    }
    if (inputPath.empty()) {
        std::cerr << "camel-codegen: missing input file\n";
        return 1;
    }
    if (!fs::exists(inputPath)) {
        std::cerr << "camel-codegen: file not found: " << inputPath << "\n";
        return 1;
    }

    std::string entryDir = fs::absolute(fs::path(inputPath)).parent_path().string();
    auto searchPaths     = camel::utils::buildModuleSearchPaths(entryDir);
    {
        auto installRoot = camel::utils::resolveInstallRoot();
        std::size_t n    = 0;
        for (const auto &p : searchPaths) {
            if (!p.empty())
                ++n;
        }
        LogInfoPathList(
            "codegen",
            [&] {
                return std::format(
                    "run | module paths | {} entries | CAMEL_HOME={} | entry_dir={}",
                    n,
                    installRoot.string(),
                    entryDir);
            },
            searchPaths);
    }
    auto ctx = Context::create(
        EntryConfig{
            .entryDir    = entryDir,
            .entryFile   = inputPath,
            .searchPaths = std::move(searchPaths),
        },
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});

    auto diagnostics = std::make_shared<Diagnostics>("camel-codegen", inputPath);
    auto parser      = std::make_shared<CamelParser>(diagnostics);
    auto mainModule  = std::make_shared<UserDefinedModule>("main", inputPath, ctx, parser);
    ctx->setMainModule(mainModule);

    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

    std::ifstream file(inputPath);
    if (!file) {
        std::cerr << "camel-codegen: cannot open: " << inputPath << "\n";
        return 1;
    }
    parser->parse(file);
    file.close();
    CAMEL_LOG_INFO_S("codegen", "run | parse | done | {}", inputPath);

    mainModule->compile(CompileStage::Done);
    if (!mainModule->loaded()) {
        mainModule->diagnostics()->dump(std::cerr, false);
        return 1;
    }
    {
        auto rg = ctx->rootGraph();
        CAMEL_LOG_INFO_S(
            "codegen",
            "run | compile | graph={} | user_modules={}",
            rg ? rg->name() : std::string{"<none>"},
            ctx->allUserModules().size());
    }

    auto ast = parser->ast();
    if (!ast) {
        std::cerr << "camel-codegen: no AST\n";
        return 1;
    }
    ASTCodeGen::Generator generator;
    std::cout << generator.generate(ast);
    return 0;
}
