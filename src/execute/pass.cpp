// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/pass.h"
#include "camel/common/scope.h"
#include "camel/core/array/block_selftest.h"
#include "camel/core/type/array_type_selftest.h"
#include "camel/core/type/type_identity_selftest.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/mm.h"
#include "camel/core/module/module.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/resources.h"
#include "camel/core/tags.h"
#include "camel/core/mm/block.h"
#include "camel/core/mm/foreign_handle.h"
#include "camel/core/mm/profiler.h"
#include "camel/core/process_exit.h"
#include "camel/core/rtdata/view_selftest.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/observer.h"
#include "camel_pass.h"
#include "camel/passes/test/chunks.h"
#include "camel/passes/test/context_lifetime.h"
#include "camel/passes/test/contexts.h"
#include "camel/passes/test/derivatives.h"
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
#include "camel/passes/test/throw_bad_alloc.h"
#include "camel/passes/test/exec_plan.h"
#include "camel/passes/test/regions.h"
#include "camel/passes/test/retype.h"
#include "camel/passes/test/arm_loop_regions.h"
#include "camel/passes/test/loop_inputs.h"
#include "camel/passes/trans/cml/cml.h"
#include "camel/passes/trans/json/gir.h"
#include "macro/macro.h"
#include "pass_declarations.h"
#include "pass_spec.h"
#include "passes/opt/defun/defunctionalize.h"
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
#include "passes/test/impl_body.h"
#include "passes/test/index_semantics.h"
#include "passes/test/native_call.h"
#include "passes/test/resources.h"
#include "passes/test/semantics.h"
#include "passes/test/tags.h"
#include "passes/trans/cpp/cpp_export.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/profile/profile.h"
#include "passes/trans/stats/stats.h"
#include "passes/trans/rulesets/rulesets.h"
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
#include <tuple>
#include <unordered_map>

using namespace camel::core::error;
using namespace camel::core::context;
using camel::runtime::GCGraph;

const camel::runtime::Program *
NullGraphIRPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    (void)os;
    return nullptr;
}


namespace {

std::string trimText(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

/// The name of a specification, parameters removed: the words after the first are the
/// `key=value` parameters.
std::string specName(const std::string &spec) {
    const auto end = spec.find_first_of(" \t\r\n");
    return trimText(end == std::string::npos ? spec : spec.substr(0, end));
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

class ArrayTypeSelfTestPass final : public GraphIRPass {
  public:
    explicit ArrayTypeSelfTestPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << "ARRAY_TYPE_SELFTEST ok "
           << camel::core::type::debugRunArrayTypeSelfTest() << "\n";
        return program;
    }
};

class TypeIdentitySelfTestPass final : public GraphIRPass {
  public:
    explicit TypeIdentitySelfTestPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << "TYPE_IDENTITY_SELFTEST ok "
           << camel::core::type::debugRunTypeIdentitySelfTest() << "\n";
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

/// The `growth_budget` parameter of every pass built on the rewrite engine (design principles
/// 9.3): the nodes specialization and inlining may copy. Its declaration is in stdlib/std.cml.
GrowthBudget growthBudgetOf(const PassArgs &args, std::string passName) {
    GrowthBudget budget;
    const int64_t nodes = args.getInt("growth_budget");
    budget.nodes        = nodes < 0 ? 0 : static_cast<size_t>(nodes);
    budget.written      = args.given("growth_budget");
    budget.passName     = std::move(passName);
    return budget;
}

// A file a pass writes to is a `string?` parameter: null (left out) writes none, which the passes
// read as the empty path.
std::string pathArgument(const PassArgs &args, const std::string &name) {
    return args.isNull(name) ? std::string() : args.getString(name);
}

/// The `constant_budget` parameter of the passes that fold (design principles 9.6): the bytes of
/// constants folding may materialize in a graph. A negative value is read as 0, which
/// materializes none of them, the way `growth_budget`'s 0 copies nothing. Its declaration is in
/// stdlib/std.cml.
size_t constantBudgetOf(const PassArgs &args) {
    const int64_t bytes = args.getInt("constant_budget");
    return bytes < 0 ? 0 : static_cast<size_t>(bytes);
}

PassMaker makeGraphVizPass(GraphVizDumpConfig config) {
    return [config](const context_ptr_t &ctx, const PassArgs &) {
        return std::make_unique<GraphVizDumpPass>(ctx, config);
    };
}

template <class Pass, class... Args> PassMaker makePass(Args &&...args) {
    return [arguments = std::tuple<Args...>(std::forward<Args>(args)...)](
               const context_ptr_t &ctx, const PassArgs &) {
        return std::apply(
            [&](const auto &...unpacked) {
                return std::make_unique<Pass>(ctx, unpacked...);
            },
            arguments);
    };
}

PassMaker optimizePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        OptimizeRewriteConfig config;
        config.safeFloatingPoint   = args.getBool("safe_fp");
        config.checkScreens        = args.getBool("check_screens");
        config.growthBudget        = growthBudgetOf(args, "std::opt");
        config.constantBudgetBytes = constantBudgetOf(args);
        return std::make_unique<OptimizeRewritePass>(ctx, config);
    };
}

/// The lowering step itself. `report` is declared in stdlib/std.cml with a default of true: a
/// program that names the pass is asking what it did, and the pipeline runs it with the parameter
/// off (`fixedLoweringSteps`), so an inserted step writes nothing on the program's own output.
PassMaker defunctionalizePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<DefunctionalizeRewritePass>(
            ctx, args.getBool("report"), args.getBool("debug"));
    };
}

PassMaker inlinePass(std::optional<InlineTargetStrategy> strategy, std::string passName) {
    return [strategy, passName = std::move(passName)](
               const context_ptr_t &ctx, const PassArgs &args) {
        InlineRewriteConfig config{};
        if (strategy) {
            config.inlineStrategy = *strategy;
        }
        return std::make_unique<InlineRewritePass>(
            ctx, config, growthBudgetOf(args, passName));
    };
}

PassMaker specializePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<SpecializeRewritePass>(
            ctx, growthBudgetOf(args, "std::opt::specialize"));
    };
}

PassMaker devirtualizePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<DevirtualizeRewritePass>(
            ctx, growthBudgetOf(args, "std::opt::devirtualize"));
    };
}

PassMaker extractPass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<ExtractPass>(ctx, args.getString("cost"));
    };
}

/// The fold pass on its own, with the same `constant_budget` the integrated pipeline reads.
PassMaker foldPass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<ConstantFoldPass>(ctx, constantBudgetOf(args));
    };
}

/// The saturation of the rules library, under the core's name. Like `std::opt::algebra`,
/// the laws are the library's (`opt::saturate`); unlike it, the extraction is not made, so
/// the program stops in the state where a value has several writings.
PassMaker saturatePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<OptimizeSaturatePass>(
            ctx, args.getInt("rounds"), args.getBool("strict"));
    };
}

PassMaker dcePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<DeadCodePass>(ctx, args.getBool("values_only"));
    };
}

/// The algebraic simplification under the core's name: the rules library's pass
/// (`rules::simplify`, or `rules::simplify strict=true` for `safe_fp=true`), so the laws live once,
/// in stdlib/rules.cml. `neutral_only` has no library equivalent; it keeps the C++ reference
/// mode (the integer neutral elements only) that graph.plan.toml compares a Camel pass against.
const camel::runtime::Program *runRulesAlgebra(
    const camel::core::context::context_ptr_t &ctx, const camel::runtime::Program *program,
    bool safeFp, std::ostream &os, GraphIRPass *caller);

class RulesAlgebraPass final : public GraphIRPass {
  public:
    RulesAlgebraPass(const camel::core::context::context_ptr_t &ctx, bool safeFp)
        : GraphIRPass(ctx), safeFp_(safeFp) {}
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        return runRulesAlgebra(context_, program, safeFp_, os, this);
    }

  private:
    bool safeFp_;
};

PassMaker algebraPass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        if (args.getBool("neutral_only")) {
            return std::unique_ptr<GraphIRPass>(std::make_unique<AlgebraPass>(ctx, true));
        }
        return std::unique_ptr<GraphIRPass>(
            std::make_unique<RulesAlgebraPass>(ctx, args.getBool("safe_fp")));
    };
}

PassMaker profilePass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        camel::profile::ReportOptions options;
        const int64_t top = args.getInt("top");
        options.top       = top < 0 ? 0 : static_cast<size_t>(top);
        options.hints     = args.getBool("hints");
        options.report    = args.getBool("report");
        const int64_t gcStats = args.getInt("gc_stats");
        return std::make_unique<ProfilePass>(
            ctx,
            options,
            pathArgument(args, "json"),
            gcStats < 0 ? 0 : static_cast<int>(gcStats),
            args.getBool("type_stats"),
            pathArgument(args, "effect_trace"),
            args.getBool("opstats"),
            args.getBool("variant_trace"));
    };
}

PassMaker fastVmPass(bool jit) {
    return [jit](const context_ptr_t &ctx, const PassArgs &args) {
        FastVMConfig config = jit ? makeDefaultFastVmJitConfig() : FastVMConfig{};
        config.statsPath    = pathArgument(args, "stats");
        if (jit) {
            const int64_t threshold = args.getInt("hot_threshold");
            config.jitHotThreshold  = threshold < 0 ? 0 : static_cast<size_t>(threshold);
        }
        return std::make_unique<FastVMSchedPass>(ctx, config);
    };
}

