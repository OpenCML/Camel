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
 * Created: Oct. 21, 2024
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/execute/pass.h"
#include "camel/common/scope.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/diagnostics.h"
#include "macro/macro.h"
#include "passes/opt/inline/inline.h"
#include "passes/sched/fastvm/bcdump.h"
#include "passes/sched/fastvm/fastvm.h"
#include "passes/sched/fastvm/jit/dump/asmdump.h"
#include "passes/sched/fastvm/jit/dump/bindump.h"
#include "passes/sched/fastvm/jit/dump/mirdump.h"
#include "passes/sched/nodevm/nodevm.h"
#include "passes/sched/taskflow/taskflow.h"
#include "passes/trans/cpp/cpp_export.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/tns/topo_node_seq.h"

#include "camel/utils/log.h"

#include <format>
#include <memory>
#include <sstream>

using namespace camel::core::error;
using namespace camel::core::context;
using camel::runtime::GCGraph;

GCGraph *NullGraphIRPass::apply(GCGraph *graph, std::ostream &os) {
    (void)graph;
    (void)os;
    return nullptr;
}

using PassFactory  = std::function<std::unique_ptr<GraphIRPass>(const context_ptr_t &ctx)>;
using PassScope    = Scope<std::string, PassFactory, std::string>;
using PassScopePtr = scope_ptr_t<std::string, PassFactory, std::string>;

namespace {

FastVMConfig makeDefaultFastVmJitConfig() {
    FastVMConfig config{};
    config.jitMode           = FastVMConfig::JitMode::OnDemand;
    config.jitHotThreshold   = 1;
    config.enableJitTraceMir = false;
    return config;
}

std::vector<std::string> splitPath(const std::string &path) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start < path.size()) {
        size_t pos = path.find("::", start);
        if (pos == std::string::npos) {
            result.push_back(path.substr(start));
            break;
        }
        result.push_back(path.substr(start, pos - start));
        start = pos + 2;
    }
    return result;
}

PassFactory lookupInScope(PassScopePtr scope, const std::vector<std::string> &path) {
    if (path.empty())
        return nullptr;
    for (size_t i = 0; i < path.size() - 1; ++i) {
        scope = scope->enter(path[i]);
        if (!scope)
            return nullptr;
    }
    auto opt = scope->get(path.back(), false);
    return opt ? *opt : nullptr;
}

void collectPassPaths(
    PassScopePtr scope, const std::string &prefix, std::vector<std::string> &out) {
    for (const auto &[key, _] : scope->map()) {
        out.push_back(prefix.empty() ? key : prefix + "::" + key);
    }
    scope->forEachNamedInner([&](const std::string &name, const PassScopePtr &child) {
        collectPassPaths(child, prefix.empty() ? name : prefix + "::" + name, out);
    });
}

// Nested pass registry initializer helpers: `def(factory)` creates a leaf,
// `def(factory, {...})` creates a node with both a factory and children, and
// `scope({...})` creates a pure namespace node.
struct PassDef {
    std::optional<PassFactory> value;
    // shared_ptr: vector<pair<..., PassDef>> would instantiate vector while PassDef is still
    // incomplete; Clang + libstdc++ reject that ([vector] requires a complete element type).
    std::vector<std::pair<std::string, std::shared_ptr<PassDef>>> children;
};

PassDef def(PassFactory f) { return PassDef{.value = std::move(f), .children = {}}; }

PassDef def(PassFactory f, std::initializer_list<std::pair<const char *, PassDef>> list) {
    PassDef r{.value = std::move(f), .children = {}};
    for (const auto &[k, v] : list)
        r.children.emplace_back(k, std::make_shared<PassDef>(v));
    return r;
}

PassDef scope(std::initializer_list<std::pair<const char *, PassDef>> list) {
    PassDef r;
    for (const auto &[k, v] : list)
        r.children.emplace_back(k, std::make_shared<PassDef>(v));
    return r;
}

