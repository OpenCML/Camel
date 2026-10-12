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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/execute/pass.h"
#include "camel/common/scope.h"
#include "camel/core/process_exit.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/capacity.h"
#include "camel/core/mm.h"
#include "camel/core/mm/foreign_handle.h"
#include "camel/core/mm/profiler.h"
#include "macro/macro.h"
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
#include "passes/test/fault.h"
#include "passes/trans/cpp/cpp_export.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/stats/stats.h"
#include "passes/trans/tns/topo_node_seq.h"

#include "camel/utils/log.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>

using namespace camel::core::error;
using namespace camel::core::context;
using camel::runtime::GCGraph;

GCGraph *NullGraphIRPass::apply(GCGraph *graph, std::ostream &os) {
    (void)graph;
    (void)os;
    return nullptr;
}

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

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        (void)graph;
        os << camel::core::mm::profiler::snapshotToJson() << '\n';
        return nullptr;
    }
};

class GcVerifyPass final : public GraphIRPass {
  public:
    explicit GcVerifyPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
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
        return graph;
    }
};

class GcMinorPass final : public GraphIRPass {
  public:
    explicit GcMinorPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        camel::core::mm::autoSpace().minorGC();
        os << "{\"ok\":true,\"kind\":\"gc.minor\"}\n";
        return graph;
    }
};

class GcMajorPass final : public GraphIRPass {
  public:
    explicit GcMajorPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        camel::core::mm::autoSpace().majorGC();
        os << "{\"ok\":true,\"kind\":\"gc.major\"}\n";
        return graph;
    }
};

class GcConfigPass final : public GraphIRPass {
  public:
    explicit GcConfigPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        os << camel::core::mm::profiler::configToJson() << '\n';
        return graph;
    }
};

class GcSummaryPass final : public GraphIRPass {
  public:
    explicit GcSummaryPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        os << camel::core::mm::profiler::summaryToText();
        return graph;
    }
};

class GcRememberedSetPass final : public GraphIRPass {
  public:
    explicit GcRememberedSetPass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        camel::core::mm::autoSpace().debugRunRememberedSetSelfTest();
        os << "{\"ok\":true,\"kind\":\"gc.remembered_set\"}\n";
        return graph;
    }
};

class GcForeignResourcePass final : public GraphIRPass {
  public:
    explicit GcForeignResourcePass(const context_ptr_t &ctx) : GraphIRPass(ctx) {}