PassMaker taskflowPass() {
    return [](const context_ptr_t &ctx, const PassArgs &args) {
        TaskflowOptions options;
        options.linear    = args.getBool("linear");
        options.fanOutAll = args.getBool("fanout_all");
        options.diag      = args.getBool("diag");
        options.segments  = args.getBool("segments");
        options.bytecode  = args.getBool("bytecode");
        options.gcStats   = static_cast<int>(args.getInt("gc_stats"));
        options.gcStress  = args.getBool("gc_stress");
        options.blockingLeaves = args.getBool("blocking_leaves");
        auto pass         = std::make_unique<TaskflowExecSchedPass>(ctx);
        pass->setOptions(options);
        return pass;
    };
}

/// Binds the kernel of one core pass to the module and name its declaration in stdlib/std.cml
/// gives it. Nothing here declares a pass: the name belongs to the declaration, the parameters
/// belong to the declaration, and this is only the function that implements it (design
/// principles 9.3).
void registerCoreKernel(const char *name, PassMaker make, bool readsTags = false) {
    PassDescriptor descriptor;
    descriptor.make      = std::move(make);
    descriptor.readsTags = readsTags;
    registerModulePass(std::string("std::") + name, std::move(descriptor));
}

/// The core's kernels, one per declaration in stdlib/std.cml. A declaration with no kernel is a
/// pass the language declares and the core does not implement; the pipeline refuses it, and the
/// guard that resolves every declaration catches it.
void registerCoreKernels() {
    registerCoreKernel("nop", makePass<NullGraphIRPass>());
    registerCoreKernel("macro", makePass<MacroRewritePass>());
    registerCoreKernel("defunctionalize", defunctionalizePass());
    registerCoreKernel("gir", makeGraphVizPass({}));
    registerCoreKernel("rgir", makeGraphVizPass(GraphVizDumpConfig{.rich = true}));
    registerCoreKernel("gir_regions", makeGraphVizPass(GraphVizDumpConfig{.regions = true}));
    registerCoreKernel(
        "rgir_regions", makeGraphVizPass(GraphVizDumpConfig{.rich = true, .regions = true}));
    registerCoreKernel("jgir", jgirPass());
    registerCoreKernel("cml", cmlPass());
    registerCoreKernel("agir", makeGraphVizPass(GraphVizDumpConfig{.annotateRoles = true}));
    registerCoreKernel(
        "argir", makeGraphVizPass(GraphVizDumpConfig{.rich = true, .annotateRoles = true}));
    registerCoreKernel("stats", makePass<GraphStatsPass>());
    registerCoreKernel(
        "rulesets",
        [](const context_ptr_t &ctx, const PassArgs &args) {
            return std::make_unique<RulesetsPass>(
                ctx,
                args.isNull("query") ? std::string() : args.getString("query"),
                args.getBool("forward"));
        });
    registerCoreKernel("profile", profilePass());
    registerCoreKernel("cpp", makePass<CppDumpPass>());
    registerCoreKernel("cpp::module_file", makePass<CppModuleDumpPass>());
    registerCoreKernel("cpp::inspect", makePass<CppInspectDumpPass>());
    registerCoreKernel("topo_node_seq", makePass<TopoNodeSeqDumpPass>());
    registerCoreKernel("tfdump", makePass<TfDumpPass>());
    registerCoreKernel("arraytype", makePass<ArrayTypeSelfTestPass>());
    registerCoreKernel("identitytype", makePass<TypeIdentitySelfTestPass>());
    registerCoreKernel("gc::snapshot", makePass<GcSnapshotPass>());
    registerCoreKernel("gc::verify", makePass<GcVerifyPass>());
    registerCoreKernel("gc::minor", makePass<GcMinorPass>());
    registerCoreKernel("gc::major", makePass<GcMajorPass>());
    registerCoreKernel("gc::config", makePass<GcConfigPass>());
    registerCoreKernel("gc::summary", makePass<GcSummaryPass>());
    registerCoreKernel("gc::blocks", makePass<GcBlocksPass>());
    registerCoreKernel("gc::views", makePass<GcViewsPass>());
    registerCoreKernel("gc::views_escape", makePass<GcViewsEscapePass>());
    registerCoreKernel("gc::remembered_set", makePass<GcRememberedSetPass>());
    registerCoreKernel("gc::foreign_resource", makePass<GcForeignResourcePass>());
    registerCoreKernel("gc::poison", makePass<GcPoisonPass>());
    registerCoreKernel("nvm", makePass<NodeVMSchedPass>());
    registerCoreKernel("fvm", fastVmPass(false));
    registerCoreKernel("fastvm::bytecode", makePass<BytecodeDumpPass>());
    registerCoreKernel("fastvm::linked_bytecode", makePass<LinkedBytecodeDumpPass>());
    registerCoreKernel("fastvm::jit", fastVmPass(true));
    registerCoreKernel("fastvm::jit_asm", makePass<JitAsmDumpPass>());
    registerCoreKernel("fastvm::jit_bin", makePass<JitBinaryDumpPass>());
    registerCoreKernel("fastvm::jit_rmir", makePass<JitRmirDumpPass>());
    registerCoreKernel("fastvm::jit_mir", makePass<JitMirDumpPass>());
    registerCoreKernel("taskflow", taskflowPass());
    registerCoreKernel("opt", optimizePass());
    registerCoreKernel("opt::inline", inlinePass(std::nullopt, "std::opt::inline"));
    registerCoreKernel(
        "opt::small", inlinePass(InlineTargetStrategy::Small, "std::opt::inline::small"));
    registerCoreKernel("opt::arm", inlinePass(InlineTargetStrategy::Arm, "std::opt::inline::arm"));
    registerCoreKernel(
        "opt::hybrid", inlinePass(InlineTargetStrategy::Hybrid, "std::opt::inline::hybrid"));
    registerCoreKernel("opt::devirtualize", devirtualizePass());
    registerCoreKernel("opt::specialize", specializePass());
    registerCoreKernel("opt::fold", foldPass());
    registerCoreKernel("opt::algebra", algebraPass());
    registerCoreKernel("opt::saturate", saturatePass());
    registerCoreKernel("opt::extract", extractPass());
    registerCoreKernel("opt::cse", makePass<CommonSubexpressionPass>());
    registerCoreKernel("opt::dce", dcePass());
    registerCoreKernel("opt::order", makePass<OrderReductionPass>());
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
}

const std::map<std::string, std::string> passAliases = {
    // Frequently used spelling of the standalone inline stage and its strategies.
    {"std::inline", "std::opt::inline"},
    {"std::inline::small", "std::opt::small"},
    {"std::opt::inline::small", "std::opt::small"},
    {"std::opt::inline::arm", "std::opt::arm"},
    {"std::opt::inline::hybrid", "std::opt::hybrid"},
    {"std::fastvm::jit::dump::asm", "std::fastvm::jit_asm"},
    {"std::fastvm::jit::dump::bin", "std::fastvm::jit_bin"},
    {"std::fastvm::jit::dump::rmir", "std::fastvm::jit_rmir"},
    {"std::fastvm::jit::dump::mir", "std::fastvm::jit_mir"},
    {"std::inline::arm", "std::opt::arm"},
    {"std::inline::hybrid", "std::opt::hybrid"},
    // Standard scheduler aliases
    {"std::default", "std::nvm"},
    {"std::linear", "std::nvm"},
    {"std::parallel", "std::taskflow"},
    {"std::tf", "std::taskflow"},

    // Common VM aliases
    {"std::lnr", "std::fvm"},
    {"std::prl", "std::taskflow"},
    {"std::fastvm", "std::fvm"},
    {"std::jit", "std::fastvm::jit"},
    {"std::nodevm", "std::nvm"},
    // Common translation and dump aliases
    // Names whose leaf is a keyword of the language: the declaration uses a name that can be
    // written, and the old spelling keeps resolving to it.
    {"std::null", "std::nop"},
    {"std::type::array", "std::arraytype"},
    {"std::type::identity", "std::identitytype"},
    {"std::cxx", "std::cpp"},
    {"std::cppmod", "std::cpp::module_file"},
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
    {"std::bin", "std::fastvm::jit_bin"},
    {"std::asm", "std::fastvm::jit_asm"},
    {"std::rmir", "std::fastvm::jit_rmir"},
    {"std::mir", "std::fastvm::jit_mir"},
    {"std::tfg", "std::tfdump"},
    {"std::prof", "std::profile"},
};
struct PassImplementationRegistry {
    std::mutex mutex;
    std::map<std::string, PassDescriptor> passes; // ordered for the "available passes" listing
};

