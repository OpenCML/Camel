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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/execute/pass.h"
#include "camel/common/scope.h"
#include "camel/core/array/block_selftest.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/mm.h"
#include "camel/core/module/module.h"
#include "camel/core/node_attrs.h"
#include "camel/core/tags.h"
#include "camel/core/mm/block.h"
#include "camel/core/mm/foreign_handle.h"
#include "camel/core/mm/profiler.h"
#include "camel/core/process_exit.h"
#include "camel/core/rtdata/view_selftest.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"
#include "camel_pass.h"
#include "camel/passes/test/chunks.h"
#include "camel/passes/test/edges.h"
#include "camel/passes/test/graph_release.h"
#include "camel/passes/test/keepfn.h"
#include "camel/passes/test/rules.h"
#include "camel/passes/test/saturate.h"
#include "camel/passes/test/writings.h"
#include "camel/runtime/writings.h"
#include "camel/passes/test/control.h"
#include "camel/passes/test/loop_collect.h"
#include "camel/passes/test/exhaust_perm_space.h"
#include "camel/passes/test/exec_plan.h"
#include "camel/passes/test/regions.h"
#include "camel/passes/test/arm_loop_regions.h"
#include "camel/passes/trans/json/gir.h"
#include "macro/macro.h"
#include "pass_spec.h"
#include "passes/opt/devirtualize/devirtualize.h"
#include "passes/opt/generic/generic.h"
#include "passes/opt/inline/inline.h"
#include "passes/opt/opt/opt.h"
#include "passes/opt/specialize/specialize.h"
#include "passes/sched/fastvm/bcdump.h"
#include "passes/sched/fastvm/fastvm.h"
#include "passes/sched/fastvm/jit/dump/asmdump.h"
#include "passes/sched/fastvm/jit/dump/bindump.h"
#include "passes/sched/fastvm/jit/dump/mirdump.h"
#include "passes/sched/nodevm/nodevm.h"
#include "passes/sched/taskflow/taskflow.h"
#include "passes/test/definitions.h"
#include "passes/test/facts.h"
#include "passes/test/fault.h"
#include "passes/test/index_semantics.h"
#include "passes/test/native_call.h"
#include "passes/test/resources.h"
#include "passes/test/semantics.h"
#include "passes/trans/cpp/cpp_export.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/profile/profile.h"
#include "passes/trans/stats/stats.h"
#include "passes/trans/tns/topo_node_seq.h"

#include "camel/utils/log.h"
#include "camel/utils/phase_timer.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace camel::core::error;
using namespace camel::core::context;
using camel::runtime::GCGraph;

const camel::runtime::Program *
NullGraphIRPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    (void)os;
    return nullptr;
}

using PassScope    = Scope<std::string, PassDescriptor, std::string>;
using PassScopePtr = scope_ptr_t<std::string, PassDescriptor, std::string>;

namespace {

std::string trimText(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

/// The name of a specification, parameters removed.
std::string specName(const std::string &spec) {
    const auto open = spec.find('(');
    return trimText(open == std::string::npos ? spec : spec.substr(0, open));
}

FastVMConfig makeDefaultFastVmJitConfig() {
#if !ENABLE_FASTVM_JIT
    // The JIT has a code generator for x86-64 only; in a build without it std::jit is the
    // bytecode interpreter of std::fvm, with the same results. Said once per process, in the log
    // (-v) and not as a warning: the same command line must run on every platform, also under
    // -Werror.
    static std::once_flag said;
    std::call_once(said, [] {
        CAMEL_LOG_WARN_S(
            "JIT",
            "std::jit: this build has no native code generator (x86-64 only); the program runs "
            "on the bytecode interpreter, as under std::fvm.");
    });
#endif
    FastVMConfig config{};
    config.jitMode           = FastVMConfig::JitMode::OnDemand;
    config.jitHotThreshold   = 1;
    config.enableJitTraceMir = false;
    return config;
}

std::string
formatGcIssue(const camel::core::mm::GenerationalAllocatorWithGC::HeapVerificationIssue &issue) {
    std::ostringstream os;
    os << issue.message;
    if (!issue.path.empty())
        os << " | path=" << issue.path;
    if (!issue.owner.empty())
        os << " | owner=" << issue.owner;
    if (!issue.slotType.empty())
        os << " | slotType=" << issue.slotType;
    if (!issue.region.empty())
        os << " | region=" << issue.region;
    if (issue.object != 0)
        os << " | object=0x" << std::hex << issue.object << std::dec;
    if (issue.target != 0)
        os << " | target=0x" << std::hex << issue.target << std::dec;
    return os.str();
}

class GcSnapshotPass final : public GraphIRPass {
  public:
    explicit GcSnapshotPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::mm::profiler::snapshotToJson() << '\n';
        return nullptr;
    }
};

class GcVerifyPass final : public GraphIRPass {
  public:
    explicit GcVerifyPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        auto issues = camel::core::mm::autoSpace().verifyHeap();
        if (!issues.empty()) {
            constexpr size_t maxIssues = 10;
            std::ostringstream details;
            const size_t shown = std::min(maxIssues, issues.size());
            for (size_t i = 0; i < shown; ++i) {
                details << "\n  [" << i << "] " << formatGcIssue(issues[i]);
            }
            if (issues.size() > shown) {
                details << "\n  ... " << (issues.size() - shown) << " more issue(s)";
            }
            throw std::runtime_error(
                std::format(
                    "GC heap verification failed with {} issue(s):{}",
                    issues.size(),
                    details.str()));
        }
        os << "{\"ok\":true,\"kind\":\"gc.verify\"}\n";
        return program;
    }
};

class GcMinorPass final : public GraphIRPass {
  public:
    explicit GcMinorPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().minorGC();
        os << "{\"ok\":true,\"kind\":\"gc.minor\"}\n";
        return program;
    }
};

class GcMajorPass final : public GraphIRPass {
  public:
    explicit GcMajorPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().majorGC();
        os << "{\"ok\":true,\"kind\":\"gc.major\"}\n";
        return program;
    }
};

class GcConfigPass final : public GraphIRPass {
  public:
    explicit GcConfigPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::mm::profiler::configToJson() << '\n';
        return program;
    }
};

class GcSummaryPass final : public GraphIRPass {
  public:
    explicit GcSummaryPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::mm::profiler::summaryToText();
        return program;
    }
};

class GcBlocksPass final : public GraphIRPass {
  public:
    explicit GcBlocksPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::array::debugRunBlockSelfTest();
        os << std::format(
            "BLOCK_SELFTEST ok live={} peak={} count={} creations={}\n",
            camel::core::mm::blockBytesLive(),
            camel::core::mm::blockBytesPeak(),
            camel::core::mm::blockLiveCount(),
            camel::core::mm::blockCreations());
        return program;
    }
};

class GcRememberedSetPass final : public GraphIRPass {
  public:
    explicit GcRememberedSetPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().debugRunRememberedSetSelfTest();
        os << "{\"ok\":true,\"kind\":\"gc.remembered_set\"}\n";
        return program;
    }
};

class GcPoisonPass final : public GraphIRPass {
  public:
    explicit GcPoisonPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::autoSpace().debugRunPoisonSelfTest();
        os << "{\"ok\":true,\"kind\":\"gc.poison\"}\n";
        return program;
    }
};

