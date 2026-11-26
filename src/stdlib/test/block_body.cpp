// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "block_body.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/source_loader.h"
#include "camel/core/module/userdef.h"
#include "camel/execute/pass/registration.h"
#include "camel/parse/parse.h"
#include "camel/runtime/graph.h"

#include <algorithm>
#include <format>
#include <sstream>
#include <string>
#include <vector>

namespace {

using camel::core::error::Diagnostics;

/// The body: one ordinary parameter in, one result out. A block's body is written this way -- the
/// values it captures are the parameters its canonical form names.
constexpr const char *kBodyText = R"cml(// The body a block hands over.
export func body(x: int) => int {
    let stepped = x + 1
    return stepped
}
)cml";

constexpr const char *kBodyFunction = "body";

/// The graphs of a program, by name, so two builds can be compared line by line.
std::vector<std::string> graphNames(const camel::runtime::Program &program) {
    std::vector<std::string> names;
    for (const camel::runtime::GCGraph *graph : program.graphs()) {
        names.push_back(graph->name());
    }
    std::sort(names.begin(), names.end());
    return names;
}

uint64_t nodeCount(const camel::runtime::Program &program) {
    uint64_t count = 0;
    for (const camel::runtime::GCGraph *graph : program.graphs()) {
        count += graph->storedNodeCount();
    }
    return count;
}

void reportFailure(const std::string &what, const Diagnostics &diagnostics, std::ostream &os) {
    std::ostringstream text;
    diagnostics.dump(text);
    std::string first = text.str();
    if (const size_t newline = first.find('\n'); newline != std::string::npos) {
        first.erase(newline);
    }
    os << std::format("BLOCK_BODY failed {}: {}\n", what, first);
}

} // namespace

const camel::runtime::Program *
BlockBodyTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The entry program as this pass found it: the same numbers must hold when the body has been
    // compiled, because the body's graphs are that program's, not this one's.
    const std::vector<std::string> entryBefore = graphNames(*program);
    const uint64_t entryNodesBefore            = nodeCount(*program);

    auto diagnostics = std::make_shared<Diagnostics>(
        std::string("block_body"),
        std::string("<in-memory>"),
        context_->sourceContext());
    diagnostics->setConfig(context_->diagConfig());
    auto parser = std::make_shared<camel::parse::CamelParser>(diagnostics);
    parser->setSourceContext(context_->sourceContext());
    // Parsed before the module exists: a module whose reader already holds an AST needs no file,
    // which is what `readSource` keys on (a module with no path never opens one).
    std::istringstream text(kBodyText);
    if (!parser->parse(text)) {
        reportFailure("the body does not parse", *diagnostics, os);
        return program;
    }

    auto module = std::make_shared<camel::core::module::UserDefinedModule>(
        std::string("block_body"),
        std::string(),
        context_,
        parser);
    std::string problem;
    std::shared_ptr<void> root;
    const auto status = module->compileAsProgram(kBodyFunction, problem, root, nullptr);
    if (status != camel::core::module::UserDefinedModule::ProgramStatus::Built || !root) {
        os << std::format(
            "BLOCK_BODY failed to build the body: status={} problem={}\n",
            static_cast<int>(status),
            problem);
        if (module->diagnostics()->hasErrors()) {
            reportFailure("the body does not compile", *module->diagnostics(), os);
        }
        return program;
    }

    camel::core::module::SourceModuleLoader *loader = camel::core::module::sourceModuleLoader();
    if (loader == nullptr) {
        os << "BLOCK_BODY failed to encode the body: no source module loader\n";
        return program;
    }
    // The body as a program of its own, encoded by the same compiler and pinned to the context:
    // the module that built it ends with this call, and the graphs would go with it.
    camel::runtime::Program *body = context_->reserveProgram();
    loader->encodeProgram(root, *module, *body, true);
    context_->pinRuntimeProgram(body);

    os << std::format(
        "BLOCK_BODY built graphs={} nodes={}\n",
        graphNames(*body).size(),
        nodeCount(*body));
    for (const camel::runtime::GCGraph *graph : body->graphs()) {
        os << std::format(
            "BLOCK_BODY graph {} nodes={}\n",
            graph->name(),
            graph->storedNodeCount());
    }

    const std::vector<std::string> entryAfter = graphNames(*program);
    os << std::format(
        "BLOCK_BODY entry graphs {} -> {} nodes {} -> {}\n",
        entryBefore.size(),
        entryAfter.size(),
        entryNodesBefore,
        nodeCount(*program));
    if (entryBefore != entryAfter) {
        os << "BLOCK_BODY entry took a graph of the body\n";
    }
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

const PassKernel blockBodyKernel(kBlockBodyTestPass, makePass<BlockBodyTestPass>());

} // namespace