PassImplementationRegistry &passImplementations() {
    static PassImplementationRegistry registry;
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

/// Registers `test::declared`, the test-only pass whose parameters are a declaration and not a
/// list in this file (defined with the pass itself, below).
void registerDeclaredParamsTestPass();

/// Binds every kernel of the core to its declaration. Called on first resolution, so a run that
/// never looks a pass up pays nothing; the registry it fills holds implementations only.
void ensureCorePasses() {
    static std::once_flag once;
    std::call_once(once, [] {
        registerCoreKernels();
        registerDeclaredParamsTestPass();
    });
}

/// The implementation bound to the function `module::function`, whichever side of the core it
/// belongs to. The name, documentation and parameters are the declaration's; this answers only
/// which function implements it.
std::optional<PassDescriptor> findImplementation(const std::string &path) {
    ensureCorePasses();
    auto &registry = passImplementations();
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
    auto &registry = passImplementations();
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
    // The core's namespace belongs to the core. A bare alias may not shadow a core declaration
    // either: resolution tries the written name as a declaration before any alias.
    if (alias.starts_with("std::") || passAliases.contains(alias)) {
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
/// pipeline, so naming one is the whole switch (removed the variable that used to gate
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
        TEST_PASS(kTagsTestPass, TagsTestPass),
        TEST_PASS(kImplBodyTestPass, ImplBodyTestPass),
        TEST_PASS(kChunksTestPass, ChunksTestPass),
        TEST_PASS(kRetypeTrivialTestPass, RetypeTrivialTestPass),
        TEST_PASS(kRetypeEnumTestPass, RetypeEnumTestPass),
        TEST_PASS(kEdgesCycleTestPass, EdgesCycleTestPass),
        TEST_PASS(kEdgesTypeTestPass, EdgesTypeTestPass),
        TEST_PASS(kEdgesOkTestPass, EdgesOkTestPass),
        TEST_PASS(kCommitConflictTestPass, CommitConflictTestPass),
        TEST_PASS(kKeepFnTestPass, KeepFnTestPass),
        TEST_PASS(kContextLifetimeTestPass, ContextLifetimeTestPass),
        TEST_PASS(kContextsTestPass, ContextsTestPass),
        TEST_PASS(kDerivativesTestPass, DerivativesTestPass),
        TEST_PASS(kCommitMergeTestPass, CommitMergeTestPass),
        TEST_PASS(kEdgesOrderOkTestPass, EdgesOrderOkTestPass),
        TEST_PASS(kEdgesOrderPureTestPass, EdgesOrderPureTestPass),
        TEST_PASS(kEdgesOrderScheduleTestPass, EdgesOrderScheduleTestPass),
        TEST_PASS(kEdgesOrderSpliceTestPass, EdgesOrderSpliceTestPass),
        TEST_PASS(kEdgesOrderReplaceTestPass, EdgesOrderReplaceTestPass),
        TEST_PASS(kEdgesSyncGateTestPass, EdgesSyncGateTestPass),
        TEST_PASS(kEdgesSyncForkTestPass, EdgesSyncForkTestPass),
        TEST_PASS(kEdgesSyncForkGateTestPass, EdgesSyncForkGateTestPass),
        TEST_PASS(kEdgesSyncCycleTestPass, EdgesSyncCycleTestPass),
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
        TEST_PASS(kSaturatePropsTestPass, SaturatePropsTestPass),
        TEST_PASS(kRegionsTestPass, RegionsTestPass),
        TEST_PASS(kArmLoopRegionsTestPass, ArmLoopRegionsTestPass),
        TEST_PASS(kLoopInputsTestPass, LoopInputsTestPass),
        TEST_PASS(kExecPlanTestPass, ExecPlanTestPass),
        TEST_PASS(kExhaustPermSpaceTestPass, ExhaustPermSpaceTestPass),
        TEST_PASS(kThrowBadAllocTestPass, ThrowBadAllocTestPass),
        TEST_PASS(kLoopCollectTestPass, LoopCollectTestPass),
        TEST_PASS(kLoopCollectSummaryTestPass, LoopCollectSummaryTestPass),
        TEST_PASS(kFactStoreTestPass, FactStoreTestPass),
        TEST_PASS(kFactLoadRulesTestPass, FactLoadRulesTestPass),
        TEST_PASS(kFactSeedTestPass, FactSeedTestPass),
        TEST_PASS(kFactCheckTestPass, FactCheckTestPass),
        TEST_PASS(kFactPersistTestPass, FactPersistTestPass),
    };
#undef TEST_PASS
    return table;
}

/// A test-only pass whose call can be read back: it prints every parameter the declaration
/// names, the value it was given, whether that value was written, and how many default
/// expressions have been evaluated in the process. A plan pins the declaration's count with
/// `declared=`, writes each name in one call, and compares the count of two calls that use the
/// defaults (equal: a default is evaluated at the declaration, not at each call).
class PassParamsTestPass final : public GraphIRPass {
  public:
    PassParamsTestPass(
        const context_ptr_t &ctx, const PassArgs &args, std::vector<PassParam> declared)
        : GraphIRPass(ctx), args_(args), declared_(std::move(declared)) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        os << "test::params";
        for (const auto &p : declared_) {
            os << ' ' << p.name << '=';
            switch (p.type) {
            case PassParamType::Bool:
                os << (args_.getBool(p.name) ? "true" : "false");
                break;
            case PassParamType::Int:
                os << args_.getInt(p.name);
                break;
            case PassParamType::Float:
                os << std::format("{}", args_.getFloat(p.name));
                break;
            case PassParamType::String:
                os << args_.getString(p.name);
                break;
            case PassParamType::IntArray:
                // Not declared by this pass (its list is the registration's own); the case is here
                // so the switch covers every type a parameter may have.
                os << args_.text(p.name);
                break;
            }
            os << (args_.given(p.name) ? " (given)" : " (default)");
        }
        os << std::format(" declared={}\n", declared_.size());
        return program;
    }

  private:
    PassArgs args_;
    std::vector<PassParam> declared_;
};

const PassDescriptor &passParamsTestDescriptor() {
    static const PassDescriptor descriptor = [] {
        PassDescriptor d;
        d.params = {
            {"flag", PassParamType::Bool, false, "a parameter that must be given", true},
            {"n", PassParamType::Int, int64_t{5}, "an int whose default is a constant expression", false},
            {"ratio", PassParamType::Float, 1.5, "a float", false},
            {"label", PassParamType::String, std::string("x"), "a string", false},
            {"count", PassParamType::Int, int64_t{7}, "another int", false},
        };
        d.make = [declared = d.params](const context_ptr_t &ctx, const PassArgs &args) {
            return std::make_unique<PassParamsTestPass>(ctx, args, declared);
        };
        return d;
    }();
    return descriptor;
}

/// A test-only pass whose parameters are a declaration and not a list in C++: it has none of its
/// own, so the names, types and defaults the pipeline binds are exactly the ones a `.cml` on the
/// module search path writes for `test::declared` (`test.cml` in a case's own directory, `-I .`).
/// It prints every parameter the declaration named, the value the declared type read from the
/// word, and whether that value was written — which is what a case that checks how a type is read
/// compares against. A declaration the pipeline cannot read (a type it has no value for, a
/// default that does not fit it) is refused while the pass is resolved, so nothing here ever asks
/// for a parameter by a type the declaration did not write.
class DeclaredParamsTestPass final : public GraphIRPass {
  public:
    DeclaredParamsTestPass(const context_ptr_t &ctx, const PassArgs &args)
        : GraphIRPass(ctx), args_(args) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        const std::vector<std::string> names = args_.names();
        os << "test::declared";
        for (const auto &name : names) {
            os << ' ' << name << '=' << args_.text(name)
               << (args_.given(name) ? " (given)" : " (default)");
        }
        os << std::format(" declared={}\n", names.size());
        return program;
    }

  private:
    PassArgs args_;
};

/// Registers `test::declared`: in no default pipeline, so naming it is the whole switch, and the
/// declaration it reads is the one its `.cml` writes (there is no parameter list here to fall
/// back to, which is the point).
void registerDeclaredParamsTestPass() {
    PassDescriptor descriptor;
    descriptor.make = [](const context_ptr_t &ctx, const PassArgs &args) {
        return std::make_unique<DeclaredParamsTestPass>(ctx, args);
    };
    registerModulePass("test::declared", std::move(descriptor));
}

/// A pass a name resolves to: how to make it and the parameters its declaration gives. The
/// declaration is the standard library's; a pass written in Camel's is its signature's `with`
/// list.
struct FoundPass {
    PassDescriptor descriptor;
    std::vector<PassParam> params;
    /// A declaration that exists but whose applicability condition does not hold.
    std::string inapplicable;
};

/// Resolves a pass name to its declaration and implementation. Every pass is resolved the same
/// way, by the module it belongs to and the name of the function its declaration gives: the
/// declaration supplies the name, documentation, parameters and applicability; the registry
/// supplies only the kernel. `ctx` locates the standard library and the pass modules; a name
/// without one is a name the pipeline cannot resolve.
std::optional<FoundPass> findDescriptor(const context_ptr_t &ctx, const std::string &name) {
    // The name as written is tried first, so a module alias can never shadow a declaration.
    const auto candidateFor = [&](const std::string &written) -> std::optional<FoundPass> {
        std::string module;
        std::string function;
        if (written.rfind("std::", 0) == 0) {
            module   = "std";
            function = written.substr(5);
        } else if (const size_t separator = written.find("::"); separator != std::string::npos) {
            module   = written.substr(0, separator);
            function = written.substr(separator + 2);
        } else {
            // A bare name is the core's: `camel m.cml opt` is `std::opt`.
            module   = "std";
            function = written;
        }
        if (function.empty()) {
            return std::nullopt;
        }
        const std::string path = module + "::" + function;
        const auto implementation = findImplementation(path);
        const auto *declaration =
            camel::execute::detail::findPassDeclaration(module, function, ctx);
        FoundPass found;
        if (!declaration) {
            // A pass with no declaration in a `.cml` file: a test-only pass, or a pass written in
            // Camel, whose declaration is the `with` list of the function itself. Its parameters
            // are the registration's.
            if (!implementation) {
                return std::nullopt;
            }
            found.descriptor = *implementation;
            found.params     = implementation->params;
            return found;
        }
        found.params = declaration->params;
        if (!passDeclarationApplies(*declaration, found.inapplicable)) {
            found.inapplicable = path + ": " + found.inapplicable;
            return found;
        }
        // A declaration with no kernel is a pass the language declares and nothing implements;
        // the unknown-pass answer is the right one, and the guard keeps the two in step.
        if (!implementation) {
            return std::nullopt;
        }
        found.descriptor = *implementation;
        return found;
    };
    if (auto written = candidateFor(name)) {
        return written;
    }
    const std::string resolved = resolveAlias(name);
    if (resolved != name) {
        return candidateFor(resolved);
    }
    return std::nullopt;
}