class GcForeignResourcePass final : public GraphIRPass {
  public:
    explicit GcForeignResourcePass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        camel::core::mm::debugRunForeignResourceSelfTest(camel::core::mm::autoSpace());
        os << "{\"ok\":true,\"kind\":\"gc.foreign_resource\"}\n";
        return program;
    }
};

class GcViewsPass final : public GraphIRPass {
  public:
    explicit GcViewsPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << camel::core::rtdata::debugRunViewSelfTest() << '\n';
        return program;
    }
};

class GcViewsEscapePass final : public GraphIRPass {
  public:
    explicit GcViewsEscapePass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        // A debug build aborts inside the store; a release build prints why it did not.
        os << camel::core::rtdata::debugRunViewEscapeSelfTest() << '\n';
        return program;
    }
};

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

std::optional<PassDescriptor>
lookupInScope(PassScopePtr scope, const std::vector<std::string> &path) {
    if (path.empty())
        return std::nullopt;
    for (size_t i = 0; i < path.size() - 1; ++i) {
        scope = scope->enter(path[i]);
        if (!scope)
            return std::nullopt;
    }
    auto opt = scope->get(path.back(), false);
    if (!opt)
        return std::nullopt;
    return *opt;
}

void collectPassPaths(
    PassScopePtr scope, const std::string &prefix,
    std::vector<std::pair<std::string, PassDescriptor>> &out) {
    for (const auto &[key, descriptor] : scope->map()) {
        out.emplace_back(prefix.empty() ? key : prefix + "::" + key, descriptor);
    }
    scope->forEachNamedInner([&](const std::string &name, const PassScopePtr &child) {
        collectPassPaths(child, prefix.empty() ? name : prefix + "::" + name, out);
    });
}

// Nested pass registry initializer helpers: `def(factory)` creates a leaf,
// `def(factory, {...})` creates a node with both a factory and children, and
// `scope({...})` creates a pure namespace node.
struct PassDef {
    std::optional<PassDescriptor> value;
    // shared_ptr: vector<pair<..., PassDef>> would instantiate vector while PassDef is still
    // incomplete; Clang + libstdc++ reject that ([vector] requires a complete element type).
    std::vector<std::pair<std::string, std::shared_ptr<PassDef>>> children;
};

PassDef def(PassDescriptor f) { return PassDef{.value = std::move(f), .children = {}}; }

PassDef def(PassFactory f) { return def(passWithoutParameters(std::move(f))); }

PassDef def(PassDescriptor f, std::initializer_list<std::pair<const char *, PassDef>> list) {
    PassDef r{.value = std::move(f), .children = {}};
    for (const auto &[k, v] : list)
        r.children.emplace_back(k, std::make_shared<PassDef>(v));
    return r;
}