void buildPassScope(PassScopePtr s, const PassDef &def) {
    for (const auto &[name, childPtr] : def.children) {
        const PassDef &child = *childPtr;
        if (child.value)
            s->insert(name, *child.value);
        if (!child.children.empty()) {
            auto sub = s->enter(name);
            buildPassScope(sub, child);
        }
    }
}

#define PASS(T) [](const context_ptr_t &ctx) { return std::make_unique<T>(ctx); }
#define PASS1(T, A) [](const context_ptr_t &ctx) { return std::make_unique<T>(ctx, A); }

PassScopePtr initPassScope() {
    auto root = PassScope::create();
    buildPassScope(
        root,
        scope({
            {
                "std",
                scope({
                    {"null", def(PASS(NullGraphIRPass))},
                    {"macro", def(PASS(MacroRewritePass))},
                    {"graphviz", def(PASS(GraphVizDumpPass))},
                    {"cpp",
                     def(PASS(CppDumpPass),
                         {
                             {"module", def(PASS(CppModuleDumpPass))},
                             {"inspect", def(PASS(CppInspectDumpPass))},
                             {"bench", def(PASS(CppBenchDumpPass))},
                         })},
                    {"topo_node_seq", def(PASS(TopoNodeSeqDumpPass))},
                    {"nodevm", def(PASS(NodeVMSchedPass))},
                    {"fastvm",
                     def(PASS(FastVMSchedPass),
                         {
                             {"bytecode", def(PASS(BytecodeDumpPass))},
                             {"linked_bytecode", def(PASS(LinkedBytecodeDumpPass))},
                             {"jit",
                              def(PASS1(FastVMSchedPass, makeDefaultFastVmJitConfig()),
                                  {
                                      {"dump",
                                       scope({
                                           {"asm", def(PASS(JitAsmDumpPass))},
                                           {"bin", def(PASS(JitBinaryDumpPass))},
                                           {"rmir", def(PASS(JitRmirDumpPass))},
                                           {"mir", def(PASS(JitMirDumpPass))},
                                       })},
                                  })},
                         })},
                    {"inline",
                     def(PASS1(InlineRewritePass, InlineRewriteConfig{}),
                         {
                             {"small", def([](const context_ptr_t &ctx) {
                                  return std::make_unique<InlineRewritePass>(
                                      ctx,
                                      InlineRewriteConfig{
                                          .inlineStrategy = InlineTargetStrategy::Small,
                                      });
                              })},
                             {"arm", def([](const context_ptr_t &ctx) {
                                  return std::make_unique<InlineRewritePass>(
                                      ctx,
                                      InlineRewriteConfig{
                                          .inlineStrategy = InlineTargetStrategy::Arm,
                                      });
                              })},
                             {"hybrid", def([](const context_ptr_t &ctx) {
                                  return std::make_unique<InlineRewritePass>(
                                      ctx,
                                      InlineRewriteConfig{
                                          .inlineStrategy = InlineTargetStrategy::Hybrid,
                                      });
                              })},
                         })},
                    {"taskflow", def(PASS(TaskflowExecSchedPass))},
                    {"tfdump", def(PASS(TfDumpPass))},
                }),
            },
        }));
    return root;
}

#undef PASS
#undef PASS1

const PassScopePtr passScope = initPassScope();