/// Test-only passes: they exist only to be named by a test's pipeline and are in no default
/// pipeline, so naming one is the whole switch (removed the variable that used to gate
/// them).
std::optional<FoundPass> findTestPass(const std::string &name) {
    if (name == "test::params") {
        FoundPass found;
        found.descriptor = passParamsTestDescriptor();
        found.params     = found.descriptor.params;
        return found;
    }
    for (const auto &[testName, factory] : testPasses()) {
        if (name == testName) {
            FoundPass found;
            found.descriptor = passWithoutParameters(factory);
            return found;
        }
    }
    return std::nullopt;
}

struct ResolvedPass {
    PassFactory factory; // the pass with its parameters bound; null if not usable
    bool known = false;  // the name is a pass
    /// A parameter the call wrote wrongly: unknown, written twice, of the wrong type, or a
    /// required one left out.
    std::optional<camel::execute::detail::ParamProblem> paramProblem;
    /// A pass module that does not compile: its first error, as a secondary location.
    std::optional<std::string> problem;
    std::optional<DiagLabel> errorLocation;
    /// A `<module>::<pass>` whose module is there and compiles but provides no such pass: the
    /// module, the name, what the module does provide, and whether it declares the name without
    /// exporting it.
    struct MissingPass {
        std::string module;
        std::string function;
        std::vector<std::string> provided;
        bool declaresUnexported = false;
    };
    std::optional<MissingPass> missingPass;
};

/// Resolves one specification: the pass name and its `key=value` words. Prints nothing.
ResolvedPass resolvePass(const std::string &spec, const context_ptr_t &ctx) {
    ResolvedPass result;
    camel::execute::detail::ParsedSpec parsed;
    if (auto problem = camel::execute::detail::parseSpec(spec, parsed)) {
        result.paramProblem = std::move(problem);
        return result;
    }
    // A core pass exists only as the declaration in stdlib/std.cml plus its kernel. When the
    // declaration cannot be read, the kernel must not run on defaults invented elsewhere: the
    // failure names the module and the files the pipeline looked at.
    if (ctx) {
        const std::string resolvedName = resolveAlias(parsed.name);
        if (resolvedName.rfind("std::", 0) == 0) {
            const std::string bare = resolvedName.substr(5);
            if (findImplementation(resolvedName) &&
                camel::execute::detail::findPassDeclaration("std", bare, ctx) == nullptr) {
                result.known = true;
                const std::string path =
                    camel::execute::detail::moduleDeclarationPath("std", ctx);
                if (!path.empty()) {
                    result.problem =
                        std::format("module 'std' declares no pass '{}' in {}", bare, path);
                } else {
                    const auto candidates =
                        camel::execute::detail::moduleDeclarationCandidates("std", ctx);
                    std::string looked;
                    for (const auto &candidate : candidates) {
                        looked += (looked.empty() ? "" : ", ") + candidate;
                    }
                    result.problem = std::format(
                        "the pass declarations of module 'std' are not found: looked for std.cml "
                        "at {}",
                        looked.empty() ? "no module search path" : looked);
                }
                return result;
            }
        }
    }
    const auto descriptor = [&]() -> std::optional<FoundPass> {
        if (auto declared = findDescriptor(ctx, parsed.name)) {
            return declared;
        }
        return findTestPass(resolveAlias(parsed.name));
    }();
    if (!descriptor) {
        return result;
    }
    result.known = true;
    if (!descriptor->inapplicable.empty()) {
        result.problem = descriptor->inapplicable;
        return result;
    }
    PassArgs args;
    if (auto problem =
            camel::execute::detail::bindArgs(parsed.name, descriptor->params, parsed, args)) {
        result.paramProblem = std::move(problem);
        return result;
    }
    result.factory =
        [make = descriptor->descriptor.make, args = std::move(args)](const context_ptr_t &ctx) {
            return make(ctx, args);
        };
    return result;
}

/// A parameter written wrongly at a pass call: the language's own diagnostics for a call, at the
/// place in the pipeline the pass is written (`<pipeline>:<line>:<column>`), in the semantic
/// stage. A specification that is not a call at all (a positional value, a missing `)`, an
/// argument that is not a constant) is still `InvalidPassParameter`, now with the same position.
Diagnostic parameterDiagnostic(
    const std::string &spec, const camel::execute::detail::ParamProblem &problem, size_t line) {
    using Kind = camel::execute::detail::ParamProblemKind;
    Diagnostic diag;
    switch (problem.kind) {
    case Kind::Syntax:
        diag = DiagnosticBuilder::of(RuntimeDiag::InvalidPassParameter).commit(spec, problem.text);
        diag.stage = "typecheck";
        break;
    case Kind::Unknown:
        diag = DiagnosticBuilder::of(SemanticDiag::ParamArgumentUnknown)
                   .commit(problem.name, specName(spec));
        break;
    case Kind::Duplicate:
        diag = DiagnosticBuilder::of(SemanticDiag::ParamArgumentDuplicate)
                   .commit(problem.name, specName(spec));
        break;
    case Kind::WrongType:
        diag = DiagnosticBuilder::of(SemanticDiag::ParamArgumentWrongType)
                   .commit(problem.name, specName(spec), problem.found, problem.expected);
        break;
    case Kind::Missing:
        diag = DiagnosticBuilder::of(SemanticDiag::ParamArgumentMissing)
                   .commit(problem.name, specName(spec));
        break;
    }
    // The whole specification for a problem that has no narrower place (a required parameter
    // left out, a specification that is not a call).
    const size_t length =
        problem.length > 0 ? problem.length : std::max<size_t>(spec.size(), 1);
    diag.range      = CharRange{{line, problem.column}, {line, problem.column + length}};
    diag.modulePath = "<pipeline>";
    diag.moduleName = "pipeline";
    return diag;
}

/// The `help` of a pass specification whose module did not load, in place of the paragraph about how
/// a pass specification is written (rule, "pass-unavailable-diagnostics" 1). The
/// user did not write the name wrongly: the module the name needs is not there to be loaded, and
/// the diagnostic's message already carries the loader's own first reason. What the help owes the
/// reader is that fact, said plainly, so that the paragraph about `key=value` words — the answer to
/// a different problem — is not what they act on.
///
/// `module` is the module the pass name resolved to; `reason` is the same text the message carries.
std::string moduleLoadFailureHelp(const std::string &module, const std::string &reason) {
    return std::format(
        "The module '{}' itself did not load, so none of its passes can be named: {}. Nothing on "
        "this command line is a parameter problem. Fix what the loader reported about the module "
        "first — its file, its build, or the runtime libraries it needs — and run the pipeline "
        "again.",
        module, reason);
}

/// Whether a pass specification's problem is that the module it names did not load, as opposed to
/// the pass name or its parameters being wrong. `problem` is the text `DefinitionStore` builds for
/// a name whose module cannot be used: the module is not there (an unknown pass, not this), it is
/// found and the loader refuses it, or it is found and does not compile.
bool isModuleLoadFailure(const std::string &problem) {
    return problem.find("cannot be loaded") != std::string::npos ||
           problem.find("does not compile") != std::string::npos;
}

/// The module a `<module>::<pass>` specification names, or empty when it names none.
std::string moduleOf(const std::string &spec) {
    const std::string name = specName(spec);
    const size_t separator = name.find("::");
    if (separator == std::string::npos) {
        return "";
    }
    return name.substr(0, separator);
}

Diagnostic invalidParameterDiagnostic(
    const std::string &spec, const std::string &problem,
    const std::optional<DiagLabel> &errorLocation = std::nullopt) {
    setPipelineStage("pass:" + specName(spec));
    auto diag = DiagnosticBuilder::of(RuntimeDiag::InvalidPassParameter).commit(spec, problem);
    if (errorLocation) {
        diag.related.push_back(*errorLocation);
    }
    // A module that did not load gets its own help: the paragraph about how to write parameters is
    // the answer to a problem this run does not have.
    if (isModuleLoadFailure(problem)) {
        const std::string module = moduleOf(spec);
        if (!module.empty()) {
            diag.suggestion = moduleLoadFailureHelp(module, problem);
        }
    }
    diag.stage = "pass:" + specName(spec);
    setPipelineStage("");
    return diag;
}

void printPass(
    std::ostream &help, const std::string &path, const std::string &doc,
    const std::vector<PassParam> &params) {
    help << std::format("  {}\n", path);
    if (!doc.empty()) {
        help << std::format("      {}\n", doc);
    }
    if (!params.empty()) {
        help << std::format("      {}\n", camel::execute::detail::describeParams(params));
    }
}