    GCGraph *apply(GCGraph *graph, std::ostream &os) override {
        camel::core::mm::debugRunForeignResourceSelfTest(camel::core::mm::autoSpace());
        os << "{\"ok\":true,\"kind\":\"gc.foreign_resource\"}\n";
        return graph;
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
                    {"stats", def(PASS(GraphStatsPass))},
                    {"readable_graphviz",
                     def(PASS1(GraphVizDumpPass, GraphVizDumpConfig{.readableOnly = true}))},
                    {"annotated_graphviz",
                     def(PASS1(GraphVizDumpPass, GraphVizDumpConfig{.annotateRoles = true}))},
                    {"readable_annotated_graphviz",
                     def(PASS1(
                         GraphVizDumpPass,
                         (GraphVizDumpConfig{.readableOnly = true, .annotateRoles = true})))},
                    {"cpp",
                     def(PASS(CppDumpPass),
                         {
                             {"module", def(PASS(CppModuleDumpPass))},
                             {"inspect", def(PASS(CppInspectDumpPass))},
                             {"bench", def(PASS(CppBenchDumpPass))},
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
                         {"remembered_set", def(PASS(GcRememberedSetPass))},
                         {"foreign_resource", def(PASS(GcForeignResourcePass))},
                     })},
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
                    {"devirtualize", def(PASS(DevirtualizeRewritePass))},
                    {"specialize", def(PASS(SpecializeRewritePass))},
                    {"opt",
                     def(PASS(OptimizeRewritePass),
                         {
                             {"fold", def(PASS(ConstantFoldPass))},
                             {"cse", def(PASS(CommonSubexpressionPass))},
                             {"dce", def(PASS(DeadCodePass))},
                             {"simplify", def(PASS(SimplifyPass))},
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
    {"std::tf", "std::taskflow"},
    // Common translation and dump aliases
    // Common translation and dump aliases
    {"std::dot", "std::graphviz"},
    {"std::gir", "std::graphviz"},
    {"std::rdot", "std::readable_graphviz"},
    {"std::rgir", "std::readable_graphviz"},
    {"std::agir", "std::annotated_graphviz"},
    {"std::argir", "std::readable_annotated_graphviz"},
    {"std::cxx", "std::cpp"},
    {"std::cppmod", "std::cpp::module"},
    {"std::cppinspect", "std::cpp::inspect"},
    {"std::cppbench", "std::cpp::bench"},
    {"std::tns", "std::topo_node_seq"},
    {"std::gcsnap", "std::gc::snapshot"},
    {"std::gcverify", "std::gc::verify"},
    {"std::gcminor", "std::gc::minor"},
    {"std::gcmajor", "std::gc::major"},
    {"std::gcconfig", "std::gc::config"},
    {"std::gcsummary", "std::gc::summary"},
    {"std::gcremembered", "std::gc::remembered_set"},
    {"std::gcforeign", "std::gc::foreign_resource"},
    {"std::bc", "std::fastvm::bytecode"},
    {"std::lbc", "std::fastvm::linked_bytecode"},
    {"std::bin", "std::fastvm::jit::dump::bin"},
    {"std::asm", "std::fastvm::jit::dump::asm"},
    {"std::rmir", "std::fastvm::jit::dump::rmir"},
    {"std::mir", "std::fastvm::jit::dump::mir"},
    {"std::tfg", "std::tfdump"},
};

struct ModulePassRegistry {
    std::mutex mutex;
    std::map<std::string, PassFactory> factories; // ordered for the "available passes" listing
};

ModulePassRegistry &modulePasses() {
    static ModulePassRegistry registry;
    return registry;
}

PassFactory findModulePass(const std::string &path) {
    auto &registry = modulePasses();
    std::lock_guard lock(registry.mutex);
    auto it = registry.factories.find(path);
    return it == registry.factories.end() ? nullptr : it->second;
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
        it->stage = "pass:" + name;
        if (it->severity == Severity::Error) {
            promoted.push_back(std::move(*it));
            it = fresh.erase(it);
        } else {
            ++it;
        }
    }
    if (!fresh.empty()) {
        if (PassWarningHandler handler =
                passWarningHandlerSlot().load(std::memory_order_acquire)) {
            handler(
                name.c_str(),
                fresh,
                passWarningUserDataSlot().load(std::memory_order_acquire));
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

void registerModulePass(const std::string &path, PassFactory factory) {
    auto &registry = modulePasses();
    std::lock_guard lock(registry.mutex);
    registry.factories.insert_or_assign(path, std::move(factory));
}

namespace {

/// The factory of a pass name, or null. Prints nothing.
PassFactory lookupPassFactory(const std::string &name) {
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

    // 4. Passes contributed by loaded modules (full names such as "tensor::fuse")
    if (auto factory = findModulePass(resolved)) {
        return factory;
    }

    // 5. Test-only fault-injection passes, off unless the environment enables them.
    if (resolved == kDropCallArgumentPass && std::getenv(kTestPassesEnvVar) != nullptr) {
        return [](const context_ptr_t &ctx) -> std::unique_ptr<GraphIRPass> {
            return std::make_unique<DropCallArgumentTestPass>(ctx);
        };
    }
    if (resolved == kOvergrowGraphPass && std::getenv(kTestPassesEnvVar) != nullptr) {
        return [](const context_ptr_t &ctx) -> std::unique_ptr<GraphIRPass> {
            return std::make_unique<OvergrowGraphTestPass>(ctx);
        };
    }
    if (resolved == kLeakBranchArmPass && std::getenv(kTestPassesEnvVar) != nullptr) {
        return [](const context_ptr_t &ctx) -> std::unique_ptr<GraphIRPass> {
            return std::make_unique<LeakBranchArmTestPass>(ctx);
        };
    }
    if (resolved == kShareBranchArmPass && std::getenv(kTestPassesEnvVar) != nullptr) {
        return [](const context_ptr_t &ctx) -> std::unique_ptr<GraphIRPass> {
            return std::make_unique<ShareBranchArmTestPass>(ctx);
        };
    }
    return nullptr;
}

/// Lists the available passes on stderr. The listing is help text for the command line:
/// stdout stays reserved for program output and the diagnostics array.
void listAvailablePasses(const std::string &name) {
    std::ostream &help = std::cerr;
    help << std::format("Pass <{}> not found, available passes are:\n", name);
    std::vector<std::string> allPaths;
    collectPassPaths(passScope, "", allPaths);
    for (const auto &p : allPaths) {
        help << std::format("  {}\n", p);
    }
    {
        auto &registry = modulePasses();
        std::lock_guard lock(registry.mutex);
        if (!registry.factories.empty()) {
            help << std::format("Passes contributed by loaded modules:\n");
            for (const auto &entry : registry.factories) {
                help << std::format("  {}\n", entry.first);
            }
        }
    }
    help << std::format("Available aliases are:\n");
    for (const auto &[alias, target] : passAliases) {
        help << std::format("  {} -> {}\n", alias, target);
    }
    help << std::endl;
}

} // namespace

PassFactory findPassFactory(const std::string &name, std::ostream &os) {
    (void)os;
    auto factory = lookupPassFactory(name);
    if (!factory) {
        listAvailablePasses(name);
    }
    return factory;
}

void requireKnownPasses(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    for (const auto &name : passes) {
        if (lookupPassFactory(name)) {
            continue;
        }
        // `<module>::<pass>`: the pass is contributed by a module the program did not import.
        // Loading the module registers it.
        if (ctx) {
            std::string resolved = name;
            if (auto aliasIt = passAliases.find(name); aliasIt != passAliases.end()) {
                resolved = aliasIt->second;
            }
            const size_t sep = resolved.find("::");
            if (sep != std::string::npos && sep > 0 && resolved.substr(0, sep) != "std") {
                try {
                    (void)ctx->importModule(resolved.substr(0, sep));
                } catch (const Diagnostic &) {
                    // No such module: the name stays unknown and is reported below.
                } catch (const std::exception &) {
                }
                if (lookupPassFactory(name)) {
                    continue;
                }
            }
        }
        listAvailablePasses(name);
        setPipelineStage("pass:" + name);
        auto unknown = DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(name);
        unknown.stage = "pass:" + name;
        setPipelineStage("");
        throw unknown;
    }
}

std::optional<size_t>
firstConsumingPass(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    for (size_t i = 0; i < passes.size(); ++i) {
        auto factory = lookupPassFactory(passes[i]);
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
    auto warning = DiagnosticBuilder::of(RuntimeDiag::PassesNeverRun).commit(passes[*consumer], rest);
    warning.stage = "pass:" + passes[*consumer];
    return warning;
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
    // Every name is checked before the first pass runs: an unknown pass written after a
    // scheduling pass must not let the program execute first.
    requireKnownPasses(passes, ctx);
    size_t passOutputIndex = 0;
    for (const auto &p : passes) {
        if (graph == nullptr) {
            return {nullptr, PassApplyStatus::Consumed};
        }

        auto factory = findPassFactory(p, os);
        if (factory) {
            camel::core::mm::autoSpace().safepoint(std::format("before pass {}", p));
            EXEC_WHEN_DEBUG({ camel::DebugBreakpoint::Hit(p.c_str(), graph); });
            auto pass = factory(ctx);
            // Diagnostics raised while this pass runs carry its stage: "run" for a scheduling
            // pass (the program executes), "pass:<name>" for rewrite and translation passes.
            struct StageScope {
                explicit StageScope(std::string stage) { setPipelineStage(std::move(stage)); }
                ~StageScope() { setPipelineStage(""); }
            } stageScope(
                dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr ? std::string("run")
                                                                         : "pass:" + p);
            // A program runs only when every parameter of `main` has a value. Parameters
            // left open are fine for rewriting and translation (they are the inputs of what
            // is translated); nothing may execute with a slot nobody filled.
            if (dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr) {
                const auto open = camel::execute::unboundEntryParameters(graph);
                if (!open.empty()) {
                    std::string names;
                    for (size_t i = 0; i < open.size(); ++i) {
                        names += (i == 0 ? "'" : i + 1 == open.size() ? " and '" : ", '") +
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
            try {
                graph = pass->apply(graph, os);
            } catch (const DiagnosticsLimitExceededBaseException &) {
                throw; // diagnostics already recorded; the caller dumps them
            } catch (const camel::core::ProcessExit &exit) {
                // The program called exit(code): the run is over, with that exit code and no
                // diagnostic. The pipeline ends as after a scheduling pass that completed.
                ctx->setProcessExitCode(exit.code);
                CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> exit({})", p, exit.code);
                return {nullptr, PassApplyStatus::Consumed};
            } catch (const camel::runtime::GraphCapacityError &e) {
                // The pass built a graph larger than a graph can be. That is a limit of the
                // representation the program ran into, not a broken graph: it is reported with
                // the graph, the quantity, the size reached and the limit, at the function the
                // graph was compiled from (or at one of its nodes).
                setPipelineStage("pass:" + p);
                auto diag = ctx->rtmDiags()->of(RuntimeDiag::GraphTooLarge);
                if (e.origin() != camel::source::kInvalidOriginId) {
                    diag.atOrigin(e.origin());
                }
                diag.commit(
                    p,
                    e.graph().empty() ? std::string("(unnamed)") : e.graph(),
                    e.size(),
                    e.quantity(),
                    e.limit());
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (graph too large)", p);
                return {nullptr, PassApplyStatus::Failed};
            } catch (const camel::core::mm::MemoryExhausted &e) {
                // A memory space is at its limit, or the host has no memory left. Like a graph
                // that is too large this is a limit the program (or what the pass made of it)
                // ran into: it is reported with the space, the usage, the limit and the
                // setting that raises it, at the graph that was being built when one is known.
                setPipelineStage("pass:" + p);
                auto diag = ctx->rtmDiags()->of(RuntimeDiag::MemoryExhausted);
                if (e.origin() != camel::source::kInvalidOriginId) {
                    diag.atOrigin(e.origin());
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
                setPipelineStage("pass:" + p);
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
            if (ctx->rtmDiags()->hasErrors()) {
                CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (see diagnostics)", p);
                return {nullptr, PassApplyStatus::Failed};
            }
            if (!graph) {
                CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> consumed", p);
                return {nullptr, PassApplyStatus::Consumed};
            }
            if (PassSnapshotHandler snapshotHandler =
                    passSnapshotHandlerSlot().load(std::memory_order_acquire)) {
                snapshotHandler(
                    p.c_str(),
                    ++passOutputIndex,
                    graph,
                    passSnapshotUserDataSlot().load(std::memory_order_acquire));
            }
            CAMEL_LOG_INFO_S("Pass", "run | passes | OK {} -> next graph '{}'", p, graph->name());
        } else {
            setPipelineStage("pass:" + p);
            auto unknown = DiagnosticBuilder::of(RuntimeDiag::UnrecognizedGraphPass).commit(p);
            setPipelineStage("");
            throw unknown;
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
