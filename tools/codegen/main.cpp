// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/utils/windows_parser_guard.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/init.h"
#include "camel/parse/parse.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/log.h"
#include "camel/utils/path.h"
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
    // `CAMEL_HOME` picks the installation whose standard library the run reads; a value that does
    // not name an installation is reported as that, before anything is loaded.
    if (const std::string problem = camel::utils::camelHomeProblem(); !problem.empty()) {
        std::cerr << "camel-codegen: " << problem << "\n";
        return 1;
    }
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
    // The entry module is named by its file (design principles 10.2), as in the CLI. The
    // diagnostics above keep `camel-codegen`: that name is the tool reporting, not the module.
    auto mainModule  = std::make_shared<UserDefinedModule>(
        camel::utils::moduleNameOfFile(inputPath), inputPath, ctx, parser);
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
    CAMEL_LOG_INFO_S(
        "codegen",
        "run | compile | module={} | user_modules={}",
        mainModule->name(),
        ctx->allUserModules().size());

    auto ast = parser->ast();
    if (!ast) {
        std::cerr << "camel-codegen: no AST\n";
        return 1;
    }
    ASTCodeGen::Generator generator;
    std::cout << generator.generate(ast);
    return 0;
}
