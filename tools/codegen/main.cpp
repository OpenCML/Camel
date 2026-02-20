/**
 * camel-codegen: 从源文件生成代码（AST -> 源码），链接 libcamel。
 */
#include "service/codegen/source/generator.h"
#include "camel/parse/parse.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/userdef.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/utils/env.h"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char *argv[]) {
    std::string inputPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: camel-codegen [options] <file>\n";
            return 0;
        }
        if (arg[0] != '-') { inputPath = arg; break; }
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
    auto ctx = Context::create(
        EntryConfig{
            .entryDir = entryDir,
            .entryFile = inputPath,
            .searchPaths = {
                entryDir,
                fs::absolute(fs::path(getEnv("CAMEL_STD_LIB", "./stdlib"))).string(),
                getEnv("CAMEL_PACKAGES"),
                getEnv("CAMEL_HOME", fs::current_path().string()),
            },
        },
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});

    auto diagnostics = std::make_shared<Diagnostics>("camel-codegen", inputPath);
    auto parser = std::make_shared<CamelParser>(diagnostics);
    auto mainModule = std::make_shared<UserDefinedModule>("main", inputPath, ctx, parser);
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

    mainModule->compile(CompileStage::Done);
    if (!mainModule->loaded()) {
        mainModule->diagnostics()->dump(std::cerr, false);
        return 1;
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