/// Lists the available passes on stderr. The listing is help text for the command line:
/// stdout stays reserved for program output and the diagnostics array.
void listAvailablePasses(const std::string &name, const context_ptr_t &ctx) {
    ensureCorePasses();
    std::ostream &help = std::cerr;
    help << std::format("Pass <{}> not found, available passes are:\n", name);
    for (const auto &declaration :
         camel::execute::detail::passDeclarations("std", ctx)) {
        printPass(
            help, "std::" + declaration.name, declaration.doc, declaration.params);
    }
    {
        auto &registry = passImplementations();
        std::lock_guard lock(registry.mutex);
        bool listed = false;
        for (const auto &[path, descriptor] : registry.passes) {
            // The core's kernels are listed from their declarations above; this section is the
            // passes a module contributed. A test-only pass is not a module's: the section below
            // lists it.
            if (path.rfind("std::", 0) == 0 || path.rfind("test::", 0) == 0) {
                continue;
            }
            if (!listed) {
                help << std::format("Passes contributed by loaded modules:\n");
                listed = true;
            }
            const size_t separator     = path.find("::");
            const std::string module   = path.substr(0, separator);
            const std::string function = path.substr(separator + 2);
            const auto declaration = camel::execute::detail::findPassDeclaration(
                module, function, ctx);
            printPass(
                help,
                path,
                declaration ? declaration->doc : std::string(),
                declaration ? declaration->params : descriptor.params);
        }
    }
    help << std::format("Passes that exist for the tests only:\n");
    printPass(help, "test::params", "a test pass whose parameters exercise every type", {});
    for (const auto &[testName, factory] : testPasses()) {
        (void)factory;
        printPass(help, testName, {}, {});
    }
    {
        // `test::declared` is a test pass whose parameters are a declaration, not a list here:
        // where `test.cml` is on the module search path, the listing shows the types it declares.
        const auto declaration =
            camel::execute::detail::findPassDeclaration("test", "declared", ctx);
        printPass(
            help,
            "test::declared",
            declaration ? declaration->doc
                        : "a test pass whose parameters are the declaration `test.cml` writes",
            declaration ? declaration->params : std::vector<PassParam>{});
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
    help << "A pass is written `name`, followed by its parameters as `key=value` words; a value is "
            "the constant the parameter's declared type reads (a bool, int, float, string or "
            "`int[]` constant, or `null` where the declaration writes `T?`).\n";
}

/// The passes of a module, as the sentence of a diagnostic writes them: "no passes", "the pass
/// 'x'", "the passes 'x', 'y'".
std::string providedPassesText(const std::vector<std::string> &passes) {
    if (passes.empty()) {
        return "no passes";
    }
    std::string text       = passes.size() == 1 ? "the pass " : "the passes ";
    std::string separator;
    for (const auto &pass : passes) {
        text += separator;
        text += std::format("'{}'", pass);
        separator = ", ";
    }
    return text;
}

/// The diagnostic for a pass name that does not resolve, told apart by what was found:
///   - a module that is there and cannot be used: its reason, chained to the module's own first
///     error as a secondary location when that error has a position, exactly as any other
///     refused pass specification;
///   - a module that is there and provides no such pass: the short answer, naming the module and
///     the passes it does provide, so that a name that is simply wrong does not read like a
///     compiler that lost the module;
///   - a name with no module behind it: unknown, followed by the list of passes that exist.
[[noreturn]] void
throwUnresolvedPass(const std::string &spec, const ResolvedPass &resolved, size_t line, const context_ptr_t &ctx) {
    if (resolved.paramProblem) {
        throw parameterDiagnostic(spec, *resolved.paramProblem, line);
    }
    if (resolved.problem) {
        throw invalidParameterDiagnostic(spec, *resolved.problem, resolved.errorLocation);
    }
    if (resolved.missingPass) {
        const auto &missing        = *resolved.missingPass;
        const std::string provided = providedPassesText(missing.provided);
        setPipelineStage("pass:" + specName(spec));
        const RuntimeDiag code = missing.declaresUnexported
                                     ? RuntimeDiag::PassNotExportedByModule
                                     : RuntimeDiag::PassNotProvidedByModule;
        auto diag = DiagnosticBuilder::of(code).commit(missing.module, missing.function, provided);
        diag.stage = "pass:" + specName(spec);
        setPipelineStage("");
        throw diag;
    }
    listAvailablePasses(spec, ctx);
    setPipelineStage("pass:" + specName(spec));
    auto unknown  = DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(spec);
    unknown.stage = "pass:" + specName(spec);
    setPipelineStage("");
    throw unknown;
}

} // namespace

PassFactory findPassFactory(const std::string &name, const context_ptr_t &ctx) {
    auto resolved = resolvePass(name, ctx);
    if (resolved.paramProblem) {
        throw parameterDiagnostic(name, *resolved.paramProblem, 0);
    }
    if (resolved.problem) {
        throw invalidParameterDiagnostic(name, *resolved.problem);
    }
    if (!resolved.factory) {
        listAvailablePasses(name, ctx);
    }
    return resolved.factory;
}

/// Resolves `spec`, importing the module that contributes a `<module>::<pass>` name when the
/// program has not imported it yet: a pass list may name a pass of a module the program does
/// not import, and loading the module registers the pass. A name that stays unknown resolves
/// to no factory, and a bad parameter list keeps its problem.
ResolvedPass resolvePassWithModule(const std::string &spec, const context_ptr_t &ctx) {
    ResolvedPass resolved = resolvePass(spec, ctx);
    if (resolved.paramProblem || resolved.problem || resolved.factory || !ctx) {
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
    resolved = resolvePass(spec, ctx);
    if (resolved.paramProblem || resolved.problem || resolved.factory) {
        return resolved;
    }
    // A function written in Camel: the module is a library of its own, compiled now.
    std::string problem;
    std::optional<DiagLabel> errorLocation;
    camel::execute::MissingCamelPass missing;
    camel::execute::registerCamelPass(
        ctx, name.substr(0, sep), name.substr(sep + 2), problem, &errorLocation, &missing);
    resolved = resolvePass(spec, ctx);
    if (!resolved.factory) {
        if (!problem.empty()) {
            resolved.problem       = problem;
            resolved.errorLocation = errorLocation;
        } else if (missing.moduleHasNoSuchPass) {
            resolved.missingPass = ResolvedPass::MissingPass{
                name.substr(0, sep), name.substr(sep + 2), std::move(missing.provided),
                missing.declaresUnexported};
        }
    }
    return resolved;
}

namespace {

/// The graph id -> revision of every graph a program holds: what `programContentChanged`
/// compares. Read out before a pass runs rather than looked at afterwards, because a pass of the
/// core is another registered pass's caller (`std::opt::algebra` runs the rules library's
/// `rules::simplify`, `std::opt::saturate` its `opt::saturate`), and that inner pass
/// commits its result and releases the program the outer one was given
/// (`Context::settleRuntimeGraphs`). The old program is not read after `pass->apply` returns.
std::unordered_map<uint32_t, uint64_t>
graphRevisionsOf(const camel::runtime::Program *program) {
    std::unordered_map<uint32_t, uint64_t> revisions;
    for (camel::runtime::GCGraph *graph : program->graphs()) {
        if (graph != nullptr) {
            revisions.emplace(camel::runtime::toValue(graph->id()), graph->revision());
        }
    }
    return revisions;
}

/// Whether `after` holds graphs of content different from `before`, whose graph revisions are
/// `beforeRevisions`: a graph's revision changes only when a commit encoded different content for
/// it (graph.h), and a graph one of the two holds and the other does not counts. The program
/// objects themselves differ after every commit, so pointer inequality would call every pass a
/// change.
bool programContentChanged(
    const camel::runtime::Program *before,
    const std::unordered_map<uint32_t, uint64_t> &beforeRevisions,
    const camel::runtime::Program *after) {
    if (after == nullptr) {
        return true;
    }
    if (after == before) {
        return false;
    }
    for (camel::runtime::GCGraph *graph : after->graphs()) {
        if (graph == nullptr) {
            continue;
        }
        const auto known = beforeRevisions.find(camel::runtime::toValue(graph->id()));
        if (known == beforeRevisions.end() || known->second != graph->revision()) {
            return true;
        }
    }
    for (const auto &entry : beforeRevisions) {
        if (after->graph(static_cast<camel::runtime::GraphId>(entry.first)) == nullptr) {
            return true;
        }
    }
    return false;
}

/// Whether the node runs an effect. This is the tree's one judgement of it, copied from the
/// `"effect"` exclusion of modules/tiles/src/graph_regions.cpp: an operator that is not pure, or
/// one whose running is visible outside the program. A second reading of the operator table here
/// would be a second model to keep in step with that one.
bool nodeHasEffect(const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Oper) {
        return false;
    }
    const auto &table            = camel::core::OperatorTable::instance();
    const camel::core::OperId op = graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)->oper;
    return !table.isPure(op) || table.hasExternalEffect(op);
}

/// The call whose body a node was copied into, when a pass inlined one around it: the source origin
/// of the innermost inline region it sits in, 0 when it sits where the program wrote it. The region
/// table is part of the frozen graph, so this reads the same before and after a pass, which is what
/// the effect comparison needs; the origin of the copy alone does not tell it from the node it was
/// copied from.
uint64_t inlinedCopySite(const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    using camel::runtime::GCRegionKind;
    uint32_t region = graph.nodeRegion(ref);
    for (uint32_t depth = 0; depth < 64 && region != 0; ++depth) {
        const camel::runtime::GCRegion *current = graph.region(region);
        if (current == nullptr) {
            break;
        }
        if (current->kind == GCRegionKind::Inline &&
            current->callSite != camel::runtime::kInvalidNodeRef) {
            return graph.nodeOrigin(current->callSite);
        }
        region = current->parent;
    }
    return 0;
}

/// One effect of a program, at the place the program puts it: the source origin and the operator it
/// runs (what names it across a pass), the graph it is in, its position in that graph's order, and
/// the ambient resources it touches.
struct EffectSite {
    uint64_t origin      = 0;
    std::string_view uri = {};
    size_t graph         = 0; // the GraphId value of the graph it runs in
    size_t position      = 0; // its index in that graph's order
    /// Which copy of that origin it is: 0 for the node as the program wrote it, and the source
    /// origin of the call whose body it was copied into when a pass inlined one. A pass that copies
    /// a body gives the copy the origin of the node it copied, so the origin alone names both the
    /// copy and the node it came from; this is what tells them apart across the boundary, and it is
    /// read from the region the node sits in, which every frozen graph carries.
    uint64_t copySite    = 0;
    /// The node itself, and its place in its own graph's order: what the consistency check below
    /// reads to be sure a place it names is a place that holds the node.
    camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
    std::vector<std::string> resources; // the ambient resources it names, sorted and deduplicated
};

