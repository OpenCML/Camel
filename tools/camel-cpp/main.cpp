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
 * Created: Mar. 11, 2026
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/execute/pass/base.h"
#include "camel/init.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/cst_dumper.h"
#include "camel/parse/parse.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/memperf.h"
#include "camel/utils/windows_parser_guard.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/tns/topo_node_seq.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::error;
using namespace camel::parse;

namespace fs = std::filesystem;

static fs::path findProjectRoot() {
    for (auto p = fs::current_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        auto libPath = p / "build" / "Release" / "libcamel.lib";
        if (fs::exists(libPath))
            return p;
        libPath = p / "build" / "libcamel.lib";
        if (fs::exists(libPath))
            return p;
    }
    return fs::current_path();
}

static int runCompile(const fs::path &projRoot, const fs::path &cppPath, const std::string &stem) {
    auto libDir  = projRoot / "build" / "Release";
    auto libPath = libDir / "libcamel.lib";
    if (!fs::exists(libPath)) {
        libDir  = projRoot / "build";
        libPath = libDir / "libcamel.lib";
    }
    if (!fs::exists(libPath)) {
        std::cerr << "[camel-cpp] libcamel.lib not found. Run: cmake --build build --config Release"
                  << std::endl;
        return 1;
    }

    auto tmpDir  = projRoot / "tmp";
    auto outExe  = tmpDir / (stem + ".exe");
    auto include = projRoot / "include";
    auto src     = projRoot / "src";
    auto third   = projRoot / "third_party";
    auto antlr4  = third / "antlr4-runtime";

    std::ostringstream cmd;
#ifdef _WIN32
    cmd << "clang++ -std=c++23 -O2 -m64 "
        << "-DANTLR4CPP_STATIC -DPLATFORM_WINDOWS "
        << "-I \"" << include.string() << "\" "
        << "-I \"" << src.string() << "\" "
        << "-I \"" << third.string() << "\" "
        << "-I \"" << antlr4.string() << "\" "
        << "-I \"" << projRoot.string() << "\" "
        << "\"" << cppPath.string() << "\" "
        << "-o \"" << outExe.string() << "\" "
        << "\"" << libPath.string() << "\"";
#else
    cmd << "clang++ -std=c++23 -O2 "
        << "-DANTLR4CPP_STATIC "
        << "-I " << include.string() << " "
        << "-I " << src.string() << " "
        << "-I " << third.string() << " "
        << "-I " << antlr4.string() << " "
        << "-I " << projRoot.string() << " " << cppPath.string() << " "
        << "-o " << outExe.string() << " " << libPath.string();
#endif

    std::cerr << "[camel-cpp] Building: " << cppPath.filename().string() << " -> " << outExe
              << std::endl;
    int ret = std::system(cmd.str().c_str());
    if (ret != 0)
        return ret;

    std::cerr << "[camel-cpp] Done: " << outExe << std::endl;
    return 0;
}

int main(int argc, char *argv[]) {
    camel::ScopedRuntime camelRuntime;

    if (argc < 2) {
        std::cerr << "Usage: camel-cpp <file.cml>\n";
        return 1;
    }
    std::string targetFile = argv[1];

    std::unique_ptr<std::ifstream> input = std::make_unique<std::ifstream>(targetFile);
    if (!input->is_open()) {
        std::cerr << "Error: Cannot open file " << targetFile << std::endl;
        return 1;
    }

    auto diagnostics = std::make_shared<Diagnostics>("main", targetFile);
    diagnostics->setConfig(
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});

    fs::path entryPath(targetFile);
    std::string entryDir = fs::absolute(entryPath).parent_path().string();

    auto projRoot    = findProjectRoot();
    auto searchPaths = camel::utils::buildModuleSearchPaths(entryDir);
    searchPaths.push_back(fs::absolute(projRoot / "stdlib").string());

    auto ctx = Context::create(
        EntryConfig{
            .entryDir    = entryDir,
            .entryFile   = targetFile,
            .searchPaths = std::move(searchPaths),
        },
        DiagsConfig{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}});

    auto parser     = std::make_shared<CamelParser>(diagnostics);
    auto mainModule = std::make_shared<UserDefinedModule>("main", targetFile, ctx, parser);
    ctx->setMainModule(mainModule);

    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

    try {
        std::cerr << "[camel-cpp] Parsing..." << std::endl;
        parser->parse(*input);
        std::cerr << "[camel-cpp] Compiling..." << std::endl;
        mainModule->compile(CompileStage::Done);
        if (!mainModule->loaded()) {
            ctx->dumpAllModuleDiagnostics(std::cerr, false);
            return 1;
        }

        std::cerr << "[camel-cpp] Applying std::cpp..." << std::endl;
        std::stringstream cppOutput;
        auto graph = ctx->rootGraph();
        graph      = applyPasses(graph, {"std::cpp"}, ctx, cppOutput);
        if (ctx->rtmDiags()->hasErrors()) {
            ctx->runtimeDiagSink()->dump(std::cerr, false);
            return 1;
        }

        std::string out = cppOutput.str();
        if (out.find("#include") == std::string::npos) {
            std::cerr << "[camel-cpp] std::cpp produced invalid output" << std::endl;
            return 1;
        }

        auto tmpDir = projRoot / "tmp";
        fs::create_directories(tmpDir);

        std::string stem = entryPath.stem().string();
        auto cppPath     = tmpDir / (stem + ".cpp");
        std::cerr << "[camel-cpp] Writing " << cppPath << "..." << std::endl;
        std::ofstream ofs(cppPath);
        if (!ofs) {
            std::cerr << "[camel-cpp] Cannot write " << cppPath << std::endl;
            return 1;
        }
        ofs << out;
        ofs.close();

        return runCompile(projRoot, cppPath, stem);

    } catch (Diagnostic &d) {
        RangeConverter conv(parser->getTokens());
        d.fetchRange(conv);
        std::cerr << "Uncaught diagnostic: " << d.toText() << std::endl;
        return 1;
    } catch (std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error" << std::endl;
        return 1;
    }
}