PassDef def(PassFactory f, std::initializer_list<std::pair<const char *, PassDef>> list) {
    return def(passWithoutParameters(std::move(f)), list);
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

PassParam safeFpParameter() {
    return PassParam{
        "safe_fp",
        PassParamType::Bool,
        false,
        "keep the algebraic stage to strictly equivalent floating-point transformations "
        "(design principles 7.4)"};
}

/// The `growth_budget` parameter of every pass built on the rewrite engine (design principles
/// 9.3): the nodes specialization and inlining may copy. A pipeline no longer sets it through an
/// environment variable.
PassParam growthBudgetParameter() {
    return PassParam{
        "growth_budget",
        PassParamType::Int,
        static_cast<int64_t>(kDefaultGrowthBudgetNodes),
        "nodes specialization and inlining may copy before the remaining calls are left as "
        "calls; 0 stops them (design principles 9.6)"};
}

GrowthBudget growthBudgetOf(const PassArgs &args, std::string passName) {
    GrowthBudget budget;
    const int64_t nodes = args.getInt("growth_budget");
    budget.nodes    = nodes < 0 ? 0 : static_cast<size_t>(nodes);
    budget.written  = args.given("growth_budget");
    budget.passName = std::move(passName);
    return budget;
}

PassDescriptor optimizeDescriptor() {
    PassDescriptor d;
    d.doc    = "the standard optimization pipeline";
    d.params = {
        safeFpParameter(),
        growthBudgetParameter(),
        PassParam{
            "check_screens",
            PassParamType::Bool,
            false,
            "test of the generic passes' screens: give the graphs they skip to the pass anyway "
            "and fail if it rewrites one"}};
    d.make = [](const context_ptr_t &ctx, const PassArgs &args) {
        OptimizeRewriteConfig config;
        config.safeFloatingPoint = args.getBool("safe_fp");
        config.checkScreens      = args.getBool("check_screens");
        config.growthBudget      = growthBudgetOf(args, "std::opt");
        return std::make_unique<OptimizeRewritePass>(ctx, config);
    };
    return d;
}

PassDescriptor inlineDescriptor(
    std::optional<InlineTargetStrategy> strategy, std::string passName) {
    PassDescriptor d;
    if (strategy == InlineTargetStrategy::Small) {
        d.doc = "inline-only rewrite pass, small targets";
    } else if (strategy == InlineTargetStrategy::Arm) {
        d.doc = "inline-only rewrite pass, branch-arm targets";
    } else {
        d.doc = "inline-only rewrite pass (hybrid target strategy)";
    }
    d.params = {growthBudgetParameter()};
    d.make   = [strategy, passName = std::move(passName)](
                   const context_ptr_t &ctx, const PassArgs &args) {
        InlineRewriteConfig config{};
        if (strategy) {
            config.inlineStrategy = *strategy;
        }
        return std::make_unique<InlineRewritePass>(
            ctx, config, growthBudgetOf(args, passName));
    };
    return d;
}

PassDescriptor specializeDescriptor() {
    PassDescriptor d;
    d.doc    = "specialization only";
    d.params = {growthBudgetParameter()};
    d.make   = [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<SpecializeRewritePass>(
            ctx, growthBudgetOf(args, "std::opt::specialize"));
    };
    return d;
}

PassDescriptor devirtualizeDescriptor() {
    PassDescriptor d;
    d.doc    = "devirtualization only";
    d.params = {growthBudgetParameter()};
    d.make   = [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<DevirtualizeRewritePass>(
            ctx, growthBudgetOf(args, "std::opt::devirtualize"));
    };
    return d;
}

PassDescriptor extractDescriptor() {
    PassDescriptor d;
    d.doc    = "one writing for every value that has several, chosen by a cost model";
    d.params = {PassParam{
        "cost",
        PassParamType::String,
        std::string("weighted"),
        "the cost model: 'nodes' (every node costs one), 'weighted' (a division costs more than a "
        "product, a product more than a sum), or one a module registered"}};
    d.make   = [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<ExtractPass>(ctx, args.getString("cost"));
    };
    return d;
}

PassDescriptor dceDescriptor() {
    PassDescriptor d;
    d.doc    = "dead code elimination";
    d.params = {PassParam{
        "values_only",
        PassParamType::Bool,
        false,
        "remove only the constants and pure operations that no node reads, not gates and syncs "
        "(the reference for passes that do the same, such as one written in Camel)"}};
    d.make = [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<DeadCodePass>(ctx, args.getBool("values_only"));
    };
    return d;
}

PassDescriptor algebraDescriptor() {
    PassDescriptor d;
    d.doc    = "algebraic simplification";
    d.params = {
        safeFpParameter(),
        PassParam{
            "neutral_only",
            PassParamType::Bool,
            false,
            "apply only the integer neutral elements (x + 0, 0 + x, x - 0, x * 1, 1 * x), not the "
            "merging of like terms and constant factors (the reference for passes that do the "
            "same)"},
    };
    d.make = [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<AlgebraPass>(
            ctx,
            args.getBool("safe_fp"),
            args.getBool("neutral_only"));
    };
    return d;
}

PassParam statsParameter() {
    return PassParam{
        "stats",
        PassParamType::String,
        std::string(),
        "append per-graph bytecode and execution counts of the run to this file"};
}

PassDescriptor profileDescriptor() {
    PassDescriptor d;
    d.doc =
        "profiles the run (design docs/design/gpu/README.cn.md 5.6): the device and the time of "
        "every operator, the operators and kernels of one step, the host/device transfers, the "
        "synchronisations, the phase split of the compilation, and the element-wise loops that "
        "run on the host; the report is written when the run ends";
    d.params = {
        PassParam{
            "top",
            PassParamType::Int,
            int64_t{24},
            "rows of the operator table; 0 writes every operator that ran"},
        PassParam{
            "hints",
            PassParamType::Bool,
            true,
            "report the element-wise loops that run on the host, and why they did not go to a "
            "device"},
        PassParam{
            "json",
            PassParamType::String,
            std::string(),
            "also write the report as one JSON object to this file"},
        PassParam{
            "report",
            PassParamType::Bool,
            true,
            "print the text report when the run ends; false turns on only the diagnostics the "
            "other parameters ask for"},
        PassParam{
            "gc_stats",
            PassParamType::Int,
            int64_t{0},
            "the allocator's statistics at exit: 1 one line, 2 also one line per collection"},
        PassParam{
            "type_stats",
            PassParamType::Bool,
            false,
            "the type storage's statistics at exit (one line per kind, and the type table)"},
        PassParam{
            "effect_trace",
            PassParamType::String,
            std::string(),
            "append one JSON line per executed effectful operator to this file"},
    };
    d.make = [](const context_ptr_t &ctx, const PassArgs &args) {
        camel::profile::ReportOptions options;
        const int64_t top = args.getInt("top");
        options.top       = top < 0 ? 0 : static_cast<size_t>(top);
        options.hints     = args.getBool("hints");
        options.report    = args.getBool("report");
        const int64_t gcStats = args.getInt("gc_stats");
        return std::make_unique<ProfilePass>(
            ctx,
            options,
            args.getString("json"),
            gcStats < 0 ? 0 : static_cast<int>(gcStats),
            args.getBool("type_stats"),
            args.getString("effect_trace"));
    };
    return d;
}

PassDescriptor fastVmDescriptor(bool jit) {
    PassDescriptor d;
    d.doc =
        jit ? "the bytecode interpreter with on-demand native code" : "the bytecode interpreter";
    d.params = {statsParameter()};
    if (jit) {
        d.params.push_back(
            PassParam{
                "hot_threshold",
                PassParamType::Int,
                int64_t{1},
                "calls from one call site before a graph is compiled; 0 compiles at the first call "
                "(short test programs then run as compiled code too)"});
    }
    d.make = [jit](const context_ptr_t &ctx, const PassArgs &args) {
        FastVMConfig config = jit ? makeDefaultFastVmJitConfig() : FastVMConfig{};
        config.statsPath    = args.getString("stats");
        if (jit) {
            const int64_t threshold = args.getInt("hot_threshold");
            config.jitHotThreshold  = threshold < 0 ? 0 : static_cast<size_t>(threshold);
        }
        return std::make_unique<FastVMSchedPass>(ctx, config);
    };
    return d;
}

PassDescriptor taskflowDescriptor() {
    PassDescriptor d;
    d.doc    = "the parallel scheduler";
    d.params = {
        PassParam{
            "linear",
            PassParamType::Bool,
            false,
            "evaluate every graph node by node (for comparison and diagnosis)"},
        PassParam{
            "fanout_all",
            PassParamType::Bool,
            false,
            "fan every unordered higher-order call out, not only those worth it by measurement"},
        PassParam{
            "diag",
            PassParamType::Bool,
            false,
            "print the decision for every graph and why a graph stays node by node"},
        PassParam{
            "bytecode",
            PassParamType::Bool,
            true,
            "hand graphs that need no parallelism to the bytecode interpreter; false: the "
            "scheduler evaluates every graph itself"},
        PassParam{
            "gc_stats",
            PassParamType::Int,
            int64_t{0},
            "1: print the task graphs and collections of the run when it ends; 2: also one "
            "line per collection"},
    };
    d.make = [](const context_ptr_t &ctx, const PassArgs &args) {
        TaskflowOptions options;
        options.linear    = args.getBool("linear");
        options.fanOutAll = args.getBool("fanout_all");
        options.diag      = args.getBool("diag");
        options.bytecode  = args.getBool("bytecode");
        options.gcStats   = static_cast<int>(args.getInt("gc_stats"));
        auto pass         = std::make_unique<TaskflowExecSchedPass>(ctx);
        pass->setOptions(options);
        return pass;
    };
    return d;
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
                    // GIR formats: compact DOT by default, rich DOT for Graphviz, JSON for tools.
                    {"gir", def(PASS(GraphVizDumpPass))},
                    {"rgir", def(PASS1(GraphVizDumpPass, GraphVizDumpConfig{.rich = true}))},
                    {"gir_regions", def(PASS1(GraphVizDumpPass, GraphVizDumpConfig{.regions = true}))},
                    {"rgir_regions",
                     def(PASS1(
                         GraphVizDumpPass, (GraphVizDumpConfig{.rich = true, .regions = true})))},
                    {"jgir", def(PASS(JsonGirDumpPass))},
                    {"agir",
                     def(PASS1(GraphVizDumpPass, GraphVizDumpConfig{.annotateRoles = true}))},
                    {"argir",
                     def(PASS1(
                         GraphVizDumpPass,
                         (GraphVizDumpConfig{.rich = true, .annotateRoles = true})))},
                    {"stats", def(PASS(GraphStatsPass))},
                    {"profile", def(profileDescriptor())},
                    {"cpp",
                     def(PASS(CppDumpPass),
                         {
                             {"module", def(PASS(CppModuleDumpPass))},
                             {"inspect", def(PASS(CppInspectDumpPass))},
                         })},
                    {"topo_node_seq", def(PASS(TopoNodeSeqDumpPass))},
                    {"gc",
                     scope({
                         {"snapshot", def(PASS(GcSnapshotPass))},
                         {"verify", def(PASS(GcVerifyPass))},
                         {"minor", def(PASS(GcMinorPass))},
                         {"major", def(PASS(GcMajorPass))},
                         {"config", def(PASS(GcConfigPass))},
                         {"summary", def(PASS(GcSummaryPass))},
                          {"blocks", def(PASS(GcBlocksPass))},
                         {"views", def(PASS(GcViewsPass))},
                         {"views_escape", def(PASS(GcViewsEscapePass))},
                         {"remembered_set", def(PASS(GcRememberedSetPass))},
                         {"foreign_resource", def(PASS(GcForeignResourcePass))},
                         {"poison", def(PASS(GcPoisonPass))},
                     })},
                    {"nodevm", def(PASS(NodeVMSchedPass))},
                    {"fastvm",
                     def(fastVmDescriptor(false),
                         {
                             {"bytecode", def(PASS(BytecodeDumpPass))},
                             {"linked_bytecode", def(PASS(LinkedBytecodeDumpPass))},
                             {"jit",
                              def(fastVmDescriptor(true),
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
                    {"opt",
                     def(optimizeDescriptor(),
                         {
                             {"inline",
                              def(inlineDescriptor(std::nullopt, "std::opt::inline"),
                                  {
                                      {"small",
                                       def(inlineDescriptor(
                                           InlineTargetStrategy::Small, "std::opt::inline::small"))},
                                      {"arm",
                                       def(inlineDescriptor(
                                           InlineTargetStrategy::Arm, "std::opt::inline::arm"))},
                                      {"hybrid",
                                       def(inlineDescriptor(
                                           InlineTargetStrategy::Hybrid,
                                           "std::opt::inline::hybrid"))},
                                  })},
                             {"devirtualize", def(devirtualizeDescriptor())},
                             {"specialize", def(specializeDescriptor())},
                             {"fold", def(PASS(ConstantFoldPass))},
                             {"algebra", def(algebraDescriptor())},
                             {"extract", def(extractDescriptor())},
                             {"cse", def(PASS(CommonSubexpressionPass))},
                             {"dce", def(dceDescriptor())},
                             {"ctrl", def(PASS(ControlReductionPass))},
                         })},
                    {"taskflow", def(taskflowDescriptor())},
                    {"tfdump", def(PASS(TfDumpPass))},
                }),
            },
        }));
    // The core passes that read tag facts: the dumps show a node's attributes and numeric
    // permissions, and std::opt keeps them across rewrites (CSE combines by their merge rules).
    // A tag's `read_by` names one of these (or a module's pass that registers itself).
    for (const char *reader :
         {"std::gir",
          "std::rgir",
          "std::gir_regions",
          "std::rgir_regions",
          "std::jgir",
          "std::agir",
          "std::argir",
          "std::opt"}) {
        camel::core::tag::declareReader(reader);
    }
    return root;
}