std::unordered_map<std::string, std::string> passAliases = {
    // Standard scheduler aliases
    {"std::default", "std::nodevm"},
    {"std::linear", "std::nodevm"},
    {"std::parallel", "std::taskflow"},

    // Common VM aliases
    {"std::lnr", "std::fastvm"},
    {"std::prl", "std::taskflow"},
    {"std::fvm", "std::fastvm"},
    {"std::jit", "std::fastvm::jit"},
    {"std::nvm", "std::nodevm"},
    {"std::svm", "std::stackvm"},
    {"std::tf", "std::taskflow"},
    // Common translation and dump aliases
    // Common translation and dump aliases
    {"std::dot", "std::graphviz"},
    {"std::gir", "std::graphviz"},
    {"std::cxx", "std::cpp"},
    {"std::cppmod", "std::cpp::module"},
    {"std::cppinspect", "std::cpp::inspect"},
    {"std::cppbench", "std::cpp::bench"},
    {"std::tns", "std::topo_node_seq"},
    {"std::bc", "std::fastvm::bytecode"},
    {"std::lbc", "std::fastvm::linked_bytecode"},
    {"std::bin", "std::fastvm::jit::dump::bin"},
    {"std::asm", "std::fastvm::jit::dump::asm"},
    {"std::rmir", "std::fastvm::jit::dump::rmir"},
    {"std::mir", "std::fastvm::jit::dump::mir"},
    {"std::tfg", "std::tfdump"},
};

} // namespace

PassFactory findPassFactory(const std::string &name, std::ostream &os) {
    // 1. Resolve aliases
    std::string resolved = name;
    auto aliasIt         = passAliases.find(name);
    if (aliasIt != passAliases.end()) {
        resolved = aliasIt->second;
    }

    // 2. Full paths with :: search by scope hierarchy
    if (resolved.find("::") != std::string::npos) {
        auto path = splitPath(resolved);
        if (!path.empty()) {
            auto factory = lookupInScope(passScope, path);
            if (factory)
                return factory;
        }
    } else {
        // 3. No :: try std::name first, then the global name
        auto stdPath = splitPath("std::" + resolved);
        auto factory = lookupInScope(passScope, stdPath);
        if (factory)
            return factory;
        auto globalPath = std::vector<std::string>{resolved};
        factory         = lookupInScope(passScope, globalPath);
        if (factory)
            return factory;
    }

    // Not found; print the list of available passes
    os << std::format("Pass <{}> not found, available passes are:\n", name);
    std::vector<std::string> allPaths;
    collectPassPaths(passScope, "", allPaths);
    for (const auto &p : allPaths) {
        os << std::format("  {}\n", p);
    }
    os << std::format("Available aliases are:\n");
    for (const auto &[alias, target] : passAliases) {
        os << std::format("  {} -> {}\n", alias, target);
    }
    os << std::endl;

    return nullptr;
}

PassApplyResult applyPassesDetailed(
    GCGraph *graph, const std::vector<std::string> &passes, const context_ptr_t &ctx,
    std::ostream &os) {
    if (!passes.empty() && Logger::ShouldEmit(LogLevel::Info, "Pass")) {
        std::ostringstream seq;
        for (size_t i = 0; i < passes.size(); ++i) {
            if (i > 0)
                seq << " -> ";
            seq << passes[i];
        }
        Logger::EmitLine(
            LogLevel::Info,
            "Pass",
            std::format("run | passes | plan ({}): {}", passes.size(), seq.str()));
    }
    for (const auto &p : passes) {
        if (graph == nullptr) {
            return {nullptr, PassApplyStatus::Consumed};
        }

        auto factory = findPassFactory(p, os);
        if (factory) {
            EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit(p.c_str(), graph); });
            auto pass = factory(ctx);
            graph     = pass->apply(graph, os);
            if (ctx->rtmDiags()->hasErrors()) {
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (see diagnostics)", p);
                return {nullptr, PassApplyStatus::Failed};
            }
            if (!graph) {
                CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> consumed", p);
                return {nullptr, PassApplyStatus::Consumed};
            }
            CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> next graph '{}'", p, graph->name());
        } else {
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(p);
        }
    }

    EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit("GIR-Z", graph); });
    return {graph, PassApplyStatus::Transformed};
}

GCGraph *applyPasses(
    GCGraph *graph, const std::vector<std::string> &passes, const context_ptr_t &ctx,
    std::ostream &os) {
    return applyPassesDetailed(graph, passes, ctx, os).graph;
}
