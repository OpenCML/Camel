// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Own static scope artifacts in the core fact store and bind invocation inputs.

#include "tiles/internal/compiled_scope.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/facts.h"
#include "tiles/internal/capability.h"

namespace camel::tiles {
namespace rt = runtime;
namespace {
// This is an in-process CUDA-policy artifact, not a second semantic identity.
// Graph id/revision and the declaration epoch are supplied by FactStore.
// Changing the lowering policy must change this hash; device-specific machine
// compilation remains in the backend's context-local source cache.
constexpr uint64_t compiledScopeTarget = 0x74696c6573000004ULL;
rt::fact::KindId compiledScopeKind() {
    static const auto kind = rt::fact::registerKind<std::shared_ptr<const Program>>(
        {.key               = "tiles.compiled_scope",
         .owner             = "tiles",
         .cls               = rt::fact::FactClass::Target,
         .scope             = rt::fact::Scope::Graph,
         .readsDeclarations = true,
         .print             = [](const std::shared_ptr<const Program> &program) {
             return "compiled CUDA template: " + std::to_string(program->nodes.size()) + " values";
         }});
    return kind;
}
} // namespace
bool hasCompiledScope(Function *function, rt::fact::FactStore &facts) {
    return function && function->graph() &&
           facts.get<std::shared_ptr<const Program>>(
               compiledScopeKind(),
               rt::fact::Anchor{function->program(), function->graph()},
               compiledScopeTarget) != nullptr;
}
Program compileTypes(Function *function, rt::fact::FactStore &facts) {
    if (!function || !function->graph())
        throw std::runtime_error("expected a function graph");
    // A template cannot specialize on closure values: their owner and current
    // contents belong to the invocation, never to the graph's long-lived fact.
    if (function->tuple() && function->tuple()->size())
        throw UnsupportedComputation("type-only compilation requires a closure-free scope");
    const rt::fact::Anchor anchor{function->program(), function->graph()};
    if (auto cached = facts.get<std::shared_ptr<const Program>>(
            compiledScopeKind(),
            anchor,
            compiledScopeTarget))
        return **cached;
    auto program = compileRegionTemplate(function, facts);
    for (const auto &node : program.nodes)
        if (node.resident)
            throw std::runtime_error("compiled scope template retains a resident input");
    for (auto input : program.parameterInputs) {
        const auto &node = program.nodes.at(input);
        if (!node.data.empty() || node.scalarBits)
            throw std::runtime_error("compiled scope template retains invocation data");
    }
    facts.put(
        compiledScopeKind(),
        anchor,
        std::make_shared<const Program>(program),
        compiledScopeTarget);
    return program;
}
Program prepareProgram(
    Function *function, const std::vector<Argument> &arguments, rt::fact::FactStore &facts,
    bool staticRegion) {
    if (!function || !function->graph())
        throw std::runtime_error("expected a function graph");
    // The outlined call expresses static compilation eligibility. Cache presence
    // is not that identity: another reader may already have discarded a stale fact.
    const rt::fact::Anchor anchor{function->program(), function->graph()};
    if (function->tuple() && function->tuple()->size())
        return compile(function, arguments, facts);
    auto cached = facts.get<std::shared_ptr<const Program>>(
        compiledScopeKind(),
        anchor,
        compiledScopeTarget);
    if (!cached && !staticRegion)
        return compile(function, arguments, facts);
    Program program = cached ? **cached : compileTypes(function, facts);
    bindRegionArguments(program, arguments, facts);
    return program;
}
} // namespace camel::tiles