#undef PASS
#undef PASS1

const PassScopePtr passScope = initPassScope();

std::unordered_map<std::string, std::string> passAliases = {
    // Frequently used spelling of the standalone inline stage and its strategies.
    {"std::inline", "std::opt::inline"},
    {"std::inline::small", "std::opt::inline::small"},
    {"std::inline::arm", "std::opt::inline::arm"},
    {"std::inline::hybrid", "std::opt::inline::hybrid"},
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
    {"std::tf", "std::taskflow"},
    // Common translation and dump aliases
    // Common translation and dump aliases
    {"std::cxx", "std::cpp"},
    {"std::cppmod", "std::cpp::module"},
    {"std::cppinspect", "std::cpp::inspect"},
    {"std::tns", "std::topo_node_seq"},
    {"std::gcsnap", "std::gc::snapshot"},
    {"std::gcverify", "std::gc::verify"},
    {"std::gcminor", "std::gc::minor"},
    {"std::gcmajor", "std::gc::major"},
    {"std::gcconfig", "std::gc::config"},
    {"std::gcsummary", "std::gc::summary"},
        {"std::gcblocks", "std::gc::blocks"},
    {"std::gcviews", "std::gc::views"},
    {"std::gcviews_escape", "std::gc::views_escape"},
    {"std::gcremembered", "std::gc::remembered_set"},
    {"std::gcforeign", "std::gc::foreign_resource"},
    {"std::gcpoison", "std::gc::poison"},
    {"std::bc", "std::fastvm::bytecode"},
    {"std::lbc", "std::fastvm::linked_bytecode"},
    {"std::bin", "std::fastvm::jit::dump::bin"},
    {"std::asm", "std::fastvm::jit::dump::asm"},
    {"std::rmir", "std::fastvm::jit::dump::rmir"},
    {"std::mir", "std::fastvm::jit::dump::mir"},
    {"std::tfg", "std::tfdump"},
    {"std::prof", "std::profile"},
};

struct ModulePassRegistry {
    std::mutex mutex;
    std::map<std::string, PassDescriptor> passes; // ordered for the "available passes" listing
};

ModulePassRegistry &modulePasses() {
    static ModulePassRegistry registry;
    return registry;
}

struct ModuleAliasRegistry {
    std::mutex mutex;
    std::map<std::string, std::string> aliases;
};

ModuleAliasRegistry &moduleAliases() {
    static ModuleAliasRegistry registry;
    return registry;
}

std::optional<PassDescriptor> findModulePass(const std::string &path) {
    auto &registry = modulePasses();
    std::lock_guard lock(registry.mutex);
    auto it = registry.passes.find(path);
    if (it == registry.passes.end())
        return std::nullopt;
    return it->second;
}

} // namespace

// -----------------------------------------------------------------------------
// Pass snapshot hook — unconditional (release included). See snapshot.h for the
// contract; modeled on DebugBreakpoint's GIR node slot.
// -----------------------------------------------------------------------------
namespace {

std::atomic<PassSnapshotHandler> &passSnapshotHandlerSlot() {
    static std::atomic<PassSnapshotHandler> slot{nullptr};
    return slot;
}

std::atomic<void *> &passSnapshotUserDataSlot() {
    static std::atomic<void *> slot{nullptr};
    return slot;
}

} // namespace

void setPassSnapshotHandler(PassSnapshotHandler handler, void *userData) {
    if (handler) {
        // Publish userData before the handler so an acquire-load of the handler
        // guarantees the matching userData store is visible.
        passSnapshotUserDataSlot().store(userData, std::memory_order_release);
        passSnapshotHandlerSlot().store(handler, std::memory_order_release);
    } else {
        // Clearing: drop the handler first so no new call observes a half-cleared pair.
        passSnapshotHandlerSlot().store(nullptr, std::memory_order_release);
        passSnapshotUserDataSlot().store(nullptr, std::memory_order_release);
    }
}

// -----------------------------------------------------------------------------
// Warnings raised by passes (see pass/warnings.h).
// -----------------------------------------------------------------------------
namespace {

/// The warnings of one context: those raised so far, and how many were handed to the handler.
/// Held as a pass fact, so it lives and dies with the context.
struct PassWarningState {
    mutable std::vector<Diagnostic> raised;
    mutable size_t delivered = 0;
};

constexpr const char *kPassWarningsFactKey = "pass.warnings";

const PassWarningState &passWarningStateOf(Context &ctx) {
    auto state =
        std::static_pointer_cast<const PassWarningState>(ctx.passFact(kPassWarningsFactKey));
    if (!state) {
        state = std::make_shared<PassWarningState>();
        ctx.setPassFact(kPassWarningsFactKey, state);
    }
    return *state;
}

std::atomic<PassWarningHandler> &passWarningHandlerSlot() {
    static std::atomic<PassWarningHandler> slot{nullptr};
    return slot;
}

std::atomic<void *> &passWarningUserDataSlot() {
    static std::atomic<void *> slot{nullptr};
    return slot;
}

/// Hands the warnings raised since the last call to the handler, as raised by pass `name`.
void deliverPassWarnings(Context &ctx, const std::string &name) {
    const PassWarningState &state = passWarningStateOf(ctx);
    if (state.delivered >= state.raised.size()) {
        return;
    }
    std::vector<Diagnostic> fresh(
        state.raised.begin() + static_cast<std::ptrdiff_t>(state.delivered),
        state.raised.end());
    state.delivered = state.raised.size();
    std::vector<Diagnostic> promoted;
    for (auto it = fresh.begin(); it != fresh.end();) {
        it->stage = "pass:" + specName(name);
        if (it->severity == Severity::Error) {
            promoted.push_back(std::move(*it));
            it = fresh.erase(it);
        } else {
            ++it;
        }
    }
    if (!fresh.empty()) {
        if (PassWarningHandler handler = passWarningHandlerSlot().load(std::memory_order_acquire)) {
            handler(name.c_str(), fresh, passWarningUserDataSlot().load(std::memory_order_acquire));
        }
    }
    // A warning made an error on the command line (-Werror) is an error of the pipeline: it
    // goes to the run-time sink, where the driver finds it and stops before the next pass.
    for (Diagnostic &error : promoted) {
        error.persisted = false;
        ctx.rtmDiags()->add(std::move(error));
    }
}

} // namespace

