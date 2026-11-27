// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/pass.h"
#include "camel/common/scope.h"
#include "camel/core/debug_breakpoint.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/mm.h"
#include "camel/core/module/module.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/process_exit.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/slot.h"
#include "camel/core/source/manager.h"
#include "camel/core/tags.h"
#include "camel/core/trace.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/observer.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/definitions.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/effects.h"
#include "camel/runtime/graph_exit_type.h"
#include "camel/runtime/resources.h"
#include "camel/runtime/writings.h"
#include "camel/utils/assert.h"
#include "camel_pass.h"
#include "pass_declarations.h"
#include "pass_spec.h"

#include "camel/utils/log.h"
#include "camel/utils/phase_timer.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using namespace camel::core::error;
using namespace camel::core::context;
using camel::runtime::GCGraph;

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

/// The core passes that read tag facts: the dumps show a node's attributes and numeric
/// permissions, and the optimizing pipeline keeps them across rewrites (CSE combines by their
/// merge rules). A tag's `read_by` names one of these (or a module's pass that registers itself).
/// Both names of the pipeline are readers: `std::opt` is the default pipeline's name, and
/// `std::cxx` covers the C++ passes named under the isolation (`std::cxx::opt`, `std::cxx::fold`)
/// by the prefix rule that makes a name cover the passes under it.
///
/// This is the tag registry's answer, not the pipeline's: the kernels are registered by the files
/// that implement them (camel/execute/pass/registration.h), and what the core still has to say
/// about a pass here is which of its names read tag facts.
void declareCoreTagReaders() {
    for (const char *reader :
         {"std::gir",
          "std::rgir",
          "std::gir_regions",
          "std::rgir_regions",
          "std::jgir",
          "std::agir",
          "std::argir",
          "std::opt",
          "std::cxx"}) {
        camel::core::tag::declareReader(reader);
    }
}