/// One graph of a program in one topological order, with the users of each node: the order says
/// where a node sits, and the users say whether the graph makes one node wait for another at all.
struct GraphOrder {
    size_t id = 0; // the GraphId value
    std::vector<camel::runtime::gc_node_ref_t> nodes;
    std::vector<std::vector<camel::runtime::gc_node_ref_t>> users;
    /// Where each node stands in `nodes`, by node reference, kNotOrdered when it is not in it: what
    /// asks for the place of the node that owns a graph.
    std::vector<uint32_t> places;
};
inline constexpr uint32_t kNotOrdered = UINT32_MAX;

/// The effects of one program, and the order of each of its graphs.
struct ProgramEffects {
    std::vector<EffectSite> sites;
    std::vector<GraphOrder> graphs;
    std::unordered_map<size_t, size_t> graphAt; // GraphId value -> index in `graphs`
    /// For each graph a body was inlined into: the graph that holds it and the node there that runs
    /// it (the call the inline region replaced). A nested graph runs at the place of that node.
    std::unordered_map<size_t, std::pair<size_t, camel::runtime::gc_node_ref_t>> ownerOf;
};

/// `graph` in one topological order: every node after every node its edges make it wait for (its
/// data inputs, its control inputs, its sync and order predecessors), each node exactly once
/// however many edges name it. One ordering pass per graph, not a reachability test per pair of
/// effects: the program that made a pairwise test expensive (264 effects, 20 s) costs a graph order
/// here. Nodes a rewrite removed are not in it, and neither is anything the order cannot place (the
/// seal check reports a cycle, and this check must not fail on it a second time).
GraphOrder orderOfGraph(const GCGraph &graph) {
    using camel::runtime::gc_node_ref_t;
    GraphOrder result;
    result.id          = static_cast<size_t>(camel::runtime::toValue(graph.id()));
    const size_t count = graph.nodeCount();
    result.users.assign(count, {});
    std::vector<uint32_t> indegree(count, 0);
    std::vector<uint32_t> seen(count, 0);
    uint32_t stamp = 0;
    size_t live    = 0;
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t user = it.ref();
        ++live;
        ++stamp;
        const auto visit = [&](gc_node_ref_t pred) {
            if (pred == user || !graph.containsNodeRef(pred)) {
                return;
            }
            const camel::runtime::GCNode *record = graph.node(pred);
            if (record == nullptr || record->kind == camel::runtime::GCNodeKind::Dead) {
                return;
            }
            if (seen[pred] == stamp) {
                return; // one pair counted once, whatever runs of edges name it
            }
            seen[pred] = stamp;
            result.users[pred].push_back(user);
            ++indegree[user];
        };
        for (gc_node_ref_t pred : graph.normInputsOf(user)) {
            visit(pred);
        }
        for (gc_node_ref_t pred : graph.withInputsOf(user)) {
            visit(pred);
        }
        for (gc_node_ref_t pred : graph.ctrlInputsOf(user)) {
            visit(pred);
        }
        for (const camel::runtime::GCGraphSyncEdge &edge : graph.syncPredsOf(user)) {
            visit(edge.pred);
        }
        for (const camel::runtime::GCGraphOrderEdge &edge : graph.orderPredsOf(user)) {
            visit(edge.pred);
        }
    }
    result.nodes.reserve(count);
    std::vector<gc_node_ref_t> ready;
    ready.reserve(count);
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        if (indegree[it.ref()] == 0) {
            ready.push_back(it.ref());
        }
    }
    for (size_t next = 0; next < ready.size(); ++next) {
        const gc_node_ref_t node = ready[next];
        result.nodes.push_back(node);
        for (gc_node_ref_t user : result.users[node]) {
            if (--indegree[user] == 0) {
                ready.push_back(user);
            }
        }
    }
    if (result.nodes.size() != live) {
        std::vector<uint8_t> placed(count, 0);
        for (gc_node_ref_t node : result.nodes) {
            placed[node] = 1;
        }
        for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
            if (placed[it.ref()] == 0) {
                result.nodes.push_back(it.ref());
            }
        }
    }
    result.places.assign(count, kNotOrdered);
    for (size_t position = 0; position < result.nodes.size(); ++position) {
        result.places[result.nodes[position]] = static_cast<uint32_t>(position);
    }
    return result;
}

/// The effects of `program`: every effect node of every graph of it, each at its position in one
/// topological order of its graph and each with the ambient resources it names.
ProgramEffects collectEffects(const camel::runtime::Program &program) {
    using camel::runtime::gc_node_ref_t;
    ProgramEffects effects;
    for (GCGraph *graph : program.graphs()) {
        effects.graphAt[static_cast<size_t>(camel::runtime::toValue(graph->id()))] =
            effects.graphs.size();
        effects.graphs.push_back(orderOfGraph(*graph));
        const GraphOrder &order = effects.graphs.back();
        for (uint32_t regionId = 1; regionId < graph->regionCount(); ++regionId) {
            const camel::runtime::GCRegion *region = graph->region(regionId);
            if (region == nullptr || region->kind != camel::runtime::GCRegionKind::Inline ||
                region->inlineOf == camel::runtime::kInvalidGraphId ||
                region->callSite == camel::runtime::kInvalidNodeRef) {
                continue;
            }
            effects.ownerOf[static_cast<size_t>(camel::runtime::toValue(region->inlineOf))] = {
                order.id, region->callSite};
        }
        for (size_t position = 0; position < order.nodes.size(); ++position) {
            const gc_node_ref_t ref = order.nodes[position];
            if (!nodeHasEffect(*graph, ref)) {
                continue;
            }
            EffectSite site;
            site.origin   = graph->nodeOrigin(ref);
            site.uri      = graph->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
            site.graph    = order.id;
            site.position = position;
            site.copySite = inlinedCopySite(*graph, ref);
            site.node     = ref;
            const camel::runtime::NodeResources resources =
                camel::runtime::resourcesOf(program, *graph, ref);
            for (const camel::runtime::NodeAccess &access : resources.accesses) {
                if (access.resource.cls != camel::core::sem::ResourceClass::Ambient) {
                    continue; // a cell, an object or a handle: its identity is written nowhere, so
                              // two of them are one resource only inside one graph, which is not
                              // what two graphs are compared by
                }
                site.resources.push_back(access.resource.name);
            }
            std::sort(site.resources.begin(), site.resources.end());
            site.resources.erase(
                std::unique(site.resources.begin(), site.resources.end()),
                site.resources.end());
            effects.sites.push_back(std::move(site));
        }
    }
    return effects;
}

/// The name two effects are matched by across a pass: the node they came from, which a pass keeps
/// (a copy it makes carries the origin of the node it copied too).
std::string effectKey(const EffectSite &site) {
    std::string key = std::to_string(site.origin);
    key += '\x1f';
    key.append(std::to_string(site.copySite));
    key += '\x1f';
    key.append(site.uri.data(), site.uri.size());
    return key;
}

/// The effects of the two programs, matched: one origin and operator that the program has once is
/// the same effect after the pass. An effect the pass added or removed has no partner, and takes
/// part in no pair. An effect the program has more than once is left out of every pair: a pass that
/// copies a body (inlining does) leaves several nodes carrying one origin, and which copy a node of
/// the next program is cannot be read off the origin, so an order between copies would be a guess.
std::vector<std::pair<size_t, size_t>> matchedEffects(
    const ProgramEffects &before, const ProgramEffects &after) {
    std::unordered_map<std::string, size_t> beforeOccurrences;
    std::unordered_map<std::string, size_t> beforeAt;
    beforeOccurrences.reserve(before.sites.size());
    for (size_t i = 0; i < before.sites.size(); ++i) {
        const std::string key = effectKey(before.sites[i]);
        ++beforeOccurrences[key];
        beforeAt[key] = i;
    }
    std::vector<uint8_t> afterSeen(before.sites.size(), 0);
    std::vector<std::pair<size_t, size_t>> pairs;
    for (size_t i = 0; i < after.sites.size(); ++i) {
        const std::string key = effectKey(after.sites[i]);
        const auto found      = beforeOccurrences.find(key);
        if (found == beforeOccurrences.end() || found->second != 1) {
            continue;
        }
        const size_t at = beforeAt[key];
        if (afterSeen[at] != 0) {
            continue; // the program has it more than once after the pass too
        }
        afterSeen[at] = 1;
        pairs.emplace_back(at, i);
    }
    return pairs;
}

/// The resource two effects meet on, empty when they meet on none: the one ambient resource both of
/// them name (design principles 3.3: two effects on different resources are not a pair, and two
/// ambient resources are the same exactly when their names are). An effect that names no resource
/// at all is not paired with one that names another: what it touches is written nowhere, so the
/// model cannot say that the two are one resource, and a check that guessed would report pairs the
/// program never ordered.
std::string sharedResourceOf(const EffectSite &a, const EffectSite &b) {
    for (const std::string &resource : a.resources) {
        if (std::find(b.resources.begin(), b.resources.end(), resource) != b.resources.end()) {
            return resource;
        }
    }
    return {};
}