void raisePassWarning(
    Context &ctx, RuntimeDiag diag, camel::source::origin_id_t origin, const std::string &detail) {
    // Built against a sink of its own: the position is resolved through the context's sources,
    // and the run-time error sink stays free of warnings.
    Diagnostics sink("", "", ctx.sourceContext());
    auto builder = sink.of(diag);
    if (origin != camel::source::kInvalidOriginId) {
        builder.atOrigin(origin);
    }
    Diagnostic warning = builder.commit(detail);
    if (warning.suppressed) {
        return; // switched off on the command line (-Wno-<name>)
    }
    const PassWarningState &state = passWarningStateOf(ctx);
    const std::string text        = warning.toText();
    for (const Diagnostic &known : state.raised) {
        if (known.toText() == text) {
            return;
        }
    }
    state.raised.push_back(std::move(warning));
}

void setPassWarningHandler(PassWarningHandler handler, void *userData) {
    if (handler) {
        passWarningUserDataSlot().store(userData, std::memory_order_release);
        passWarningHandlerSlot().store(handler, std::memory_order_release);
    } else {
        passWarningHandlerSlot().store(nullptr, std::memory_order_release);
        passWarningUserDataSlot().store(nullptr, std::memory_order_release);
    }
}

void registerModulePass(const std::string &path, PassDescriptor descriptor) {
    const bool readsTags = descriptor.readsTags;
    auto &registry = modulePasses();
    {
        std::lock_guard lock(registry.mutex);
        registry.passes.insert_or_assign(path, std::move(descriptor));
    }
    if (readsTags) {
        // The pass is a reader of tag facts; so is every alias that already names it.
        camel::core::tag::declareReader(path);
        auto &aliases = moduleAliases();
        std::lock_guard lock(aliases.mutex);
        for (const auto &[alias, target] : aliases.aliases) {
            if (target == path) {
                camel::core::tag::declareReader(alias);
            }
        }
    }
}

void registerModulePass(const std::string &path, PassFactory factory) {
    registerModulePass(path, passWithoutParameters(std::move(factory)));
}

void registerPassAlias(const std::string &alias, const std::string &target) {
    // The core's names are the core's: an alias in `std::`, or a bare name that `std::` already
    // has (bare names are tried in `std::` first), would shadow or replace them.
    const bool bare = alias.find("::") == std::string::npos;
    if (alias.starts_with("std::") || passAliases.contains(alias) ||
        (bare && (passAliases.contains("std::" + alias) ||
                  lookupInScope(passScope, splitPath("std::" + alias))))) {
        CAMEL_LOG_WARN_S(
            "Pass",
            "alias '{}' -> '{}' ignored: the name belongs to the core's std:: namespace",
            alias,
            target);
        return;
    }
    auto &registry = moduleAliases();
    {
        std::lock_guard lock(registry.mutex);
        registry.aliases.insert_or_assign(alias, target);
    }
    // An alias of a reader is a reader too: a tag may name the short name.
    if (camel::core::tag::isReader(target)) {
        camel::core::tag::declareReader(alias);
    }
}

