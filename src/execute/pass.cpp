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
 * Updated: Mar. 15, 2026
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

#include <format>

using namespace GIR;
using namespace camel::core::error;
using namespace camel::core::context;

graph_ptr_t NullGraphIRPass::apply(graph_ptr_t &graph, std::ostream &os) {
    // Do nothing
    return Graph::null();
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

// 嵌套初始化列表：def(factory) 叶子节点，def(factory, {...}) 带子域，scope({...}) 纯子域
struct PassDef {
    std::optional<PassFactory> value;
    std::vector<std::pair<std::string, PassDef>> children;
};

PassDef def(PassFactory f) { return PassDef{.value = std::move(f), .children = {}}; }

PassDef def(PassFactory f, std::initializer_list<std::pair<const char *, PassDef>> list) {
    PassDef r{.value = std::move(f), .children = {}};
    for (const auto &[k, v] : list)
        r.children.emplace_back(k, v);
    return r;
}

PassDef scope(std::initializer_list<std::pair<const char *, PassDef>> list) {
    PassDef r;
    for (const auto &[k, v] : list)
        r.children.emplace_back(k, v);
    return r;
}

void buildPassScope(PassScopePtr s, const PassDef &def) {
    for (const auto &[name, child] : def.children) {
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
                    {"inline", def(PASS(InlineRewritePass))},
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
    // 标准调度器
    {"std::default", "std::nodevm"},
    {"std::linear", "std::nodevm"},
    {"std::parallel", "std::taskflow"},

    // 常用vm缩写
    {"std::lnr", "std::fastvm"},
    {"std::prl", "std::taskflow"},
    {"std::fvm", "std::fastvm"},
    {"std::jit", "std::fastvm::jit"},
    {"std::nvm", "std::nodevm"},
    {"std::svm", "std::stackvm"},
    {"std::tf", "std::taskflow"},

    // 常用转译遍缩写
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
    // 1. 解析别名
    std::string resolved = name;
    auto aliasIt         = passAliases.find(name);
    if (aliasIt != passAliases.end()) {
        resolved = aliasIt->second;
    }

    // 2. 含 :: 的完整路径：按域分级查找
    if (resolved.find("::") != std::string::npos) {
        auto path = splitPath(resolved);
        if (!path.empty()) {
            auto factory = lookupInScope(passScope, path);
            if (factory)
                return factory;
        }
    } else {
        // 3. 无 ::：先查 std::name，再查全局 name
        auto stdPath = splitPath("std::" + resolved);
        auto factory = lookupInScope(passScope, stdPath);
        if (factory)
            return factory;
        auto globalPath = std::vector<std::string>{resolved};
        factory         = lookupInScope(passScope, globalPath);
        if (factory)
            return factory;
    }

    // 未找到，输出可用 pass 列表
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
    GIR::graph_ptr_t graph, const std::vector<std::string> &passes, const context_ptr_t &ctx,
    std::ostream &os) {
    for (const auto &p : passes) {
        if (graph == nullptr || graph == Graph::null()) {
            return {Graph::null(), PassApplyStatus::Consumed};
        }
        ASSERT(
            graph->frozen(),
            std::format("Graph {} is not finalized before pass execution.", graph->name()));

        auto factory = findPassFactory(p, os);
        if (factory) {
            EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit(p.c_str(), graph.get()); });
            auto pass = factory(ctx);
            graph     = pass->apply(graph, os);
            if (ctx->rtmDiags()->hasErrors()) {
                return {nullptr, PassApplyStatus::Failed};
            }
            if (graph == Graph::null())
                return {Graph::null(), PassApplyStatus::Consumed};
        } else {
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(p);
        }
    }

    EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit("GIR-Z", graph ? graph.get() : nullptr); });
    return {graph, PassApplyStatus::Transformed};
}

GIR::graph_ptr_t applyPasses(
    GIR::graph_ptr_t graph, const std::vector<std::string> &passes, const context_ptr_t &ctx,
    std::ostream &os) {
    return applyPassesDetailed(std::move(graph), passes, ctx, os).graph;
}