/// Whether the graph itself makes `later` wait for `earlier`: a chain of edges from the one to the
/// other, its data inputs, its control inputs, its sync and order predecessors. Two effects are in
/// the order the program wrote them in only when such a chain exists: two effects with no chain
/// between them are placed by whichever order the graph was written out in, and a pass that writes
/// the graph out again may place them the other way without having reordered anything.
bool waitsFor(const ProgramEffects &effects, const EffectSite &earlier, const EffectSite &later) {
    const auto at = effects.graphAt.find(earlier.graph);
    if (at == effects.graphAt.end() || earlier.graph != later.graph) {
        return false;
    }
    const GraphOrder &order = effects.graphs[at->second];
    if (earlier.position >= order.nodes.size() || later.position >= order.nodes.size()) {
        return false;
    }
    const camel::runtime::gc_node_ref_t from = order.nodes[earlier.position];
    const camel::runtime::gc_node_ref_t to   = order.nodes[later.position];
    // A node reference is a position in the graph's node table, which is at least as wide as the
    // nodes its order holds: the marks are as wide as that table, not as the order.
    if (static_cast<size_t>(from) >= order.users.size() ||
        static_cast<size_t>(to) >= order.users.size()) {
        return false;
    }
    std::vector<uint8_t> seen(order.users.size(), 0);
    std::vector<camel::runtime::gc_node_ref_t> stack{from};
    seen[from] = 1;
    while (!stack.empty()) {
        const camel::runtime::gc_node_ref_t node = stack.back();
        stack.pop_back();
        for (camel::runtime::gc_node_ref_t user : order.users[node]) {
            if (user == to) {
                return true;
            }
            if (seen[user] != 0) {
                continue;
            }
            seen[user] = 1;
            stack.push_back(user);
        }
    }
    return false;
}

std::string describeEffect(const EffectSite &site) {
    if (site.copySite != 0) {
        return std::format(
            "'{}' at origin {} copied into the call at origin {} (graph {}, position {})",
            std::string(site.uri),
            site.origin,
            site.copySite,
            site.graph,
            site.position);
    }
    return std::format(
        "'{}' at origin {} (graph {}, position {})",
        std::string(site.uri),
        site.origin,
        site.graph,
        site.position);
}

std::string describeMoves(const EffectSite &a, const EffectSite &runsA, const EffectSite &b,
                          const EffectSite &runsB) {
    std::string moves;
    const auto appendMove = [&](const EffectSite &was, const EffectSite &is) {
        if (was.graph == is.graph) {
            return;
        }
        if (!moves.empty()) {
            moves += "; ";
        }
        moves += std::format(
            "'{}' at origin {} moved from graph {} to graph {}",
            std::string(is.uri),
            is.origin,
            was.graph,
            is.graph);
    };
    appendMove(a, runsA);
    appendMove(b, runsB);
    return moves.empty() ? "both stayed in their graphs" : moves;
}

/// Where the place of a graph is, when it is not the graph itself: the graph that carries it and the
/// place in that graph of the node that runs it. A graph a pass inlined into another one has its
/// place at the call it replaced, so an effect in it runs at that call, not wherever its own graph
/// would have been laid out.
struct GraphPlace {
    bool found      = false;
    size_t graph    = 0;
    size_t position = 0;
};

GraphPlace placeOfGraph(const ProgramEffects &effects, size_t graph) {
    const auto owner = effects.ownerOf.find(graph);
    if (owner == effects.ownerOf.end()) {
        return {};
    }
    const auto holder = effects.graphAt.find(owner->second.first);
    if (holder == effects.graphAt.end()) {
        return {};
    }
    const GraphOrder &order = effects.graphs[holder->second];
    const size_t ref        = owner->second.second;
    if (ref >= order.places.size() || order.places[ref] == kNotOrdered) {
        return {};
    }
    return {true, owner->second.first, order.places[ref]};
}

/// Whether the order the program wrote between two effects still holds: the one waits for the other
/// in the graph they share, or, when a pass left them in two graphs, through the calls that hold
/// those graphs. An effect of a nested graph runs at the place of the call that runs it, so the
/// question is asked of the outermost graph both of them reach, lifting the one whose graph is
/// nested one level at a time. Only pairs the comparison above flagged are asked: an effect whose
/// graph nothing holds (a program graph) stays where it is.
/// What one question of the order found: whether the one waits for the other, and, when it took a
/// lift to ask, the graph the answer was read in and the call that lifted them there.
struct OrderVerdict {
    bool ordered    = false;
    bool lifted     = false;
    size_t graph    = 0;
    uint64_t call   = 0; // source origin of the call the lift went through, 0 for none
};

/// Whether the place an effect is asked about is a place that holds a node: the effect's own graph
/// must have its node at the position named, and a lift must land on the node that owns the graph.
/// A place that does not is a bug in this check, not in the program, so it is caught here and not
/// reported against a pass; debug builds stop on it.
bool placeHoldsEffect(const ProgramEffects &effects, const EffectSite &site) {
    const auto graph = effects.graphAt.find(site.graph);
    if (graph == effects.graphAt.end()) {
        return false;
    }
    const GraphOrder &order = effects.graphs[graph->second];
    return site.position < order.nodes.size() && order.nodes[site.position] == site.node;
}

OrderVerdict orderedInProgram(const ProgramEffects &effects, EffectSite earlier, EffectSite later) {
    OrderVerdict verdict;
    for (int step = 0; step < 64; ++step) {
        if (earlier.graph == later.graph) {
            if (!placeHoldsEffect(effects, earlier) || !placeHoldsEffect(effects, later)) {
                return verdict; // the place does not hold the node: do not answer from it
            }
            verdict.ordered = waitsFor(effects, earlier, later);
            verdict.graph   = earlier.graph;
            return verdict;
        }
        const GraphPlace place = placeOfGraph(effects, later.graph);
        if (place.found) {
            const auto holder = effects.graphAt.find(place.graph);
            const GraphOrder &order =
                effects.graphs[holder->second];
            if (place.position >= order.nodes.size()) {
                return verdict;
            }
            verdict.lifted = true;
            later.node     = order.nodes[place.position];
            later.graph    = place.graph;
            later.position = place.position;
            continue;
        }
        const GraphPlace earlierPlace = placeOfGraph(effects, earlier.graph);
        if (!earlierPlace.found) {
            return verdict;
        }
        const auto holder = effects.graphAt.find(earlierPlace.graph);
        const GraphOrder &order = effects.graphs[holder->second];
        if (earlierPlace.position >= order.nodes.size()) {
            return verdict;
        }
        verdict.lifted   = true;
        earlier.node     = order.nodes[earlierPlace.position];
        earlier.graph    = earlierPlace.graph;
        earlier.position = earlierPlace.position;
    }
    return verdict;
}

/// The one effect-order violation of a pass, as the text its diagnostic carries; empty when every
/// pair of effects on one resource is still in the order the program wrote it in (design principles
/// 3.3). Two things are compared, and both are needed:
///
/// 1. The order the program wrote, read from the one topological order of the graph the two effects
///    shared before the pass, and the chain of edges that made the one wait for the other. A pair
///    the program did not put in an order is not a pair to break: two effects with no chain between
///    them are placed by whichever order the graph was written out in, and a pass that writes it out
///    again may place them the other way without having reordered anything.
/// 2. The graph each of them is in after the pass. A pass that pulls an effect's node into another
///    graph takes its order with it, and a check that only read the order of a graph sees nothing:
///    the node is still in the program, it is the graph it runs in that moved.
std::string effectOrderViolation(const ProgramEffects &before, const ProgramEffects &after) {
    const std::vector<std::pair<size_t, size_t>> pairs = matchedEffects(before, after);
    for (size_t i = 0; i < pairs.size(); ++i) {
        const EffectSite &first     = before.sites[pairs[i].first];
        const EffectSite &runsFirst = after.sites[pairs[i].second];
        for (size_t j = i + 1; j < pairs.size(); ++j) {
            const EffectSite &second     = before.sites[pairs[j].first];
            const EffectSite &runsSecond = after.sites[pairs[j].second];
            const std::string resource   = sharedResourceOf(first, second);
            if (resource.empty()) {
                continue;
            }
            const bool writtenFirst       = first.position < second.position;
            const EffectSite &earlier     = writtenFirst ? first : second;
            const EffectSite &later       = writtenFirst ? second : first;
            const EffectSite &runsEarlier = writtenFirst ? runsFirst : runsSecond;
            const EffectSite &runsLater   = writtenFirst ? runsSecond : runsFirst;
            const OrderVerdict written = orderedInProgram(before, earlier, later);
            if (!written.ordered) {
                continue; // no chain before the pass: the program did not put them in this order
            }
            const OrderVerdict kept = orderedInProgram(after, runsEarlier, runsLater);
            if (kept.ordered) {
                continue; // the program's order still holds, in one graph or through the calls
            }
            const std::string moves = describeMoves(earlier, runsEarlier, later, runsLater);
            const char *writtenIn   = written.lifted ? " (read through the call that runs it)"
                                                     : "";
            if (runsEarlier.graph != runsLater.graph) {
                // The pass left them in two graphs, and the calls that hold these graphs do not
                // make the one wait for the other either: the written order is gone.
                return std::format(
                    "the effects on '{}' are not in the order the program wrote them: the program "
                    "wrote {} before {}{}, and this pass leaves them in two graphs where no call "
                    "makes the one wait for the other any more: {} and {} ({})",
                    resource,
                    describeEffect(earlier),
                    describeEffect(later),
                    writtenIn,
                    describeEffect(runsEarlier),
                    describeEffect(runsLater),
                    moves);
            }
            return std::format(
                "the effects on '{}' are not in the order the program wrote them: the program "
                "wrote {} before {}, and this pass leaves {} before {} with nothing in the graph "
                "that makes them wait for one another any more ({})",
                resource,
                describeEffect(earlier),
                describeEffect(later),
                describeEffect(runsLater),
                describeEffect(runsEarlier),
                moves);
        }
    }
    return {};
}

} // namespace