const std::map<std::string, std::string> passAliases = {
    // The optimization passes are registered under `std::cxx::` -- the C++ implementations, kept
    // apart from the Camel ones in the module `opt`. Their old spellings (`std::opt::fold`,
    // `std::opt::inline`, the four inline strategies and the standalone `std::inline` and the rest)
    // are **not**
    // aliases: a pipeline that writes one is refused as an unknown pass, which is the isolation's
    // own judgement rather than compatibility debt(the old writings are deleted, not carried).
    //
    // `std::opt` stays, and it is the one entry here that is not a spelling of another pass: it is
    // the name of the default pipeline -- the choice "optimize by default". A program that names
    // no pass, and the pass the fixed lowering steps insert, go through this entry, and today it
    // selects the C++ implementation. After the comparison experiment of docs/roadmap.cn.md
    // section 0, P, the owner decides whether it selects the Camel one instead; nothing else in
    // this table has to move for that.
    {"std::opt", "std::cxx::opt"},
    {"std::fastvm::jit::dump::asm", "std::fastvm::jit_asm"},
    {"std::fastvm::jit::dump::bin", "std::fastvm::jit_bin"},
    {"std::fastvm::jit::dump::rmir", "std::fastvm::jit_rmir"},
    {"std::fastvm::jit::dump::mir", "std::fastvm::jit_mir"},
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
    {"std::type::array", "std::arraytype"},
    {"std::type::identity", "std::identitytype"},
    // `std::cxx` is not a shorthand of the C++ exporter, `std::cpp`:
    // `cxx` is the namespace the C++ optimization passes are named under (`std::cxx::opt`,
    // `std::cxx::fold`), and leaving the shorthand in place would answer `std::cxx` with a
    // translation pass while a reader who learned the namespace from docs/cli.md means the
    // optimizer. The exporter keeps its own name, `std::cpp`, and its two others below.
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

/// The kernels keyed by the operator the pass's declaration names, one entry per operator: the
/// registry a pass registers in when it registers as what it is rather than under a spelling of
/// its name (`registerPassKernel`). Indexed by `OperId`, so a lookup is one indexed read and the
/// two spellings of one pass cannot hold two kernels.
struct PassKernelRegistry {
    std::mutex mutex;
    std::vector<std::optional<PassDescriptor>> kernels;
};

PassKernelRegistry &passKernels() {
    static PassKernelRegistry registry;
    return registry;
}

/// Declares the core's tag readers on first resolution, so a run that never resolves a pass pays
/// nothing for the table.
void ensureCoreTagReaders() {
    static std::once_flag once;
    std::call_once(once, [] { declareCoreTagReaders(); });
}


/// The implementation bound to the function `module::function`, whichever side of the core it
/// belongs to. The name, documentation and parameters are the declaration's; this answers only
/// which function implements it.
std::optional<PassDescriptor> findImplementation(const std::string &path) {
    ensureCoreTagReaders();
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
    auto &registry       = passImplementations();
    {
        std::lock_guard lock(registry.mutex);
        registry.passes.insert_or_assign(path, std::move(descriptor));
    }
    if (readsTags) {
        // The pass is a reader of tag facts.
        camel::core::tag::declareReader(path);
    }
}

void registerModulePass(const std::string &path, PassFactory factory) {
    registerModulePass(path, passWithoutParameters(std::move(factory)));
}

void registerPassKernel(camel::core::OperId pass, PassDescriptor descriptor) {
    if (pass == camel::core::kNoOper) {
        return; // no operator, no pass: nothing to key the kernel by
    }
    const bool readsTags = descriptor.readsTags;
    auto &kernels        = passKernels();
    {
        std::lock_guard lock(kernels.mutex);
        if (kernels.kernels.size() <= pass) {
            kernels.kernels.resize(static_cast<size_t>(pass) + 1);
        }
        // One registration per operator: the second one is the same pass declared twice, and two
        // kernels for one declaration can never answer consistently.
        ASSERT(!kernels.kernels[pass].has_value(), "a pass kernel is registered once per operator");
        kernels.kernels[pass] = std::move(descriptor);
    }
    if (readsTags) {
        // The pass is a reader of tag facts, and its name in that registry is the operator's own.
        camel::core::tag::declareReader(
            std::string(camel::core::OperatorTable::instance().uri(pass)));
    }
}

std::optional<PassDescriptor> findPassKernel(camel::core::OperId pass) {
    if (pass == camel::core::kNoOper) {
        return std::nullopt;
    }
    auto &kernels = passKernels();
    std::lock_guard lock(kernels.mutex);
    if (static_cast<size_t>(pass) >= kernels.kernels.size()) {
        return std::nullopt;
    }
    return kernels.kernels[pass];
}

namespace {

/// The pass a written name resolves to: the core's table, whose one entry `std::opt` is the default
/// pipeline's name -- a choice rather than a spelling of another pass -- so no sub-pass of the
/// optimizer is reachable under an old name.
std::string resolveAlias(const std::string &name) {
    if (auto it = passAliases.find(name); it != passAliases.end()) {
        return it->second;
    }
    return name;
}

/// A pass a name resolves to: how to make it and the parameters its declaration gives. The
/// declaration is the standard library's; a pass written in Camel's is its signature's `with`
/// list.
struct FoundPass {
    PassDescriptor descriptor;
    std::vector<PassParam> params;
    /// A declaration that exists but whose applicability condition does not hold.
    std::string inapplicable;
    /// The module's declarations could not be read (its file does not parse): the sentence the
    /// report gives, and the location of the parser's first error. This is the same answer pass
    /// resolution gives a pass library that does not compile, and it is where the declaration
    /// reader's failure arrives: it is a diagnostic against the module, not an exception.
    std::string problem;
    std::optional<DiagLabel> problemLocation;
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
        const std::string path    = module + "::" + function;
        const auto implementation = findImplementation(path);
        // The module's declaration file is read before the pass is looked for in it. A file that
        // does not parse is a diagnostic against that module -- the same one the module loader
        // would give -- and this is where that answer is carried: the report below turns it into
        // the `InvalidPassParameter` a pass library that does not compile already gets.
        if (auto problem = camel::execute::detail::declarationProblem(module, ctx)) {
            FoundPass found;
            found.problem         = std::move(problem->text);
            found.problemLocation = std::move(problem->location);
            return found;
        }
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
std::optional<camel::execute::detail::ParamProblem>
valueProblem(const std::string &name, const PassArgs &args);

/// Resolves the function-valued parameters of one call: the word names a function of a library
/// module (`<module>::<function>`, the module being the path the name lies on, as
/// camel/common/ref.h states it), and the value the pass reads is that function. The function is
/// compiled as the program the store keeps for it (its entry graph has the function's own
/// signature and computes the library's module-level values first), and the pass receives a
/// function value over that program, rooted by the PassArgs for as long as the arguments live.
/// A name that is no function is refused here, while the pipeline is still being read, so the
/// report carries the position of the word (camel/execute/pass/params.h).
std::optional<camel::execute::detail::ParamProblem> bindFunctionValues(
    const std::vector<PassParam> &declared, const camel::execute::detail::ParsedSpec &parsed,
    PassArgs &args, const context_ptr_t &ctx) {
    using camel::execute::detail::ParamProblem;
    using camel::execute::detail::ParamProblemKind;
    for (const PassParam &param : declared) {
        if (param.type != PassParamType::Function || args.isNull(param.name)) {
            continue;
        }
        const std::string written = args.getString(param.name);
        const auto refuse         = [&](std::string text) {
            ParamProblem problem;
            problem.kind = ParamProblemKind::Syntax;
            problem.name = param.name;
            problem.text = std::move(text);
            for (const auto &word : parsed.params) {
                if (word.key == param.name) {
                    // The value, not the whole word: the name is not what is wrong.
                    problem.column = word.column + word.key.size() + 1;
                    problem.length = word.raw.size();
                    break;
                }
            }
            return problem;
        };
        if (ctx == nullptr) {
            return refuse(std::format(
                "the parameter '{}' names the function '{}', and there is no context to resolve it "
                "in",
                param.name,
                written));
        }
        // The word is a qualified name, and what one is the runtime's own rule states
        // (camel/common/ref.h): the last segment is the name, the segments before it are the path
        // the name lies on. The path is the library module's name here, and the module loader
        // spells a module name with '.' (Context::importModule, ModuleResolver::splitLookup): the
        // module named `std.cost` is the file std/cost.cml, so the word that names its `default`
        // is `std::cost::default` -- not a function `cost::default` of a module `std`, which is
        // what splitting at the first `::` would have read, and would have failed to resolve.
        Reference named;
        try {
            named = Reference(written);
        } catch (const std::exception &) {
            return refuse(std::format(
                "the parameter '{}' takes a function value, written <module>::<function>; '{}' "
                "names no function",
                param.name,
                written));
        }
        bool wellFormed = !named.plain() && !named.ident().empty();
        for (const std::string &segment : named.paths()) {
            wellFormed = wellFormed && !segment.empty();
        }
        if (!wellFormed) {
            return refuse(std::format(
                "the parameter '{}' takes a function value, written <module>::<function>; '{}' "
                "names no function",
                param.name,
                written));
        }
        std::string module;
        for (const std::string &segment : named.paths()) {
            module += segment;
            module += '.';
        }
        module.pop_back();
        const std::string function = named.ident();
        std::string problem;
        std::optional<DiagLabel> where;
        camel::runtime::DefinitionStore::LibraryLookup lookup;
        const auto found = ctx->definitions().libraryFunction(
            module, function, problem, &where, &lookup);
        if (!found) {
            return refuse(std::format(
                "the parameter '{}' takes the function '{}', which cannot be used: {}",
                param.name,
                written,
                problem.empty() ? std::string("no function of that name") : problem));
        }
        // The library's entry graph has the function's signature and computes its module-level
        // values before it calls the function, so the value needs no closure of its own.
        static const camel::core::type::TupleType *noClosure =
            camel::core::type::TupleType::create();
        ::Function *value = ::Function::create(
            found->program, found->graph->id(), noClosure, camel::core::mm::autoSpace());
        PassFunction argument;
        argument.name = written;
        argument.value = camel::core::rtdata::toSlot(value);
        argument.type  = found->graph->funcType();
        argument.root  = std::make_shared<camel::core::mm::RootHandle>(
            camel::core::mm::autoSpace(),
            value,
            found->graph->funcType(),
            "pass." + param.name);
        args.setFunction(param.name, std::move(argument));
    }
    return std::nullopt;
}

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
                result.known           = true;
                const std::string path = camel::execute::detail::moduleDeclarationPath("std", ctx);
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
    const auto descriptor = findDescriptor(ctx, parsed.name);
    if (!descriptor) {
        return result;
    }
    result.known = true;
    if (!descriptor->problem.empty()) {
        // The module's declarations could not be read: the module did not load, and the answer is
        // the one a module that does not compile gets, with the parser's first error as the
        // secondary location.
        result.problem       = descriptor->problem;
        result.errorLocation = descriptor->problemLocation;
        return result;
    }
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
    // A function-valued parameter is resolved here, with the context the call runs in: the value
    // is a function of a library module, and a name that is no function is a problem of the call.
    if (auto problem = bindFunctionValues(descriptor->params, parsed, args, ctx)) {
        result.paramProblem = std::move(problem);
        return result;
    }
    // A value the declaration's type accepted but the pass cannot use. This is where it is
    // reported: the pipeline is still being read, so the diagnostic carries the position of the
    // word, the way a wrongly typed parameter does -- and the pass is never built, so nothing here
    // can end up as an exception nothing catches.
    if (auto problem = valueProblem(parsed.name, args)) {
        result.paramProblem = std::move(problem);
        return result;
    }
    result.factory = [make = descriptor->descriptor.make,
                      args = std::move(args)](const context_ptr_t &ctx) { return make(ctx, args); };
    return result;
}

/// A value the declaration's type accepted and the pass cannot use: checked while the pipeline is
/// read, so the report has a position and the pass is never built with a value it refuses.
std::optional<camel::execute::detail::ParamProblem>
valueProblem(const std::string &name, const PassArgs &args) {
    using camel::execute::detail::ParamProblem;
    using camel::execute::detail::ParamProblemKind;
    // `std::opt settle_runs`: the bound the rewrite engine reports at. It is an internal bound
    // (see settleRunsOf), and 0 is refused rather than read as "never report". The pipeline is
    // registered as `std::cxx::opt` and `std::opt` is the default pipeline's name for it, so the
    // check is made on the name the specification resolves to: a program that writes either
    // spelling gets the refusal.
    if (resolveAlias(name) == "std::cxx::opt" && args.given("settle_runs") &&
        args.getInt("settle_runs") < 1) {
        ParamProblem problem;
        problem.kind     = ParamProblemKind::OutOfRange;
        problem.name     = "settle_runs";
        problem.found    = args.text("settle_runs");
        problem.expected = "at least 1";
        problem.text =
            std::format("settle_runs must be at least 1, got {}", args.text("settle_runs"));
        return problem;
    }
    return std::nullopt;
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
    case Kind::OutOfRange:
        diag = DiagnosticBuilder::of(RuntimeDiag::InvalidPassParameter).commit(spec, problem.text);
        diag.stage = "typecheck";
        break;
    case Kind::Missing:
        diag = DiagnosticBuilder::of(SemanticDiag::ParamArgumentMissing)
                   .commit(problem.name, specName(spec));
        break;
    }
    // The whole specification for a problem that has no narrower place (a required parameter
    // left out, a specification that is not a call).
    const size_t length = problem.length > 0 ? problem.length : std::max<size_t>(spec.size(), 1);
    diag.range          = CharRange{{line, problem.column}, {line, problem.column + length}};
    diag.modulePath     = "<pipeline>";
    diag.moduleName     = "pipeline";
    return diag;
}

/// The `help` of a pass specification whose module did not load, in place of the paragraph about
/// how a pass specification is written (rule, "pass-unavailable-diagnostics" 1). The user did not
/// write the name wrongly: the module the name needs is not there to be loaded, and the
/// diagnostic's message already carries the loader's own first reason. What the help owes the
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
        module,
        reason);
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
    ensureCoreTagReaders();
    std::ostream &help = std::cerr;
    help << std::format("Pass <{}> not found, available passes are:\n", name);
    for (const auto &declaration : camel::execute::detail::passDeclarations("std", ctx)) {
        printPass(help, "std::" + declaration.name, declaration.doc, declaration.params);
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
            const auto declaration =
                camel::execute::detail::findPassDeclaration(module, function, ctx);
            printPass(
                help,
                path,
                declaration ? declaration->doc : std::string(),
                declaration ? declaration->params : descriptor.params);
        }
    }
    {
        // The test-only passes are registered like every other pass and are in no declaration file
        // of the standard library: they are listed from the registry, with whatever `test.cml` the
        // search path publishes for them says about their parameters where it says anything.
        help << std::format("Passes that exist for the tests only:\n");
        auto &registry = passImplementations();
        std::lock_guard lock(registry.mutex);
        for (const auto &[path, descriptor] : registry.passes) {
            if (path.rfind("test::", 0) != 0) {
                continue;
            }
            const std::string function = path.substr(path.find("::") + 2);
            const auto declaration =
                camel::execute::detail::findPassDeclaration("test", function, ctx);
            printPass(
                help,
                path,
                declaration ? declaration->doc : std::string(),
                declaration ? declaration->params : descriptor.params);
        }
    }
    help << std::format("Available aliases are:\n");
    for (const auto &[alias, target] : passAliases) {
        help << std::format("  {} -> {}\n", alias, target);
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
    std::string text = passes.size() == 1 ? "the pass " : "the passes ";
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
[[noreturn]] void throwUnresolvedPass(
    const std::string &spec, const ResolvedPass &resolved, size_t line, const context_ptr_t &ctx) {
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
        const RuntimeDiag code = missing.declaresUnexported ? RuntimeDiag::PassNotExportedByModule
                                                            : RuntimeDiag::PassNotProvidedByModule;
        auto diag  = DiagnosticBuilder::of(code).commit(missing.module, missing.function, provided);
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
        ctx,
        name.substr(0, sep),
        name.substr(sep + 2),
        problem,
        &errorLocation,
        &missing);
    resolved = resolvePass(spec, ctx);
    if (!resolved.factory) {
        if (!problem.empty()) {
            resolved.problem       = problem;
            resolved.errorLocation = errorLocation;
        } else if (missing.moduleHasNoSuchPass) {
            resolved.missingPass = ResolvedPass::MissingPass{
                name.substr(0, sep),
                name.substr(sep + 2),
                std::move(missing.provided),
                missing.declaresUnexported};
        }
    }
    return resolved;
}