namespace {

std::string resolveAlias(const std::string &name) {
    if (auto it = passAliases.find(name); it != passAliases.end()) {
        return it->second;
    }
    auto &registry = moduleAliases();
    std::lock_guard lock(registry.mutex);
    auto it = registry.aliases.find(name);
    return it == registry.aliases.end() ? name : it->second;
}

/// Test-only passes: they exist only to be named by a test's pipeline and are in no default
/// pipeline, so naming one is the whole switch (H81 removed the variable that used to gate
/// them).
const std::vector<std::pair<std::string, PassFactory>> &testPasses() {
#define TEST_PASS(K, T)                                                                            \
    {                                                                                              \
        K, [](const context_ptr_t &ctx) -> std::unique_ptr<GraphIRPass> {                          \
            return std::make_unique<T>(ctx);                                                       \
        }                                                                                          \
    }
    static const std::vector<std::pair<std::string, PassFactory>> table = {
        TEST_PASS(kNativeCallbackTestPass, NativeCallbackTestPass),
        TEST_PASS(kDropCallArgumentPass, DropCallArgumentTestPass),
        TEST_PASS(kLeakBranchArmPass, LeakBranchArmTestPass),
        TEST_PASS(kShareBranchArmPass, ShareBranchArmTestPass),
        TEST_PASS(kOperatorSemanticsTestPass, OperatorSemanticsTestPass),
        TEST_PASS(kIndexSemanticsTestPass, IndexSemanticsTestPass),
        TEST_PASS(kDefinitionsTestPass, DefinitionsTestPass),
        TEST_PASS(kExpandDefinitionsTestPass, ExpandDefinitionsTestPass),
        TEST_PASS(kResourcesTestPass, ResourcesTestPass),
        TEST_PASS(kControlTestPass, ControlTestPass),
        TEST_PASS(kChunksTestPass, ChunksTestPass),
        TEST_PASS(kEdgesCycleTestPass, EdgesCycleTestPass),
        TEST_PASS(kEdgesTypeTestPass, EdgesTypeTestPass),
        TEST_PASS(kEdgesOkTestPass, EdgesOkTestPass),
        TEST_PASS(kCommitConflictTestPass, CommitConflictTestPass),
        TEST_PASS(kKeepFnTestPass, KeepFnTestPass),
        TEST_PASS(kCommitMergeTestPass, CommitMergeTestPass),
        TEST_PASS(kGraphReleaseTestPass, GraphReleaseTestPass),
        TEST_PASS(kFactNodeTestPass, FactNodeTestPass),
        TEST_PASS(kWritingsTestPass, WritingsTestPass),
        TEST_PASS(kTabledTestPass, TabledTestPass),
        TEST_PASS(kSaturateTestPass, SaturateTestPass),
        TEST_PASS(kRulesTestPass, RulesTestPass),
        TEST_PASS(kRulesGreedyTestPass, RulesGreedyTestPass),
        TEST_PASS(kSaturateSafeTestPass, SaturateSafeTestPass),
        TEST_PASS(kSaturateWideTestPass, SaturateWideTestPass),
        TEST_PASS(kSaturateOneRoundTestPass, SaturateOneRoundTestPass),
        TEST_PASS(kRegionsTestPass, RegionsTestPass),
        TEST_PASS(kArmLoopRegionsTestPass, ArmLoopRegionsTestPass),
        TEST_PASS(kExecPlanTestPass, ExecPlanTestPass),
        TEST_PASS(kExhaustPermSpaceTestPass, ExhaustPermSpaceTestPass),
        TEST_PASS(kLoopCollectTestPass, LoopCollectTestPass),
        TEST_PASS(kLoopCollectSummaryTestPass, LoopCollectSummaryTestPass),
        TEST_PASS(kFactStoreTestPass, FactStoreTestPass),
        TEST_PASS(kFactSeedTestPass, FactSeedTestPass),
        TEST_PASS(kFactCheckTestPass, FactCheckTestPass),
        TEST_PASS(kFactPersistTestPass, FactPersistTestPass),
    };
#undef TEST_PASS
    return table;
}

/// The descriptor of a pass name (no parameters written), or nothing.
std::optional<PassDescriptor> findDescriptor(const std::string &name) {
    // 1. Resolve aliases
    const std::string resolved = resolveAlias(name);

    // 2. Full paths with :: search by scope hierarchy
    if (resolved.find("::") != std::string::npos) {
        auto path = splitPath(resolved);
        if (!path.empty()) {
            if (auto found = lookupInScope(passScope, path))
                return found;
        }
    } else {
        // 3. No :: try std::name first, then the global name
        if (auto found = lookupInScope(passScope, splitPath("std::" + resolved)))
            return found;
        if (auto found = lookupInScope(passScope, std::vector<std::string>{resolved}))
            return found;
    }

    // 4. Passes contributed by loaded modules (full names such as "tensor::fuse")
    if (auto found = findModulePass(resolved)) {
        return found;
    }

    // 5. Test-only passes. They are reachable by name and by nothing else; no pipeline adds them.
    for (const auto &[testName, factory] : testPasses()) {
        if (resolved == testName) {
            return passWithoutParameters(factory);
        }
    }
    return std::nullopt;
}

struct ResolvedPass {
    PassFactory factory;                // the pass with its parameters bound; null if not usable
    bool known = false;                 // the name is a pass
    std::optional<std::string> problem; // the parameters are wrong
    /// When the problem is a pass module that does not compile: its first error, as a secondary
    /// location (H36c).
    std::optional<DiagLabel> errorLocation;
};

/// Resolves `name` or `name(key=value, ...)`. Prints nothing.
ResolvedPass resolvePass(const std::string &spec) {
    ResolvedPass result;
    const auto descriptor = findDescriptor(specName(spec));
    if (!descriptor) {
        return result;
    }
    result.known = true;
    camel::execute::detail::ParsedSpec parsed;
    if (auto problem = camel::execute::detail::parseSpec(spec, parsed)) {
        result.problem = std::move(problem);
        return result;
    }
    PassArgs args;
    if (auto problem =
            camel::execute::detail::bindArgs(parsed.name, descriptor->params, parsed, args)) {
        result.problem = std::move(problem);
        return result;
    }
    result.factory = [make = descriptor->make, args = std::move(args)](const context_ptr_t &ctx) {
        return make(ctx, args);
    };
    return result;
}

Diagnostic invalidParameterDiagnostic(
    const std::string &spec, const std::string &problem,
    const std::optional<DiagLabel> &errorLocation = std::nullopt) {
    setPipelineStage("pass:" + specName(spec));
    auto diag = DiagnosticBuilder::of(RuntimeDiag::InvalidPassParameter).commit(spec, problem);
    if (errorLocation) {
        diag.related.push_back(*errorLocation);
    }
    diag.stage = "pass:" + specName(spec);
    setPipelineStage("");
    return diag;
}

void printPass(std::ostream &help, const std::string &path, const PassDescriptor &descriptor) {
    help << std::format("  {}", path);
    if (!descriptor.params.empty()) {
        help << std::format("({})", camel::execute::detail::describeParams(descriptor.params));
    }
    help << '\n';
    if (!descriptor.doc.empty()) {
        help << std::format("      {}\n", descriptor.doc);
    }
    for (const auto &param : descriptor.params) {
        if (!param.doc.empty()) {
            help << std::format("      {}: {}\n", param.name, param.doc);
        }
    }
}

/// Lists the available passes on stderr. The listing is help text for the command line:
/// stdout stays reserved for program output and the diagnostics array.
void listAvailablePasses(const std::string &name) {
    std::ostream &help = std::cerr;
    help << std::format("Pass <{}> not found, available passes are:\n", name);
    std::vector<std::pair<std::string, PassDescriptor>> allPaths;
    collectPassPaths(passScope, "", allPaths);
    for (const auto &[path, descriptor] : allPaths) {
        printPass(help, path, descriptor);
    }
    {
        auto &registry = modulePasses();
        std::lock_guard lock(registry.mutex);
        if (!registry.passes.empty()) {
            help << std::format("Passes contributed by loaded modules:\n");
            for (const auto &[path, descriptor] : registry.passes) {
                printPass(help, path, descriptor);
            }
        }
    }
    help << std::format("Available aliases are:\n");
    for (const auto &[alias, target] : passAliases) {
        help << std::format("  {} -> {}\n", alias, target);
    }
    {
        auto &registry = moduleAliases();
        std::lock_guard lock(registry.mutex);
        for (const auto &[alias, target] : registry.aliases) {
            help << std::format("  {} -> {}\n", alias, target);
        }
    }
    help << std::endl;
    help << "A pass is written `name` or `name(key=value, ...)`; the values are constants "
            "(bool, int, float, string).\n";
}

} // namespace

PassFactory findPassFactory(const std::string &name, std::ostream &os) {
    (void)os;
    auto resolved = resolvePass(name);
    if (resolved.problem) {
        throw invalidParameterDiagnostic(name, *resolved.problem);
    }
    if (!resolved.factory) {
        listAvailablePasses(name);
    }
    return resolved.factory;
}

/// Resolves `spec`, importing the module that contributes a `<module>::<pass>` name when the
/// program has not imported it yet: a pass list may name a pass of a module the program does
/// not import, and loading the module registers the pass. A name that stays unknown resolves
/// to no factory, and a bad parameter list keeps its problem.
ResolvedPass resolvePassWithModule(const std::string &spec, const context_ptr_t &ctx) {
    ResolvedPass resolved = resolvePass(spec);
    if (resolved.problem || resolved.factory || !ctx) {
        return resolved;
    }
    const std::string name = resolveAlias(specName(spec));
    const size_t sep       = name.find("::");
    if (sep == std::string::npos || sep == 0 || name.substr(0, sep) == "std") {
        return resolved;
    }
    try {
        (void)ctx->importModule(name.substr(0, sep));
    } catch (const Diagnostic &) {
        // No such module: the name stays unknown and is reported by the caller.
    } catch (const std::exception &) {
    }
    resolved = resolvePass(spec);
    if (resolved.problem || resolved.factory) {
        return resolved;
    }
    // A function written in Camel: the module is a library of its own, compiled now.
    std::string problem;
    std::optional<DiagLabel> errorLocation;
    camel::execute::registerCamelPass(
        ctx, name.substr(0, sep), name.substr(sep + 2), problem, &errorLocation);
    resolved = resolvePass(spec);
    if (!resolved.factory && !problem.empty()) {
        resolved.problem       = problem;
        resolved.errorLocation = errorLocation;
    }
    return resolved;
}

