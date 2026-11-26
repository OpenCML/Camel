// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "block_body.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/source_loader.h"
#include "camel/core/module/userdef.h"
#include "camel/execute/executor.h"
#include "camel/execute/pass/registration.h"
#include "camel/parse/parse.h"
#include "camel/runtime/block_programs.h"
#include "camel/runtime/graph.h"
#include "graph_caller.h"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using camel::core::error::Diagnostics;

/// A body: one ordinary parameter in, one result out. A block's body is written this way -- the
/// values it captures are the parameters its canonical form names.
constexpr const char *kBodyText = R"cml(// The body a block hands over.
export func body(x: int) => int {
    let stepped = x + 1
    return stepped
}
)cml";

/// A second body, so the reading can say what one kernel does with two blocks: the value differs
/// because the program differs, not because the code does.
constexpr const char *kScaledText = R"cml(// A second body, for the second block.
export func scaled(x: int) => int {
    let tripled = x * 3
    return tripled
}
)cml";

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

/// A call's slots, for a caller that holds them and no frame: the argument view a kernel reads.
class SlotsView final : public ArgsView {
  public:
    SlotsView(std::vector<slot_t> slots, std::vector<camel::core::type::Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    camel::core::type::TypeCode code(size_t index) const override { return types_[index]->code(); }
    camel::core::type::Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<camel::core::type::Type *> types_;
};

/// What a block's operator answers: the program of its body, run with the call's arguments.
///
/// One kernel for every block, because what differs between two blocks is the program and not the
/// code: the node's attributes name the program (`block.program`), the context's store answers it,
/// and the reference executor's caller runs it. The URI reached this kernel through the executor
/// manager, so a block's implementation is resolved per context and nothing about the body is in
/// the operator's record.
slot_t
runBlockOfItsProgram(ArgsView & /*with*/, ArgsView &norm, camel::core::context::Context &ctx) {
    const std::optional<uint64_t> ordinal = camel::runtime::blockProgramOf(norm.attrs());
    if (!ordinal) {
        return camel::core::rtdata::toSlot<int32_t>(-1);
    }
    const camel::runtime::BlockProgram *block = ctx.blockPrograms().byOrdinal(*ordinal);
    if (block == nullptr || block->program == nullptr || block->root == nullptr) {
        return camel::core::rtdata::toSlot<int32_t>(-2);
    }
    const camel::execute::detail::GraphCaller call = camel::execute::detail::graphCaller();
    if (call == nullptr) {
        return camel::core::rtdata::toSlot<int32_t>(-3);
    }
    // Argument 0 of the view is the result; the parameters start at 1.
    std::vector<slot_t> arguments;
    for (size_t index = 1; index < norm.size(); ++index) {
        arguments.push_back(norm.slot(index));
    }
    return call(ctx.handle(), block->program, block->root, arguments, {}, {});
}

/// The executor of the `block` protocol: the kernel under every URI the context's store knows.
/// `Executor::find` is not virtual, so the map is filled when the executor is made -- after the
/// blocks were compiled, before anything runs one.
class BlockExecutor final : public Executor {
  public:
    explicit BlockExecutor(const camel::core::context::context_ptr_t &ctx)
        : Executor(ctx, opsOf(*ctx)) {}

  private:
    static std::unordered_map<std::string, operator_t> opsOf(camel::core::context::Context &ctx) {
        std::unordered_map<std::string, operator_t> ops;
        for (const camel::runtime::BlockProgram &block : ctx.blockPrograms().entries()) {
            // The manager asks the executor for the URI without the protocol, which is the
            // spelling every module's map uses (`ExecutorManager::find`).
            ops.emplace(block.uri.substr(block.uri.find(':') + 1), &runBlockOfItsProgram);
        }
        return ops;
    }
};

} // namespace