PassRunResult applyRegisteredPass(
    const camel::runtime::Program *program, const std::string &spec, const context_ptr_t &ctx,
    std::ostream &os, GraphIRPass *caller) {
    ResolvedPass resolved = resolvePassWithModule(spec, ctx);
    if (!resolved.factory) {
        // No pipeline source here (a pass run by name from a tool): the first line is the call.
        throwUnresolvedPass(spec, resolved, 0, ctx);
    }
    const bool publishes = ctx && ctx->currentRuntimeProgram() == program;
    // The old program's content is read here, before the pass runs (graphRevisionsOf).
    const std::unordered_map<uint32_t, uint64_t> beforeRevisions = graphRevisionsOf(program);
    auto pass            = resolved.factory(ctx);
    const camel::runtime::Program *produced = pass->apply(program, os);
    if (pass->hasErrorValue() && caller != nullptr) {
        caller->failWithErrorValue(pass->errorValue());
    }
    if (produced == nullptr) {
        return {nullptr, true};
    }
    const bool changed = programContentChanged(program, beforeRevisions, produced);
    if (ctx) {
        deliverPassWarnings(*ctx, spec);
        // A pass that ran on the installed program publishes a new one; its old program is no
        // longer needed. A pass over a private program (onnx.export's working copy) does not
        // install it, and the caller keeps it alive (the commit tracked it as detached).
        if (publishes && ctx->currentRuntimeProgram() == produced) {
            ctx->settleRuntimeGraphs(produced);
        }
        if (ctx->rtmDiags()->hasErrors()) {
            return {nullptr, true};
        }
    }
    return {produced, changed};
}

namespace {

const camel::runtime::Program *runRulesAlgebra(
    const camel::core::context::context_ptr_t &ctx, const camel::runtime::Program *program,
    bool safeFp, std::ostream &os, GraphIRPass *caller) {
    const PassRunResult result = applyRegisteredPass(
        program,
        safeFp ? "rules::simplify strict=true" : "rules::simplify",
        ctx,
        os,
        caller);
    return result.program;
}

} // namespace

namespace {

/// A step the pipeline runs ahead of another pass, whether or not the writer names it: a lowering
/// step is the input a later pass is written against, not another optimization a writer has to
/// remember to ask for.
struct FixedLoweringStep {
    const char *pass;
    const char *before;
};

/// Defunctionalization is the step the optimizer is written against: no call through a function
/// value and no closure is left when `std::opt` starts, so specialization meets a call it can
/// specialize rather than a tag it would have to guess at.
constexpr FixedLoweringStep fixedLoweringSteps[] = {
    // The step says nothing about itself here: a program that wants the report names the pass.
    {"std::defunctionalize report=false", "std::opt"},
};

/// The plan the pipeline runs: `passes` with each fixed step inserted ahead of the pass it belongs
/// before, unless the writer named it already, and nothing at all when the pass it belongs before is
/// not in the plan -- the pipeline runs what the program asked for, plus the steps that pass needs.
std::vector<std::string> expandFixedLoweringSteps(const std::vector<std::string> &passes) {
    std::vector<std::string> plan = passes;
    for (const FixedLoweringStep &step : fixedLoweringSteps) {
        const auto target = std::find_if(
            plan.begin(), plan.end(),
            [&](const std::string &spec) { return specName(spec) == step.before; });
        if (target == plan.end()) {
            continue;
        }
        const bool named = std::any_of(plan.begin(), plan.end(), [&](const std::string &spec) {
            return specName(spec) == step.pass;
        });
        if (named) {
            continue;
        }
        plan.insert(target, std::string(step.pass));
    }
    return plan;
}

} // namespace

void requireKnownPasses(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    for (size_t i = 0; i < passes.size(); ++i) {
        const auto &name = passes[i];
        auto resolved    = resolvePassWithModule(name, ctx);
        if (!resolved.factory) {
            // One pass per line of the pipeline (`--passes a,b` or the passes after the file),
            // so a parameter diagnostic points at the line the call is written on.
            throwUnresolvedPass(name, resolved, i, ctx);
        }
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

/// The Camel passes of the pipeline that declare `#reads(...)` on their function, by the tag
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
    // The registry says which of the core's passes read tag facts, and it is built on first
    // use. A run whose pipeline never looks a pass up would otherwise find no core reader at
    // all.
    ensureCorePasses();
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
        // `#reads(...)` that they read it.
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
    const camel::runtime::Program *program, const std::vector<std::string> &written,
    const context_ptr_t &ctx, std::ostream &os) {
    // What runs is what was written plus the fixed steps a written pass needs; the log and every
    // check below read the plan, so a step that runs is a step the log names.
    const std::vector<std::string> passes = expandFixedLoweringSteps(written);
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

        auto factory = findPassFactory(p, ctx);
        if (factory) {
            camel::core::mm::autoSpace().safepoint(std::format("before pass {}", p));
            EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit(p.c_str(), program->root()); });
            camel::utils::phase::Scope phaseScope("pass", p);
            auto pass = factory(ctx);
            // The debugger's observer is told which pass runs: only a scheduling pass runs the
            // program it debugs; a rewrite or translation pass may evaluate graphs of its own.
            const bool schedules = dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr;
            if (auto *observer = camel::execute::executionObserver()) {
                observer->passStarting(schedules, program->root());
            }
            // Diagnostics raised while this pass runs carry its stage: "run" for a scheduling
            // pass (the program executes), "pass:<name>" for rewrite and translation passes.
            struct StageScope {
                explicit StageScope(std::string stage) { setPipelineStage(std::move(stage)); }
                ~StageScope() { setPipelineStage(""); }
            } stageScope(
                schedules ? std::string("run") : "pass:" + specName(p));
            // A program runs only when every parameter of `main` has a value. Parameters
            // left open are fine for rewriting and translation (they are the inputs of what
            // is translated); nothing may execute with a slot nobody filled.
            if (schedules) {
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
            // cost; a pass that does not know the state does not get the program. A consuming
            // pass that declares it reads the state (a dump drawing the classes) is given
            // the program as it is.
            if (camel::runtime::hasWritings(*program) && !pass->acceptsWritings()) {
                const bool consumes = dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr ||
                                      dynamic_cast<GraphTranslatePass *>(pass.get()) != nullptr;
                if (consumes) {
                    CAMEL_LOG_INFO_S(
                        "Pass",
                        "run | passes | extract before {}: one writing per value, cost model "
                        "'{}'",
                        p,
                        camel::runtime::kWritingsCostModel);
                    program = camel::runtime::extractWritings(
                        ctx,
                        program,
                        *camel::runtime::findCostModel(camel::runtime::kWritingsCostModel));
                    ctx->settleRuntimeGraphs(program);
                } else {
                    setPipelineStage("pass:" + specName(p));
                    ctx->rtmDiags()->of(RuntimeDiag::PassNeedsExtraction).commit(p);
                    return {nullptr, PassApplyStatus::Failed};
                }
            }
            // The effects the program writes, and the order it writes them in, before the pass
            // runs: the effects on one resource keep that order through every pass (design
            // principles 3.3), and the end of this iteration compares. Read here rather than after
            // the pass because the program the pass is given is not read once it has returned.
            const ProgramEffects effectsBefore = collectEffects(*program);
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
            } catch (const std::bad_alloc &) {
                // A library container's `operator new` failed: that throws a bare `std::bad_alloc`,
                // which the runtime's own `mm::MemoryExhausted` (a class derived from it) does not
                // cover, so the clause above never sees it, and the clause below would read it as a
                // defect of the compiler. From the program's side it is the same limit as any other
                // allocation failure -- the memory ran out while a pass was running -- so it is
                // reported with the same diagnostic. Only the room is missing: no space of the
                // runtime can be named and there is no usage or limit of its own to print, because
                // the limit is the host's.
                setPipelineStage("pass:" + specName(p));
                auto diag = ctx->rtmDiags()->of(RuntimeDiag::MemoryExhausted);
                if (ctx->sourceContext() != nullptr) {
                    const uint64_t origin = ctx->sourceContext()->currentRuntimeOrigin();
                    if (origin != camel::source::kInvalidOriginId) {
                        diag.atOrigin(origin);
                    }
                }
                diag.commit(
                    std::format(" in pass '{}'", p),
                    "the host has no memory left: an allocation outside the runtime's own spaces "
                    "failed");
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
            // A pass is `(f: F) => F!` (design principles 9.1): a pass that cannot produce the
            // program ends with an error value instead of a diagnostic. The pipeline stops at that
            // pass -- nothing after it runs, the fallback list included -- and the value is handed
            // to the driver that ran the list, which reports it the way an error value nobody
            // handled is (3.9): on standard error, with the exit code the run ends with
            // (reportPassErrorValue). The value is read here, inside the same safepoint window as
            // `apply`, and the driver reports it before the next one, so nothing collects it in
            // between.
            if (pass->hasErrorValue()) {
                const slot_t error = pass->errorValue();
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (error value)", p);
                return {nullptr, PassApplyStatus::Errored, error};
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
            // The pass ended with a program: the effects on one resource must still be in the
            // order the program wrote them, which is the order its graph makes them wait for
            // (design principles 3.3). The check is the last thing the pass does, so that no pass
            // of the pipeline, nested or listed, ends without it.
            {
                const std::string violation =
                    effectOrderViolation(effectsBefore, collectEffects(*program));
                if (!violation.empty()) {
                    ctx->rtmDiags()->of(RuntimeDiag::PassInternalError).commit(p, violation);
                    CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (effect order)", p);
                    return {nullptr, PassApplyStatus::Failed};
                }
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