void requireKnownPasses(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    for (const auto &name : passes) {
        auto resolved = resolvePassWithModule(name, ctx);
        if (resolved.problem) {
            throw invalidParameterDiagnostic(name, *resolved.problem, resolved.errorLocation);
        }
        if (resolved.factory) {
            continue;
        }
        listAvailablePasses(name);
        setPipelineStage("pass:" + specName(name));
        auto unknown  = DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(name);
        unknown.stage = "pass:" + specName(name);
        setPipelineStage("");
        throw unknown;
    }
}

std::optional<size_t>
firstConsumingPass(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    for (size_t i = 0; i < passes.size(); ++i) {
        auto factory = resolvePassWithModule(passes[i], ctx).factory;
        if (!factory) {
            continue;
        }
        const auto pass = factory(ctx);
        if (dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr ||
            dynamic_cast<GraphTranslatePass *>(pass.get()) != nullptr) {
            return i;
        }
    }
    return std::nullopt;
}

bool passListTranslatesProgram(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    for (const auto &name : passes) {
        auto factory = resolvePassWithModule(name, ctx).factory;
        if (!factory) {
            continue;
        }
        const auto pass = factory(ctx);
        // The first pass that consumes the graph decides what the list does. Before it, rewrite
        // passes only transform the program; after it, the graph is gone.
        if (dynamic_cast<GraphTranslatePass *>(pass.get()) != nullptr) {
            return true;
        }
        if (dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr) {
            return false;
        }
    }
    return false;
}

std::optional<Diagnostic>
passesNeverRunWarning(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    const auto consumer = firstConsumingPass(passes, ctx);
    if (!consumer.has_value() || *consumer + 1 >= passes.size()) {
        return std::nullopt;
    }
    std::string rest;
    for (size_t i = *consumer + 1; i < passes.size(); ++i) {
        rest += (rest.empty() ? "'" : ", '") + passes[i] + "'";
    }
    auto warning =
        DiagnosticBuilder::of(RuntimeDiag::PassesNeverRun).commit(passes[*consumer], rest);
    warning.stage = "pass:" + specName(passes[*consumer]);
    return warning;
}

namespace {

/// The Camel passes of the pipeline that declare `#reads<...>` on their function, by the tag
/// they name: a pass written in Camel reads tags by saying so, the counterpart of a C++
/// module's `read_by` (design principles 6.2). The module is loaded here because the warning
/// is computed before the pipeline builds the passes.
std::map<std::string, std::vector<std::string>> declaredReadersOf(
    const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    std::map<std::string, std::vector<std::string>> readers;
    for (const auto &spec : passes) {
        const std::string pass = resolveAlias(specName(spec));
        const size_t sep       = pass.rfind("::");
        if (sep == std::string::npos) {
            continue;
        }
        const std::string moduleName = pass.substr(0, sep);
        const std::string function   = pass.substr(sep + 2);
        // `std::` passes are the core's (C++), not a module: loading "std" would be an error.
        if (moduleName == "std" || moduleName.starts_with("std::")) {
            continue;
        }
        try {
            const auto module = ctx->importModule(moduleName);
            if (!module || !module->loaded()) {
                continue;
            }
            const auto facts = camel::core::tag::ofDeclaration(*module, Reference(function));
            if (facts == camel::core::attr::kNoAttrs) {
                continue;
            }
            const auto value =
                camel::core::attr::valueOf(facts, camel::core::attr::key("reads"));
            if (value == camel::core::attr::kNoValue) {
                continue;
            }
            const auto add = [&](const std::string &tag) {
                // The fact stores the name as the pass wrote it; the use of the tag in a
                // program names it qualified. Resolve it in the pass's module, then in the core.
                std::string key = tag;
                if (const auto id = camel::core::tag::find(moduleName, tag)) {
                    key = camel::core::tag::get(*id)->name;
                } else if (const auto core = camel::core::tag::find(tag)) {
                    key = camel::core::tag::get(*core)->name;
                }
                readers[key].push_back(pass);
            };
            const camel::core::attr::Value decoded = camel::core::attr::value(value);
            if (decoded.kind == camel::core::attr::ValueKind::Symbol) {
                add(decoded.text);
            } else if (decoded.kind == camel::core::attr::ValueKind::Tuple) {
                for (const auto item : decoded.items) {
                    const camel::core::attr::Value tag = camel::core::attr::value(item);
                    if (tag.kind == camel::core::attr::ValueKind::Symbol) {
                        add(tag.text);
                    }
                }
            }
        } catch (...) {
            // A module that cannot be read here names no readers; the pass itself reports why
            // when the pipeline builds it.
        }
    }
    return readers;
}

} // namespace

bool pipelineNamesProfile(const std::vector<std::string> &passes) {
    for (const auto &spec : passes) {
        if (resolveAlias(specName(spec)) == "std::profile") {
            return true;
        }
    }
    return false;
}

std::vector<Diagnostic> tagsNeverReadWarnings(
    const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    std::vector<Diagnostic> warnings;
    if (!ctx) {
        return warnings;
    }
    // No tag is written, so there is nothing to warn about: returning here also keeps the
    // pipeline from loading pass modules it would not otherwise load.
    if (ctx->tagUses().empty()) {
        return warnings;
    }
    // The names in the pipeline, as written and as they resolve (an alias strips to the pass's
    // own name): a tag's `read_by` may name either.
    std::vector<std::string> inPipeline;
    inPipeline.reserve(passes.size() * 2);
    for (const auto &spec : passes) {
        const std::string name = specName(spec);
        inPipeline.push_back(name);
        inPipeline.push_back(resolveAlias(name));
    }
    const auto camelReaders = declaredReadersOf(passes, ctx);
    for (const auto &use : ctx->tagUses()) {
        // The passes the tag's declaration names, and the Camel passes that say with
        // `#reads<...>` that they read it.
        std::vector<std::string> readers;
        for (const std::string &reader : use.readBy) {
            // Only a pass that declared itself a reader of tag facts counts: a name that
            // happens to be in the pipeline but reads no facts leaves the tag with no effect,
            // which is what the warning says.
            if (camel::core::tag::isReader(reader)) {
                readers.push_back(reader);
            }
        }
        if (const auto declared = camelReaders.find(use.name); declared != camelReaders.end()) {
            for (const std::string &reader : declared->second) {
                if (std::find(readers.begin(), readers.end(), reader) == readers.end()) {
                    readers.push_back(reader);
                }
            }
        }
        bool read = false;
        for (const std::string &reader : readers) {
            for (const std::string &name : inPipeline) {
                if (name == reader || name.starts_with(reader + "::")) {
                    read = true;
                    break;
                }
            }
            if (read) {
                break;
            }
        }
        if (read) {
            continue;
        }
        std::string readersText;
        for (const std::string &reader : readers) {
            readersText += (readersText.empty() ? "" : ", ") + reader;
        }
        auto warning = DiagnosticBuilder::of(SemanticDiag::TagHasNoReader)
                           .atOrigin(use.origin)
                           .commit(use.name, readersText);
        warning.stage = "tag:" + use.name;
        warnings.push_back(std::move(warning));
    }
    return warnings;
}