/// Compiles `text` as a module named `name` and answers the program of its `function`, pinned to
/// the context; nullptr when it does not parse or does not compile.
camel::runtime::Program *BlockBodyTestPass::compileBody(
    const std::string &name, const char *text, const char *function, const std::string &what,
    std::ostream &os) {
    auto diagnostics =
        std::make_shared<Diagnostics>(name, std::string("<in-memory>"), context_->sourceContext());
    diagnostics->setConfig(context_->diagConfig());
    auto parser = std::make_shared<camel::parse::CamelParser>(diagnostics);
    parser->setSourceContext(context_->sourceContext());
    // Parsed before the module exists: a module whose reader already holds an AST needs no file,
    // which is what `readSource` keys on (a module with no path never opens one).
    std::istringstream source(text);
    if (!parser->parse(source)) {
        reportFailure("the " + what + " does not parse", *diagnostics, os);
        return nullptr;
    }

    auto module = std::make_shared<camel::core::module::UserDefinedModule>(
        name,
        std::string(),
        context_,
        parser);
    std::string problem;
    std::shared_ptr<void> root;
    const auto status = module->compileAsProgram(function, problem, root, nullptr);
    if (status != camel::core::module::UserDefinedModule::ProgramStatus::Built || !root) {
        os << std::format(
            "BLOCK_BODY failed to build the {}: status={} problem={}\n",
            what,
            static_cast<int>(status),
            problem);
        if (module->diagnostics()->hasErrors()) {
            reportFailure("the " + what + " does not compile", *module->diagnostics(), os);
        }
        return nullptr;
    }

    camel::core::module::SourceModuleLoader *loader = camel::core::module::sourceModuleLoader();
    if (loader == nullptr) {
        os << "BLOCK_BODY failed to encode the " << what << ": no source module loader\n";
        return nullptr;
    }
    // The body as a program of its own, encoded by the same compiler and pinned to the context:
    // the module that built it ends with this call, and the graphs would go with it.
    camel::runtime::Program *body = context_->reserveProgram();
    loader->encodeProgram(root, *module, *body, true);
    context_->pinRuntimeProgram(body);
    return body;
}

const camel::runtime::Program *
BlockBodyTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    // The entry program as this pass found it: the same numbers must hold when the bodies have been
    // compiled, because their graphs are theirs and not this program's.
    const std::vector<std::string> entryBefore = graphNames(*program);
    const uint64_t entryNodesBefore            = nodeCount(*program);

    struct Compiled {
        std::string what;
        std::string name;
        const char *function;
        const char *text;
        std::string uri;
        camel::runtime::Program *program = nullptr;
    };
    std::vector<Compiled> blocks{
        {.what     = "body",
         .name     = "block_body",
         .function = "body",
         .text     = kBodyText,
         .uri      = "block::body"},
        {.what     = "second",
         .name     = "block_scaled",
         .function = "scaled",
         .text     = kScaledText,
         .uri      = "block::scaled"},
    };
    for (Compiled &block : blocks) {
        block.program = compileBody(block.name, block.text, block.function, block.what, os);
        if (block.program == nullptr) {
            return program;
        }
        os << std::format(
            "BLOCK_BODY built {} graphs={} nodes={}\n",
            block.what,
            graphNames(*block.program).size(),
            nodeCount(*block.program));
        for (const camel::runtime::GCGraph *graph : block.program->graphs()) {
            os << std::format(
                "BLOCK_BODY graph {} {} nodes={}\n",
                block.what,
                graph->name(),
                graph->storedNodeCount());
        }
    }

    // The blocks in the context's store, where an operator's node reads them from, and the one
    // executor of their protocol: a URI reaches the kernel through the manager, the ordinal in the
    // node's attributes says which program.
    for (Compiled &block : blocks) {
        const uint64_t registered =
            context_->blockPrograms().add(block.uri, block.program, block.program->root());
        os << std::format(
            "BLOCK_BODY registered {} uri={} ordinal={}\n",
            block.what,
            block.uri,
            registered);
    }
    const camel::core::context::context_ptr_t handle = context_->handle();
    context_->registerExecutorFactory("block", [handle]() -> executor_ptr_t {
        return std::make_shared<BlockExecutor>(handle);
    });

    // One call per block, through the operator the manager finds, with the ordinal in the
    // attributes: the value is the body's, computed by the program the block names.
    const auto callBlock =
        [this, &os](const std::string &uri, int32_t argument) -> std::optional<int32_t> {
        const std::optional<operator_t> op = context_->execMgr().find(uri);
        if (!op) {
            os << std::format("BLOCK_CALL {} not found\n", uri);
            return std::nullopt;
        }
        const camel::runtime::BlockProgram *block = context_->blockPrograms().byUri(uri);
        if (block == nullptr) {
            os << std::format("BLOCK_CALL {} not in the store\n", uri);
            return std::nullopt;
        }
        std::vector<slot_t> slots{
            camel::core::rtdata::toSlot<int32_t>(0),
            camel::core::rtdata::toSlot<int32_t>(argument)};
        std::vector<camel::core::type::Type *> types{
            camel::core::type::Type::Int32(),
            camel::core::type::Type::Int32()};
        SlotsView norm(std::move(slots), std::move(types));
        norm.setAttrs(camel::runtime::blockProgramAttrs(block->ordinal));
        SlotsView with({}, {});
        const slot_t value     = (*op)(with, norm, *context_);
        const int32_t answered = camel::core::rtdata::fromSlot<int32_t>(value);
        os << std::format("BLOCK_CALL {} argument={} value={}\n", uri, argument, answered);
        return answered;
    };

    callBlock(blocks[0].uri, 41);
    callBlock(blocks[1].uri, 5);
    // The same block again, after its first run is over: the program the context pinned is still
    // the one its operator runs, on this context, with nothing of the module left.
    callBlock(blocks[0].uri, 1);

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