namespace {

/// The graph id -> revision of every graph a program holds: what `programContentChanged`
/// compares. Read out before a pass runs rather than looked at afterwards, because a pass of the
/// core is another registered pass's caller (`std::cxx::algebra` runs the rules library's
/// `rules::simplify`, `std::cxx::saturate` its `opt::saturate`), and that inner pass
/// commits its result and releases the program the outer one was given
/// (`Context::settleRuntimeGraphs`). The old program is not read after `pass->apply` returns.
std::unordered_map<uint32_t, uint64_t> graphRevisionsOf(const camel::runtime::Program *program) {
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

/// Whether the node runs an effect. The answer is the node's effect fact
/// (`core.nodeEffects`): an effect is a write -- of a resource outside the program or of state the
/// program owns -- and a read is a dependency rather than an effect (design principles 3.3). This is
/// the tree's one judgement of it; a second reading of the operator table here would be a second
/// model to keep in step with that one.
bool nodeHasEffect(
    camel::runtime::fact::FactStore &facts, const camel::runtime::Program &program,
    const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = graph.node(ref);
    if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Oper) {
        return false;
    }
    return !camel::runtime::fact::nodeEffects(facts, program, graph, ref).pure();
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
    uint64_t copySite = 0;
    /// The node itself, and its place in its own graph's order: what the consistency check below
    /// reads to be sure a place it names is a place that holds the node.
    camel::runtime::gc_node_ref_t node = camel::runtime::kInvalidNodeRef;
    std::vector<std::string> resources; // the ambient resources it names, sorted and deduplicated
    /// The ambient resources and the way each is used: writing one is not reading it, and the
    /// boundary is what the program does to state it does not own. Kept beside `resources` because
    /// the two questions are asked separately -- which resources, and how.
    std::vector<std::pair<std::string, camel::core::sem::AccessMode>> uses; // name, way
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

/// Whether the effect `ref` stands on something in `graph` (design principles 3.1): a read of its
/// value -- a normal or `with` input of another node, or the graph's own result, which whoever runs
/// the graph reads -- or a place in its block's chain. The chain is carried as two edge kinds while
/// the migration runs (docs/design/effects/edges.cn.md 5.3: the sync edge replaces the control
/// one), so a place in it is a sync or control edge either way, read from the edges themselves.
///
/// An order edge is not one of them: it orders two nodes that both run and makes neither of them
/// run (edges.cn.md 1.5), so an effect whose only company is an order edge stands on nothing. Read
/// this way, this predicate is what refuses such a graph -- `test::order_only_effect` builds one on
/// purpose by stripping every other edge of an effect, and the refusal names what is missing.
///
/// An effect with none of these is in the graph and in no chain: nothing makes it run, which is
/// what a rewrite that removes its last reader and forgets to hand its place on leaves behind.
///
/// A write to a resource outside the program stands on the GATE of the region it is written in
/// (design principles 3.1): every region's exit is
/// a `GATE` whose inputs are the region's value and its writes, so such a write is read by the
/// GATE and stands on it as it stands on any other read of its value. What is left here is the two
/// edge readings, and the answer for a write is the graph's own: `Builder::layRegionWriteEdges` is
/// the one place that knows a region owes it.
bool effectStandsOnSomething(const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    if (!graph.normUsersOf(ref).empty() || !graph.withUsersOf(ref).empty()) {
        return true;
    }
    if (graph.exitNodeRef() == ref || graph.outputNodeRef() == ref ||
        graph.returnNodeRef() == ref) {
        return true;
    }
    if (!graph.syncPredsOf(ref).empty() || !graph.syncUsersOf(ref).empty() ||
        !graph.ctrlInputsOf(ref).empty() || !graph.ctrlUsersOf(ref).empty()) {
        return true;
    }
    return false;
}

/// The effects of `program`: every effect node of every graph of it, each at its position in one
/// topological order of its graph and each with the ambient resources it names.
ProgramEffects collectEffects(camel::runtime::fact::FactStore &facts, const camel::runtime::Program &program) {
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
                order.id,
                region->callSite};
        }
        for (size_t position = 0; position < order.nodes.size(); ++position) {
            const gc_node_ref_t ref = order.nodes[position];
            if (!nodeHasEffect(facts, program, *graph, ref)) {
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
                site.uses.emplace_back(access.resource.name, access.mode);
            }
            std::sort(site.resources.begin(), site.resources.end());
            site.resources.erase(
                std::unique(site.resources.begin(), site.resources.end()),
                site.resources.end());
            std::sort(site.uses.begin(), site.uses.end());
            site.uses.erase(std::unique(site.uses.begin(), site.uses.end()), site.uses.end());
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

/// The effects of the two programs, matched. The key is origin, inline copy site and operator
/// (`effectKey`), so a body a pass inlined **is** matched: the copies differ in the call site they
/// were copied into, and each pairs with the copy it came from. What is left out of every pair is
/// a key the program has more than once on either side -- two nodes of one origin, one copy site
/// and one operator, which is what a pass that clones a node in place leaves behind. There the
/// order between the clones would be a guess, and this function does not guess.
std::vector<std::pair<size_t, size_t>>
matchedEffects(const ProgramEffects &before, const ProgramEffects &after) {
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

std::string describeMoves(
    const EffectSite &a, const EffectSite &runsA, const EffectSite &b, const EffectSite &runsB) {
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

/// Where the place of a graph is, when it is not the graph itself: the graph that carries it and
/// the place in that graph of the node that runs it. A graph a pass inlined into another one has
/// its place at the call it replaced, so an effect in it runs at that call, not wherever its own
/// graph would have been laid out.
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
    bool ordered  = false;
    bool lifted   = false;
    size_t graph  = 0;
    uint64_t call = 0; // source origin of the call the lift went through, 0 for none
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
            const auto holder       = effects.graphAt.find(place.graph);
            const GraphOrder &order = effects.graphs[holder->second];
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
        const auto holder       = effects.graphAt.find(earlierPlace.graph);
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
///    them are placed by whichever order the graph was written out in, and a pass that writes it
///    out again may place them the other way without having reordered anything.
/// 2. The graph each of them is in after the pass. A pass that pulls an effect's node into another
///    graph takes its order with it, and a check that only read the order of a graph sees nothing:
///    the node is still in the program, it is the graph it runs in that moved.
/// The ambient resources the program touches must be the same before and after a pass. A pass moves
/// code, copies it, inlines a body, or deletes what nothing needs; none of that may add or remove
/// an effect on state the program does not own.
///
/// The boundary is the set of standard ambient resources *and the way each is used*: a pass that
/// turns a write of `stdout` into a read of it has changed what the program does to state it does
/// not own, and the names alone would not say so. The whole program is compared, not one graph at a
/// time: inlining moves an effect from a callee graph into its caller and deletes the callee, which
/// changes each graph's own resources and the program's none. The order of those effects is
/// `effectOrderViolation`; this is the other half of the boundary.
///
/// The name a resource use is reported by: `stdout (write)`, not `stdout`.
std::string useText(const std::string &name, camel::core::sem::AccessMode mode) {
    std::string out = name;
    out += " (";
    out.append(camel::core::sem::accessModeName(mode));
    out += ")";
    return out;
}

/// The standard ambient resources of a program and the way each is used, sorted and deduplicated:
/// what the boundary compares. A non-standard resource is the module's business, not this one's.
std::vector<std::string> ambientUsesOf(const ProgramEffects &effects) {
    std::vector<std::string> uses;
    for (const EffectSite &site : effects.sites) {
        for (const std::pair<std::string, camel::core::sem::AccessMode> &use : site.uses) {
            if (camel::core::sem::isStandardResource(use.first)) {
                uses.push_back(useText(use.first, use.second));
            }
        }
    }
    std::sort(uses.begin(), uses.end());
    uses.erase(std::unique(uses.begin(), uses.end()), uses.end());
    return uses;
}

/// The same list as one line: `nothing` for none.
std::string useListText(const std::vector<std::string> &uses) {
    if (uses.empty()) {
        return std::string("nothing");
    }
    std::string out;
    for (const std::string &use : uses) {
        if (!out.empty()) {
            out += ", ";
        }
        out += use;
    }
    return out;
}
/// The boundary itself: the standard ambient resources the program touches, and the way it uses
/// each, must be the same before and after a pass. A pass moves code, copies it, inlines a body, or
/// deletes what nothing needs; none of that may add, remove or re-aim an effect on state the
/// program does not own.
///
/// Returns nothing for a pass that kept them, or the two lists the diagnostic names.
std::vector<std::string>
effectResourcesViolation(const ProgramEffects &before, const ProgramEffects &after) {
    const std::vector<std::string> was = ambientUsesOf(before);
    const std::vector<std::string> is  = ambientUsesOf(after);
    if (was == is) {
        return {};
    }
    return {useListText(was), useListText(is)};
}

/// Every graph a pass returns keeps an entry and an exit: the node it starts at and the node whose
/// value it returns. A rewrite may replace either one, but the one it puts there must be a node of
/// the graph that is alive -- a graph whose entry or exit is gone is a graph nothing can run.
///
/// Returns nothing, or the graph and which of the two it lost.
std::string entryExitViolation(const camel::runtime::Program &program) {
    // The program's own boundary: the graph the pipeline starts from. A graph a module keeps for
    // itself (a rule library, an extracted body) need not be runnable on its own.
    const std::vector<camel::runtime::GCGraph *> graphs = {program.root()};
    for (camel::runtime::GCGraph *graph : graphs) {
        if (graph == nullptr) {
            continue; // a program without a root is what the next pass is given to fix
        }
        const auto alive = [&](camel::runtime::gc_node_ref_t ref) {
            if (ref == camel::runtime::kInvalidNodeRef || !graph->containsNodeRef(ref)) {
                return false;
            }
            const camel::runtime::GCNode *record = graph->node(ref);
            return record != nullptr && record->kind != camel::runtime::GCNodeKind::Dead;
        };
        const bool entry = alive(graph->entryNodeRef());
        const bool exit  = alive(graph->exitNodeRef());
        if (entry && exit) {
            continue;
        }
        return !entry ? "entry node it can start at" : "exit node whose value it returns";
    }
    return {};
}

/// Every operator node still matches the declaration it names: an operator the table knows,
/// ordinary ports that are live nodes with a type, and no more of them than the declaration's
/// operands. A port the declaration does not have, a port pointing at a dead node or at one without
/// a type, or a port whose node is gone is a call nothing can run.
///
/// One half of the last case is not seen here. A port naming a node that does not exist at all is
/// refused earlier, by the commit that would have to write it down
/// (`RuntimeGraphDraftSession::validateDraftPorts`): the graph version cannot be encoded around a
/// reference to nothing, and the encoder would read through it and crash before this function
/// ever ran. What reaches this function is a graph that exists, so it covers the node that is dead
/// (erased, still referenced) and the node that has no type.
///
/// Returns nothing, or the graph, the node and what is wrong with it.
std::vector<std::string> portDeclarationViolation(const camel::runtime::Program &program) {
    using camel::runtime::gc_node_ref_t;
    using camel::runtime::GCNodeKind;
    for (camel::runtime::GCGraph *graph : program.graphs()) {
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const gc_node_ref_t ref              = it.ref();
            const camel::runtime::GCNode *record = graph->node(ref);
            if (record == nullptr || record->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto *body = graph->nodeBodyAs<camel::runtime::GCOperBody>(ref);
            if (body == nullptr) {
                return {
                    std::format("graph {}", camel::runtime::toValue(graph->id())),
                    std::format("{}", static_cast<unsigned>(ref)),
                    "it carries no operator"};
            }
            if (!camel::core::OperatorTable::instance().lookup(body->uri()).has_value()) {
                return {
                    std::format("graph {}", camel::runtime::toValue(graph->id())),
                    std::format("{}", static_cast<unsigned>(ref)),
                    std::format("'{}' is not an operator the table knows", body->uri())};
            }
            for (gc_node_ref_t input : graph->normInputsOf(ref)) {
                if (!graph->containsNodeRef(input)) {
                    return {
                        std::format("graph {}", camel::runtime::toValue(graph->id())),
                        std::format("{}", static_cast<unsigned>(ref)),
                        "an ordinary port points at a node the graph does not have"};
                }
                const camel::runtime::GCNode *source = graph->node(input);
                if (source == nullptr || source->kind == GCNodeKind::Dead ||
                    source->dataType() == nullptr) {
                    return {
                        std::format("graph {}", camel::runtime::toValue(graph->id())),
                        std::format("{}", static_cast<unsigned>(ref)),
                        "an ordinary port points at a node that is gone or has no type"};
                }
            }
        }
    }
    return {};
}
/// Whether running the node is something the program observes: it touches state the program does
/// not own (an ambient resource: stdout, the filesystem, the clock, the environment, the
/// generator), or it writes state the program does own and that state is still there. Reading a
/// variable is not one of them, and neither is a computation whose value nobody reads: dropping one
/// changes nothing the program can see, which is exactly what a pass is allowed to do with it.
///
/// A write is not judged by the set of writes the program was given but by the graph the pass
/// returns: a write that is no root of its graph and that no port reads is a dead write
/// (`camel::runtime::deadWrite`, which is where the judgement lives so that a pass and a test can
/// ask it on its own), and a dead write is not an effect. Without that reading, a rewrite that
/// drops the last closure capturing a variable cannot leave that variable's last write behind
/// (design principles 9.7). This is the line the check below draws, and it is what makes it a check
/// about *effects* rather than about every node: without it the invariant would ask a pure
/// operation for a credential it never needs, and the first graph a program builds would fail it.
bool nodeRunsForItsEffect(
    camel::runtime::fact::FactStore &facts, const camel::runtime::Program &program,
    const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    if (!nodeHasEffect(facts, program, graph, ref)) {
        return false;
    }
    const camel::runtime::NodeResources resources =
        camel::runtime::resourcesOf(program, graph, ref);
    if (resources.external) {
        return true;
    }
    bool writes = false;
    for (const camel::runtime::NodeAccess &access : resources.accesses) {
        if (access.resource.cls == camel::core::sem::ResourceClass::Ambient) {
            return true;
        }
        if (access.mode != camel::core::sem::AccessMode::Read) {
            writes = true;
        }
    }
    if (!writes) {
        return false;
    }
    return !camel::runtime::deadWrite(graph, ref);
}

/// Every effect a pass hands back stands on one of the credentials design principles 3.1 gives an
/// effect: a read of its value (a normal or `with` input of another node, or the graph's own
/// result, which whoever runs the graph reads), a place in its block's chain, in the spellings the
/// graphs carry the chain in while the migration runs, or the region it is written in
/// (`effectStandsOnSomething`).
///
/// An effect that stands on neither is in the graph and in no chain: nothing makes it run, so the
/// program the pass returned is not the program it was given (3.1, 3.3). That is the whole check --
/// it asks the question of every effect of the program a pass returns, and not only of the ones the
/// pass touched, because a node left behind by an earlier round is just as lost as one dropped now.
/// The question is asked only of effects, by `nodeRunsForItsEffect`: a pure operation that nothing
/// reads is a computation the program does not observe, and a pass may drop it.
///
/// Returns the first violation as the node it belongs to -- its graph, its id, the operator it runs
/// and where the program wrote it, which is the smallest program that shows the defect -- and the
/// origin to point the diagnostic at; empty when every effect of every graph stands on something.
struct EffectCredentialViolation {
    std::string text;
    uint64_t origin = 0;
};

EffectCredentialViolation
effectCredentialViolation(const camel::runtime::Program &program, const context_ptr_t &context) {
    if (context == nullptr) {
        // The credential is read through the facts of the run's store, and without a context there
        // is none to ask. The caller that has no context reports no diagnostic either, so there is
        // nothing this reading would be the evidence of.
        return {};
    }
    for (const camel::runtime::GCGraph *graph : program.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        for (uint32_t index = 0; index < graph->nodeCount(); ++index) {
            const camel::runtime::gc_node_ref_t ref =
                static_cast<camel::runtime::gc_node_ref_t>(index);
            const camel::runtime::GCNode *node = graph->node(ref);
            // A removed node leaves a record of kind `Dead` and no inputs (graph.h, GCNodeKind); it
            // is not part of the graph a pass returned.
            if (node == nullptr || node->kind == camel::runtime::GCNodeKind::Dead ||
                !nodeRunsForItsEffect(context->facts(), program, *graph, ref) ||
                effectStandsOnSomething(*graph, ref)) {
                continue;
            }
            EffectCredentialViolation violation;
            violation.origin      = graph->nodeOrigin(ref);
            std::string writtenAt = std::format("origin {}", violation.origin);
            if (violation.origin != 0 && context != nullptr &&
                context->sourceContext() != nullptr) {
                const auto range = context->sourceContext()->resolveOrigin(violation.origin);
                writtenAt        = std::format(
                    "{}:{}:{}",
                    context->sourceContext()->pathForOrigin(violation.origin),
                    range.start.line,
                    range.start.character);
            }
            violation.text = std::format(
                "graph '{}' has effect node {} ('{}') with neither a read of its value nor a place "
                "in its chain, written at {}",
                graph->name(),
                static_cast<unsigned>(ref),
                std::string(graph->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri()),
                writtenAt);
            return violation;
        }
    }
    return {};
}
std::string effectOrderViolation(
    const ProgramEffects &before, const ProgramEffects &after,
    const std::vector<std::pair<size_t, size_t>> &pairs) {
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
            const OrderVerdict written    = orderedInProgram(before, earlier, later);
            if (!written.ordered) {
                continue; // no chain before the pass: the program did not put them in this order
            }
            const OrderVerdict kept = orderedInProgram(after, runsEarlier, runsLater);
            if (kept.ordered) {
                continue; // the program's order still holds, in one graph or through the calls
            }
            const std::string moves = describeMoves(earlier, runsEarlier, later, runsLater);
            const char *writtenIn   = written.lifted ? " (read through the call that runs it)" : "";
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

/// A node as a diagnostic names it: the graph it is in, the node, how the graph spells it and,
/// when the graph's source names it, that name. The reader is looking at a program, so a bare
/// number would not be enough and the graph's name is what one searches for.
std::string describeNodeOf(
    const camel::runtime::Program &program, const camel::runtime::GCGraph &graph,
    camel::runtime::gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = graph.node(ref);
    const std::string_view named       = graph.nodeName(ref);
    std::string what                   = node == nullptr ? std::string("a node") : std::string();
    if (node != nullptr) {
        if (node->kind == camel::runtime::GCNodeKind::Func ||
            node->kind == camel::runtime::GCNodeKind::Call) {
            const camel::runtime::GCGraph *callee = program.calleeOf(graph, ref);
            what = callee == nullptr ? std::string("a call") : "a call of '" + callee->name() + "'";
        } else if (node->kind == camel::runtime::GCNodeKind::Oper) {
            if (const auto *body = graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)) {
                what = std::string(body->uri());
            } else {
                what = "an operator";
            }
        }
    }
    if (!named.empty()) {
        return std::format(
            "node {} of graph '{}' ({}, which is '{}')",
            ref,
            graph.name(),
            what,
            named);
    }
    return std::format("node {} of graph '{}' ({})", ref, graph.name(), what);
}

/// The one-line text the effect-containment diagnostic carries: the call site, the type it
/// declares as the declaration spells it, every access the type does not cover and -- when the
/// footprint reaches a resource no clause can name -- the node inside the body that makes that
/// reading and why it cannot be named: an access the check cannot name is refused, so the
/// diagnostic has to say which node and why.
std::string effectContainmentText(
    const camel::runtime::Program &program, const camel::runtime::GCGraph &graph,
    const camel::runtime::EffectContainment &violation) {
    std::string beyond;
    for (const std::string &access : violation.beyond) {
        beyond += (beyond.empty() ? "" : ", ") + access;
    }
    std::string text = std::format(
        "{} declares {}{}",
        describeNodeOf(program, graph, violation.call),
        violation.declared.empty() ? std::string("nothing") : violation.declared,
        beyond.empty() ? std::string() : std::format("; the graph it runs touches {}", beyond));
    if (violation.body != nullptr && violation.unnamed != camel::runtime::kInvalidNodeRef) {
        text += std::format(
            "; no clause can name what {} reaches: {}",
            describeNodeOf(program, *violation.body, violation.unnamed),
            violation.unnamedWhy);
    }
    return text;
}

/// The graphs a pass brought with it.
///
/// A pass rewrites the program it is given. What it hands back may hold the graphs that program
/// held and the graphs its own edits made -- and nothing else: `graphs(produced) ⊆ graphs(given) ∪
/// made`. So a graph in the produced table that is neither one the given program held nor one the
/// commit recorded as made (`Program::madeGraphs`) is one the pass *introduced*, and a program can
/// only gain a graph it did not make by adopting another program's table (`Program::adoptMissing`,
/// reached from the draft session when a reference names a graph only the program the pass runs in
/// holds).
///
/// `made` comes from the commit's own record (`RuntimeGraphDraftSession::madeGraphIds`, written on
/// the program the commit hands back), never from the difference of the two tables: `produced -
/// given` as the definition of `made` would make this check true by construction. When the produced
/// program carries no record at all -- no commit of that kind made it -- the check must not fall back
/// to the difference: it says the commit recorded nothing it made and refuses on that reading.
///
/// Naming them, not counting them, is the reading: a count says how much came in, and the names say
/// whose it is. Measured with `opt::cse`: a couple of hundred graphs are reached
/// from the produced root (the pass's own module graphs, `opt::evaluated` among them), but the
/// produced *table* holds only the ones the given program held: what counts here is the table, and
/// the graphs a pass reached through a function value are not graphs it adopted.
///
/// What the given program held is read out before the pass runs (`GraphIdsHeld`), not read from its
/// `Program` afterwards: the pass replaces the context's root while it runs, and `settle` then drops
/// the pipeline's reservations, so nothing roots the program a pass was given once it has returned
/// (src/kernel/runtime/program.cpp, `GCGraphManager::settle`). The check compares ids, and names the
/// introduced graphs from the produced table, which is the one that is alive here.
using GraphIdsHeld = std::unordered_set<uint32_t>;

GraphIdsHeld graphIdsHeld(const camel::runtime::Program &program) {
    GraphIdsHeld ids;
    for (const GCGraph *graph : program.graphs()) {
        if (graph != nullptr) {
            ids.insert(camel::runtime::toValue(graph->id()));
        }
    }
    return ids;
}

std::string introducedGraphsViolation(
    const GraphIdsHeld &given, const camel::runtime::Program &produced) {
    std::vector<const GCGraph *> introduced;
    for (const GCGraph *graph : produced.graphs()) {
        if (graph == nullptr) {
            continue;
        }
        const uint32_t id = camel::runtime::toValue(graph->id());
        if (given.contains(id)) {
            continue;
        }
        // `made` is consulted only when the program says a commit recorded it; a program with no
        // record is not read as one that made nothing -- that is the blindness reported below.
        if (produced.hasMadeGraphs() && produced.madeGraphs().contains(id)) {
            continue;
        }
        introduced.push_back(graph);
    }
    if (introduced.empty()) {
        return "";
    }
    constexpr size_t kNamesShown = 4;
    std::string text =
        produced.hasMadeGraphs()
            ? std::format(
                  "the program the pass hands back holds {} graph(s) it was not given, and a pass "
                  "may only hand back the graphs it was given and the ones its own edits made",
                  introduced.size())
            : std::format(
                  "the program the pass hands back holds {} graph(s) it was not given, and no commit "
                  "recorded the graphs it made, so the check cannot say which of them are the pass's "
                  "own edits",
                  introduced.size());
    text += "; the first are named";
    for (size_t i = 0; i < introduced.size() && i < kNamesShown; ++i) {
        text += std::format(" '{}'", introduced[i]->name());
    }
    if (introduced.size() > kNamesShown) {
        text += std::format(" (and {} more)", introduced.size() - kNamesShown);
    }
    return text;
}

/// Every generic pass, including a registered nested pass, checks operator ports, entry/exit
/// and effect credentials. The comparative resource-support and matched-order predicates run
/// only when selected: by the optimizer's internal checks or graph::check_pass_boundary.
/// A function-to-function type does not select preservation by itself.
///
/// The commit checks of docs/design/effects/analysis.cn.md 4.1 are asked when `commitChecks` is
/// set: a pass that rewrites through the graph module alone cannot build an effectful node, so for
/// it "no extra effect, no lost order" is a theorem(see
/// GraphIRPass::rewritesThroughTheGraphModuleOnly). Whether that theorem is read as a theorem or
/// checked is the caller's answer and it is a build's: `commitChecksAskedOf` below. Effect
/// containment is the item of 4.1 this commit lands; order completeness is the other one and is not
/// here yet.
///
/// Returns true when it refused: the diagnostic is recorded, and the caller fails the run.
bool passBoundaryViolation(
    const context_ptr_t &ctx, const std::string &spec, const camel::runtime::Program &program,
    const ProgramEffects &effectsBefore, bool preserveEffects = false, bool commitChecks = true,
    const GraphIdsHeld *given = nullptr) {
    // The graphs the pass brought with it, before anything about their content: a pass that hands
    // back a program holding graphs it was not given has already answered the wrong question, and
    // the node-level readings below would be read against a program that is not the user's.
    if (given != nullptr) {
        const std::string introduced = introducedGraphsViolation(*given, program);
        if (!introduced.empty()) {
            ctx->rtmDiags()->of(RuntimeDiag::PassInternalError).commit(spec, introduced);
            CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (introduced graphs)", spec);
            return true;
        }
    }
    const ProgramEffects effectsAfter = collectEffects(ctx->facts(), program);
    const std::vector<std::pair<size_t, size_t>> pairs =
        matchedEffects(effectsBefore, effectsAfter);
    const std::string violation = effectOrderViolation(effectsBefore, effectsAfter, pairs);
    if (preserveEffects && !violation.empty()) {
        ctx->rtmDiags()->of(RuntimeDiag::PassInternalError).commit(spec, violation);
        CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (effect order)", spec);
        return true;
    }
    const std::vector<std::string> resources =
        effectResourcesViolation(effectsBefore, effectsAfter);
    if (preserveEffects && !resources.empty()) {
        ctx->rtmDiags()
            ->of(RuntimeDiag::PassBoundaryResources)
            .commit(spec, resources[0], resources[1]);
        CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (effect resources)", spec);
        return true;
    }
    const std::vector<std::string> ports = portDeclarationViolation(program);
    if (!ports.empty()) {
        ctx->rtmDiags()->of(RuntimeDiag::PassBoundaryPorts).commit(spec, ports[1], ports[2]);
        CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (ports)", spec);
        return true;
    }
    const std::string boundary = entryExitViolation(program);
    if (!boundary.empty()) {
        ctx->rtmDiags()->of(RuntimeDiag::PassBoundaryNodes).commit(spec, "graph", boundary);
        CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (entry/exit)", spec);
        return true;
    }
    // Every effect in the program the pass returns stands on a read of its value or on a place
    // in its chain (design principles 3.1, 3.3 as amended). This is asked of the whole
    // program, not only what the pass touched. An effect retained after removing its last
    // reader still needs a credential; a preserving transformation may route it through
    // a `GATE` that reads it. A selected intentional deletion may instead erase the
    // genuinely undemanded node. Neither route may leave an orphan effect behind.
    const EffectCredentialViolation credential = effectCredentialViolation(program, ctx);
    if (!credential.text.empty()) {
        auto diag = ctx->rtmDiags()->of(RuntimeDiag::PassInternalError);
        if (credential.origin != 0) {
            diag.atOrigin(credential.origin);
        }
        diag.commit(spec, credential.text);
        CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (effect credential)", spec);
        return true;
    }
    if (commitChecks) {
        // Analysis 4.1, effect containment: at each call site the footprint of the function value
        // called must not exceed what the type at the call site declares. The message names the
        // call node and the accesses beyond its type, because those two are what a reader has to
        // look at: the type is the promise a caller relies on. When the footprint reaches a
        // resource no clause can name, the message names the node inside the body that makes that
        // reading too, and says why it cannot be named: that reading is refused rather than
        // counted, so the node is what the reader has to go and fix.
        //
        // The check also says what it declined to judge. That reading is a diagnostic line on the
        // `Pass` scope and not an environment switch, because a count decides only how much is
        // printed: the log threshold is a command-line flag (`-vv`, or `--log-include Pass` for the
        // one scope), and the environment is not where a run's settings come from -- the same rule
        // as pass settings being pass parameters (AGENTS.md). A count is how "I looked and it is
        // fine" is told apart from "I refused to look": a decline is the analysis saying it cannot
        // name the resource, and a number that only ever grows is how that shows up. A run that
        // does not ask pays nothing for it, because the arguments are not formatted unless the line
        // is emitted. The `any unknown` component is the one that is no longer a decline -- it
        // refuses -- and it is still printed so that a refused run's summary says how many such
        // readings the refusal was about.
        camel::runtime::EffectContainmentReport report;
        const camel::runtime::GCGraph *refused = nullptr;
        std::optional<camel::runtime::EffectContainment> violation;
        for (const camel::runtime::GCGraph *graph : program.graphs()) {
            if (graph == nullptr) {
                continue;
            }
            auto found = camel::runtime::effectContainmentViolation(program, *graph, &report);
            if (found.has_value() && !violation.has_value()) {
                refused   = graph;
                violation = std::move(found);
            }
        }
        // A reading of the effect-containment summary, on the run's tracing channel
        // (`--trace effects`) and not on a log scope: one kind of reading, one way to ask for it.
        // The text is that of a log line, down to the trailing newline.
        camel::core::trace(camel::core::TraceArea::Effects) << std::format(
            "run | passes | effect containment: checked {} call site(s), declined {} reading(s) "
            "(any unknown {} / any object {} / any handle {} / variable cell {} / unresolved {} / "
            "other {})\n",
            report.checked,
            report.declined(),
            report.anyUnknown,
            report.anyObject,
            report.anyHandle,
            report.variableCell,
            report.unresolved,
            report.other);
        if (violation.has_value()) {
            ctx->rtmDiags()
                ->of(RuntimeDiag::PassEffectContainment)
                .commit(spec, effectContainmentText(program, *refused, *violation));
            CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (effect containment)", spec);
            return true;
        }
    }
    return false;
}
} // namespace

namespace {

/// What a pass gave up with, as the diagnostic that says so. One function, because two places
/// receive it: where a pass runs (`apply` threw) and where a pass is built (`factory(ctx)` threw).
/// A pass's parameters are checked while it is constructed, and that check reports through the same
/// runtime diagnostics the pass uses when it runs -- so the two places have the same job, and a
/// second copy of this would be a second answer to what a pass's failure is called.
void reportPassFailure(const context_ptr_t &ctx, const std::string &name, const std::exception &e) {
    // A pass that gives up with a plain exception (graph validation at commit, the execution-order
    // builder meeting a cycle, an arena limit) hit a defect of the compiler, not of the program. It
    // is still reported as a diagnostic, so that --output-format json stays a valid diagnostics
    // array. There is no source position for it. The stage names the pass even for a scheduling
    // pass: the failure belongs to the pass, whatever part of the program had run.
    setPipelineStage("pass:" + specName(name));
    // One exception is not that: a graph whose exit hands on a value of another type than its own
    // signature declares is the program's own rewrite having written the wrong value (the seal's
    // check, graph_edges.cpp `checkReturnValueType`), so it is reported as itself -- the graph, the
    // two types, the value and the value's position -- and the reader is told what in the program
    // to change. `PassInternalError` stays what it says: a pass that broke the graph it was given.
    if (const auto *mismatch = dynamic_cast<const camel::runtime::GraphExitTypeMismatch *>(&e)) {
        try {
            auto diag = ctx->rtmDiags()->of(RuntimeDiag::ExitTypeMismatch);
            if (mismatch->origin() != camel::source::kInvalidOriginId) {
                // The value's own position: the expression that builds it. A node a rewrite built
                // records no origin, and then the message names the node and the value instead.
                diag.atOrigin(mismatch->origin());
            }
            diag.commit(
                mismatch->graphName(),
                mismatch->valueType(),
                mismatch->declaredType(),
                mismatch->whereInGraph());
        } catch (const camel::core::error::DiagnosticsLimitExceededBaseException &) {
            // The diagnostic is recorded; the limit is what throws (see below).
        }
        CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (exit type)", specName(name));
        return;
    }
    std::string what = e.what();
    while (!what.empty() && (what.back() == '.' || what.back() == '\n')) {
        what.pop_back(); // the diagnostic's message ends the sentence itself
    }
    try {
        ctx->rtmDiags()->of(RuntimeDiag::PassInternalError).commit(name, what);
    } catch (const camel::core::error::DiagnosticsLimitExceededBaseException &) {
        // The diagnostic is recorded; the limit is what throws. Letting it out of a catch handler
        // is how this became `terminate` in the first place.
    }
}

/// Build a pass, and receive what its construction reports.
///
/// Construction is where a pass's parameters are checked: `std::opt settle_runs=0` is refused in
/// the maker, with `RuntimeDiag::InvalidArgument` naming the pass and the value. That diagnostic is
/// committed to the context like any other, and at the context's diagnostics limit the commit
/// throws
/// -- which, escaping from a construction site, reached the driver and became `std::terminate` with
/// exit code 134, for a program the compiler had refused correctly.
///
/// So a construction site receives too, and it receives the same way the running site does: a throw
/// from the diagnostics limit means the diagnostic is already recorded, and the caller's
/// `hasErrors()` is what turns it into a refusal with a non-zero exit; anything else is a pass that
/// broke while being built, which `reportPassFailure` reports exactly as a break while running.
std::unique_ptr<GraphIRPass> constructPass(const context_ptr_t &ctx, const std::string &name) {
    // A pass is built more than once on the way in -- twice by the pre-checks that ask what kind it
    // is, and once when the pipeline builds it. A refused parameter is recorded the first time, so
    // every later construction would record the same diagnostic again: the run reported
    // 'settle_runs must be at least 1, got 0' twice before this. Once a diagnostic is recorded,
    // nothing else is built.
    if (ctx && ctx->rtmDiags()->hasErrors()) {
        return nullptr;
    }
    auto resolved = resolvePassWithModule(name, ctx);
    if (!resolved.factory) {
        return nullptr;
    }
    try {
        return resolved.factory(ctx);
    } catch (const camel::core::error::DiagnosticsLimitExceededBaseException &) {
        return nullptr;
    } catch (const std::exception &e) {
        reportPassFailure(ctx, name, e);
        return nullptr;
    }
}

} // namespace

namespace {

/// Whether the commit checks of analysis 4.1 are asked of a pass that rewrites through the graph
/// module alone. The pass answers a fact (`rewritesThroughTheGraphModuleOnly`, true for every pass
/// written in Camel, src/execute/camel_pass.cpp); whether the two checks are asked of it is this
/// question, and it is a build question: such a pass cannot build the defect, so the saving is
/// kept only where it is
/// paid for -- a shipped build does not ask, a debug build asks, and a test asks
/// (`setCamelPassCommitChecksEnabled`). The theorem is why the shipped build may skip; reading it
/// in the builds that have the suite is how anyone knows the theorem is still true.
///
/// The answer itself is one cell for both commit paths, and it lives in the runtime beside the
/// reading it asks for (camel/runtime/resources.h, `commitChecksEnabled`); this is the pass
/// layer's name for it, and `camelPassCommitChecksEnabled` below is the same cell.
bool commitChecksAskedOf(const GraphIRPass &pass) {
    return !pass.rewritesThroughTheGraphModuleOnly() || camelPassCommitChecksEnabled();
}

} // namespace

bool camelPassCommitChecksEnabled() {
    // One cell with the runtime's, because it is one question: `std::nvm`-time commits ask it too
    // (camel/runtime/resources.h, `commitChecksEnabled`), and a pass driver whose switch answered
    // differently from the commit under it would make "asked" mean two things in one run.
    return camel::runtime::commitChecksEnabled();
}

void setCamelPassCommitChecksEnabled(bool enabled) {
    camel::runtime::setCommitChecksEnabled(enabled);
}

PassRunResult applyRegisteredPass(
    const camel::runtime::Program *program, const std::string &spec, const context_ptr_t &ctx,
    std::ostream &os, GraphIRPass *caller) {
    ResolvedPass resolved = resolvePassWithModule(spec, ctx);
    if (!resolved.factory) {
        // No pipeline source here (a pass run by name from a tool): the first line is the call.
        throwUnresolvedPass(spec, resolved, 0, ctx);
    }
    const bool publishes = ctx && ctx->currentRuntimeProgram() == program;
    // The old program's content is read here, before the pass runs (graphRevisionsOf), and so are
    // the graph ids it held (`graphIdsHeld`): the program a pass returns is not read once it has
    // returned, and the pass may have replaced the context's root with the program it hands back.
    const std::unordered_map<uint32_t, uint64_t> beforeRevisions = graphRevisionsOf(program);
    const GraphIdsHeld givenGraphs = program != nullptr ? graphIdsHeld(*program) : GraphIdsHeld{};
    auto pass                                                    = constructPass(ctx, spec);
    if (!pass) {
        // Construction reported: the diagnostic is recorded, and the caller prints it.
        return {nullptr, true};
    }
    // The program this pass is given, read before it runs: the boundary check below compares the
    // effects of the two, and the program a pass returns is not read once it has returned. With no
    // context there is no store to ask and no diagnostic to report a violation with (the check
    // below is skipped in that case), so the effects are not read either.
    const ProgramEffects effectsBefore = program != nullptr && ctx != nullptr
                                             ? collectEffects(ctx->facts(), *program)
                                             : ProgramEffects{};
    const camel::runtime::Program *produced = pass->apply(program, os);
    if (pass->hasErrorValue()) {
        if (caller != nullptr) {
            caller->failWithErrorValue(pass->errorValue());
        } else {
            // A pass is `(f: F) => F!` (design principles 9.1): the value it ends with instead of a
            // program IS its answer, and handing it to the caller is how that answer travels. With
            // no caller there is nobody to hand it to, and dropping it would let the run
            // go on as if the pass had succeeded. Nothing reaches this branch today (both call
            // sites pass `this`), and the branch is here so that a future one cannot be silent.
            if (ctx) {
                ctx->rtmDiags()
                    ->of(RuntimeDiag::PassInternalError)
                    .commit(
                        spec,
                        "the pass ended with an error value and the caller that ran it has none to "
                        "give it to");
            }
            return {nullptr, true};
        }
    }
    if (produced == nullptr) {
        return {nullptr, true};
    }
    // The check every pass ends with, for a pass a pass runs by name.
    if (ctx != nullptr && passBoundaryViolation(
                              ctx,
                              spec,
                              *produced,
                              effectsBefore,
                              false,
                              commitChecksAskedOf(*pass),
                              program != nullptr ? &givenGraphs : nullptr)) {
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

bool checkPassBoundary(
    const context_ptr_t &ctx, const std::string &spec, const camel::runtime::Program &before,
    const camel::runtime::Program &after) {
    return passBoundaryViolation(ctx, spec, after, collectEffects(ctx->facts(), before), true);
}

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
/// before, unless the writer named it already, and nothing at all when the pass it belongs before
/// is not in the plan -- the pipeline runs what the program asked for, plus the steps that pass
/// needs.
std::vector<std::string> expandFixedLoweringSteps(const std::vector<std::string> &passes) {
    std::vector<std::string> plan = passes;
    for (const FixedLoweringStep &step : fixedLoweringSteps) {
        // The comparison is made on the names the specifications resolve to, not on the words the
        // writer used: the optimizing pipeline answers to two names (`std::cxx::opt`, and
        // `std::opt`, the default pipeline's name for it), and a step that the optimizer is
        // written against belongs before either of them. Comparing the written word would insert
        // the step for one spelling and silently skip it for the other.
        const auto target = std::find_if(plan.begin(), plan.end(), [&](const std::string &spec) {
            return resolveAlias(specName(spec)) == resolveAlias(step.before);
        });
        if (target == plan.end()) {
            continue;
        }
        const bool named = std::any_of(plan.begin(), plan.end(), [&](const std::string &spec) {
            return resolveAlias(specName(spec)) == resolveAlias(step.pass);
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
        const auto pass = constructPass(ctx, passes[i]);
        if (!pass) {
            continue; // construction reported; the pipeline build below is where it stops
        }
        if (dynamic_cast<GraphSchedulePass *>(pass.get()) != nullptr) {
            return i;
        }
    }
    return std::nullopt;
}

bool passListEndsWithoutExecuting(const std::vector<std::string> &passes, const context_ptr_t &) {
    if (passes.empty()) {
        return false; // the fallback scheduling pass runs the program
    }
    // A scheduling pass that produces no effect executes nothing, so a list that ends in it
    // executes nothing. Every other scheduling pass runs the program, and a translation pass hands
    // the graph on to whatever follows -- the fallback when nothing does.
    //
    // Which pass that is comes from the pass itself (`NopPass::kName`, `executesTheProgram`), not
    // from a spelling written here: this question is asked before the program is compiled and so
    // before any pass can be built, and it must not be the one place that answers "does this pass
    // execute" a second time.
    return resolveAlias(specName(passes.back())) == NopPass::kName;
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
std::optional<Diagnostic>
jitWithoutCodeGeneratorWarning(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
    (void)ctx;
#if ENABLE_FASTVM_JIT
    (void)passes;
    return std::nullopt;
#else
    for (const std::string &spec : passes) {
        if (resolveAlias(specName(spec)) != "std::fastvm::jit") {
            continue;
        }
        auto warning  = DiagnosticBuilder::of(RuntimeDiag::JitWithoutCodeGenerator).commit();
        warning.stage = "pass:" + specName(spec);
        return warning;
    }
    return std::nullopt;
#endif
}

namespace {

/// The Camel passes of the pipeline that declare `#reads(...)` on their function, by the tag
/// they name: a pass written in Camel reads tags by saying so, the counterpart of a C++
/// module's `read_by` (design principles 6.2). The module is loaded here because the warning
/// is computed before the pipeline builds the passes.
std::map<std::string, std::vector<std::string>>
declaredReadersOf(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
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
            const auto value = camel::core::attr::valueOf(facts, camel::core::attr::key("reads"));
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

std::vector<Diagnostic>
tagsNeverReadWarnings(const std::vector<std::string> &passes, const context_ptr_t &ctx) {
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
    ensureCoreTagReaders();
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
        auto warning  = DiagnosticBuilder::of(SemanticDiag::TagHasNoReader)
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
            auto pass = constructPass(ctx, p);
            if (!pass) {
                return {nullptr, PassApplyStatus::Failed};
            }
            // The debugger's observer is told which pass runs: a scheduling pass that executes the
            // program it debugs does, and a rewrite or translation pass may evaluate graphs of its
            // own. `std::nop` is a scheduling pass that does not execute, and the observer is told
            // what happens rather than what kind of pass it is.
            const auto *scheduling = dynamic_cast<GraphSchedulePass *>(pass.get());
            // The one criterion: does *this* pass execute the program. It is asked here once and
            // read below for both consequences -- the observer above and the entry's arguments.
            const bool executes = scheduling != nullptr && scheduling->executesTheProgram();
            if (auto *observer = camel::execute::executionObserver()) {
                observer->passStarting(executes, program->root());
            }
            // Diagnostics raised while this pass runs carry its stage: "run" for a scheduling pass
            // (the program executes there), "pass:<name>" for rewrite and translation passes. The
            // stage keeps naming the kind, not the run: a scheduling pass's diagnostics belong to
            // the run it is about even when it ends the pipeline without one.
            struct StageScope {
                explicit StageScope(std::string stage) { setPipelineStage(std::move(stage)); }
                ~StageScope() { setPipelineStage(""); }
            } stageScope(scheduling != nullptr ? std::string("run") : "pass:" + specName(p));
            // A program runs only when every parameter of `main` has a value. Parameters
            // left open are fine for rewriting and translation (they are the inputs of what
            // is translated); nothing may execute with a slot nobody filled. The question is
            // whether *this* pass executes the program, and a scheduling pass that ends the
            // pipeline without a run (`std::nop`) does not: it is not an entry, so its arguments
            // are not asked of it.
            if (executes) {
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
            // A program with graphs that have several writings per value: a pass that consumes it (a back
            // end, an exporter) is not given the program and nothing extracts it on the way. What a
            // writing costs is the target library's judgement, so the pipeline must say it
            // (`rewrite::extract cost=...`) before the pass that needs one writing per value: there
            // is no implicit extraction, because the kernel has no cost of its own to extract by.
            if (camel::runtime::hasWritings(*program) && !pass->acceptsWritings()) {
                setPipelineStage("pass:" + specName(p));
                ctx->rtmDiags()->of(RuntimeDiag::PassNeedsExtraction).commit(p);
                return {nullptr, PassApplyStatus::Failed};
            }
            // The effects the program writes, and the order it writes them in, before the pass
            // runs: the effects on one resource keep that order through every pass (design
            // principles 3.3), and the end of this iteration compares. Read here rather than after
            // the pass because the program the pass is given is not read once it has returned.
            const ProgramEffects effectsBefore = collectEffects(ctx->facts(), *program);
            // The graphs the program holds, kept to the boundary check below, which names the graphs
            // the pass hands back and the program did not hold (`introducedGraphsViolation`). The
            // ids are read here because the program a pass is given is not read once it has
            // returned: the pass replaces the context's root while it runs and `settle` then drops
            // what the pipeline reserved, so no pointer to it is alive at the check.
            const GraphIdsHeld given = graphIdsHeld(*program);
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
                // limit the program (or what the pass made of it) ran into: it is reported with the
                // space, the usage, the limit and the setting that raises it, at the graph that was
                // being built when one is known.
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
                // A commit that refused one of the checks a commit answers is not a defect of the
                // compiler: it is the reading's own diagnostic, the same one either commit path
                // asks (camel/runtime/resources.h, `commitViolation`), carrying the graph, the
                // nodes and the source position it named.
                if (const auto *refused =
                        dynamic_cast<const camel::runtime::GraphCommitRefused *>(&e)) {
                    setPipelineStage("pass:" + specName(p));
                    camel::runtime::reportCommitViolation(ctx, p, refused->violation());
                    CAMEL_LOG_INFO_S("Pass", "run | passes | FAIL {} (commit refused)", p);
                    return {nullptr, PassApplyStatus::Failed};
                }
                // The same receiver a construction site uses: one answer to "what is a pass's
                // failure called", whether the pass broke while being built or while running.
                reportPassFailure(ctx, p, e);
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
            // Generic admission checks ports, root entry/exit and effect credentials, as does
            // applyRegisteredPass. Resource support and matched ordering are compared only by
            // an explicitly selected checker; this function-to-function result implies neither.
            if (passBoundaryViolation(
                    ctx,
                    p,
                    *program,
                    effectsBefore,
                    false,
                    commitChecksAskedOf(*pass),
                    &given)) {
                return {nullptr, PassApplyStatus::Failed};
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