PassApplyResult applyPassesDetailed(
    const camel::runtime::Program *program, const std::vector<std::string> &passes,
    const context_ptr_t &ctx, std::ostream &os) {
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
    // Every name is checked before the first pass runs: an unknown pass written after a
    // scheduling pass must not let the program execute first.
    requireKnownPasses(passes, ctx);
    size_t passOutputIndex = 0;
    for (const auto &p : passes) {
        if (program == nullptr) {
            return {nullptr, PassApplyStatus::Consumed};
        }

        auto factory = findPassFactory(p, os);
        if (factory) {
            camel::core::mm::autoSpace().safepoint(std::format("before pass {}", p));
            EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit(p.c_str(), program->root()); });
            camel::utils::phase::Scope phaseScope("pass", p);
            auto pass = factory(ctx);
            // Diagnostics raised while this pass runs carry its stage: "run" for a scheduling
            // pass (the program executes), "pass:<name>" for rewrite and translation passes.
            struct StageScope {
                explicit StageScope(std::string stage) { setPipelineStage(std::move(stage)); }
                ~StageScope() { setPipelineStage(""); }
            } stageScope(
                dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr ? std::string("run")
                                                                         : "pass:" + specName(p));
            // A program runs only when every parameter of `main` has a value. Parameters
            // left open are fine for rewriting and translation (they are the inputs of what
            // is translated); nothing may execute with a slot nobody filled.
            if (dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr) {
                const auto open = camel::execute::unboundEntryParameters(program->root());
                if (!open.empty()) {
                    std::string names;
                    for (size_t i = 0; i < open.size(); ++i) {
                        names += (i == 0                 ? "'"
                                  : i + 1 == open.size() ? " and '"
                                                         : ", '") +
                                 open[i] + "'";
                    }
                    auto diag = ctx->rtmDiags()->of(RuntimeDiag::EntryArgumentMissing);
                    if (const uint64_t origin = camel::execute::entryFunctionOrigin(*ctx);
                        origin != camel::source::kInvalidOriginId) {
                        diag.atOrigin(origin);
                    }
                    diag.commit(open.size() == 1 ? "parameter" : "parameters", names);
                    return {nullptr, PassApplyStatus::Failed};
                }
            }
            // A program with graphs that have several writings per value: a pass that consumes
            // it (a back end, an exporter) gets one writing per value, picked by the default
            // cost; a pass that does not know the state does not get the program.
            if (camel::runtime::hasWritings(*program)) {
                const bool consumes = dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr ||
                                      dynamic_cast<GraphTranslatePass *>(pass.get()) != nullptr;
                if (consumes) {
                    CAMEL_LOG_INFO_S(
                        "Pass",
                        "run | passes | extract before {}: one writing per value, cost model "
                        "'weighted'",
                        p);
                    program = camel::runtime::extractWritings(
                        ctx, program, *camel::runtime::findCostModel("weighted"));
                    ctx->settleRuntimeGraphs(program);
                } else if (!pass->acceptsWritings()) {
                    setPipelineStage("pass:" + specName(p));
                    ctx->rtmDiags()->of(RuntimeDiag::PassNeedsExtraction).commit(p);
                    return {nullptr, PassApplyStatus::Failed};
                }
            }
            try {
                program = pass->apply(program, os);
            } catch (const DiagnosticsLimitExceededBaseException &) {
                throw; // diagnostics already recorded; the caller dumps them
            } catch (const camel::core::ProcessExit &exit) {
                // The program called exit(code): the run is over, with that exit code and no
                // diagnostic. The pipeline ends as after a scheduling pass that completed.
                ctx->setProcessExitCode(exit.code);
                CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> exit({})", p, exit.code);
                return {nullptr, PassApplyStatus::Consumed};
            } catch (const camel::core::mm::MemoryExhausted &e) {
                // A memory space is at its limit, or the host has no memory left. This is a
                // limit the program (or what the pass made of it) ran into: it is reported with the space, the usage, the limit and the
                // setting that raises it, at the graph that was being built when one is known.
                setPipelineStage("pass:" + specName(p));
                auto diag = ctx->rtmDiags()->of(RuntimeDiag::MemoryExhausted);
                // The allocator names a source only when the caller told it one (the loop
                // driver does for its tapes); otherwise the bytecode the VM was running is the
                // innermost place the program asked for memory.
                uint64_t origin = e.origin();
                if (origin == camel::source::kInvalidOriginId && ctx->sourceContext() != nullptr) {
                    origin = ctx->sourceContext()->currentRuntimeOrigin();
                }
                if (origin != camel::source::kInvalidOriginId) {
                    diag.atOrigin(origin);
                }
                diag.commit(std::format(" in pass '{}'", p), std::string(e.what()));
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (memory exhausted)", p);
                return {nullptr, PassApplyStatus::Failed};
            } catch (const std::exception &e) {
                // A pass that gives up with a plain exception (graph validation at commit, the
                // execution-order builder meeting a cycle, an arena limit) hit a defect of the
                // compiler, not of the program. It is still reported as a diagnostic, so that
                // --output-format json stays a valid diagnostics array. There is no source
                // position for it. The stage names the pass even for a scheduling pass: the
                // failure belongs to the pass, whatever part of the program had run.
                setPipelineStage("pass:" + specName(p));
                std::string what = e.what();
                while (!what.empty() && (what.back() == '.' || what.back() == '\n')) {
                    what.pop_back(); // the diagnostic's message ends the sentence itself
                }
                ctx->rtmDiags()->of(RuntimeDiag::PassInternalError).commit(p, what);
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (internal error)", p);
                return {nullptr, PassApplyStatus::Failed};
            }
            camel::core::mm::autoSpace().safepoint(std::format("after pass {}", p));
            // What the pass had to say about the program, before the next pass runs it.
            deliverPassWarnings(*ctx, p);
            // The programs the pass made and nothing holds are no longer needed.
            ctx->settleRuntimeGraphs(program);
            if (ctx->rtmDiags()->hasErrors()) {
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (see diagnostics)", p);
                return {nullptr, PassApplyStatus::Failed};
            }
            if (!program) {
                CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> consumed", p);
                return {nullptr, PassApplyStatus::Consumed};
            }
            if (PassSnapshotHandler snapshotHandler =
                    passSnapshotHandlerSlot().load(std::memory_order_acquire)) {
                snapshotHandler(
                    p.c_str(),
                    ++passOutputIndex,
                    program->root(),
                    passSnapshotUserDataSlot().load(std::memory_order_acquire));
            }
            CAMEL_LOG_INFO_S(
                "Pass",
                "run | passes | OK {} -> next graph '{}'",
                p,
                program->root()->name());
        } else {
            setPipelineStage("pass:" + specName(p));
            auto unknown = DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(p);
            setPipelineStage("");
            throw unknown;
        }
    }

    EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit("GIR-Z", program ? program->root() : nullptr); });
    return {program, PassApplyStatus::Transformed};
}

const camel::runtime::Program *applyPasses(
    const camel::runtime::Program *program, const std::vector<std::string> &passes,
    const context_ptr_t &ctx, std::ostream &os) {
    return applyPassesDetailed(program, passes, ctx, os).program;
}

