// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The definition graphs of operators: building them from their libraries, the checks, and the
 * expansion of a node (see definitions.h).
 */

#include "camel/runtime/definitions.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/encode.h"
#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/module/userdef.h"
#include "camel/core/node_attrs.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/symbol.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/fact_persist.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/program.h"
#include "camel/runtime/reachable.h"
#include "core/module/entity_internal.h"

#include <format>
#include <functional>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

namespace core = camel::core;
namespace sem  = camel::core::sem;
using camel::core::module::UserDefinedModule;

namespace {

/// The first line of a module diagnostic.
std::string firstLine(const std::string &message) {
    return message.substr(0, message.find('\n'));
}

/// The reason a library could not be loaded, as a pass specification states it: the first line of
/// the module diagnostic, without the "Module 'm' not found." lead (the specification already
/// names the pass and its library, and a module the loader found is not "not found") and without
/// a trailing lead-in to a list that the one-line form does not show ("Search paths tried:").
std::string loadFailureReason(const std::string &name, const std::string &message) {
    std::string reason     = firstLine(message);
    const std::string lead = std::format("Module '{}' not found. ", name);
    if (reason.starts_with(lead)) {
        reason.erase(0, lead.size());
    }
    if (reason.ends_with(':')) {
        const size_t cut = reason.rfind(". ");
        reason           = cut == std::string::npos ? std::string{} : reason.substr(0, cut + 1);
    }
    if (reason.empty()) {
        reason = firstLine(message);
    }
    // The template that carries this reason ends the sentence with a period of its own.
    while (!reason.empty() && reason.back() == '.') {
        reason.pop_back();
    }
    return reason;
}

/// The first error of a module's diagnostics, as one line.
std::string firstError(const UserDefinedModule &module) {
    if (const auto diags = module.diagnostics()) {
        if (const auto errors = diags->errors(); !errors.empty()) {
            return errors.front()->name + ": " + errors.front()->message;
        }
    }
    return "it does not compile";
}

/// The first error of a module's diagnostics as a secondary location: its range (resolved with
/// the module's tokens when the builder left it as one) and its own text. Nothing when the module
/// has no error or the error has no position.
std::optional<core::error::DiagLabel> firstErrorLocation(const UserDefinedModule &module) {
    const auto diags = module.diagnostics();
    if (!diags) {
        return std::nullopt;
    }
    const auto errors = diags->errors();
    if (errors.empty()) {
        return std::nullopt;
    }
    core::error::Diagnostic first = *errors.front();
    if (module.parser()) {
        first.fetchRange(core::error::RangeConverter(module.parser()->tokenPositions()));
    }
    const auto *range = std::get_if<core::error::CharRange>(&first.range);
    if (range == nullptr) {
        return std::nullopt;
    }
    return core::error::DiagLabel{
        *range, first.name + ": " + first.message, core::error::diagnosticPath(first)};
}

/// The operators whose OPER nodes appear in `graph` and every graph reachable from it.
std::vector<core::OperId> operatorsUsedBy(const Program &program, GCGraph *graph) {
    std::vector<core::OperId> used;
    std::unordered_set<core::OperId> seen;
    for (GCGraph *g : collectReachableGraphs(program, graph)) {
        for (gc_node_ref_t ref = 0; ref < g->nodeCount(); ++ref) {
            if (g->node(ref)->kind != GCNodeKind::Oper) {
                continue;
            }
            const core::OperId id = g->nodeBodyAs<GCOperBody>(ref)->oper;
            if (seen.insert(id).second) {
                used.push_back(id);
            }
        }
    }
    return used;
}

/// The constant a left-out optional operand gets, with its type.
std::pair<slot_t, core::type::Type *> constantOf(const sem::DefinitionDefault &value) {
    using core::type::Type;
    if (const auto *i = std::get_if<int64_t>(&value)) {
        return {core::rtdata::toSlot(static_cast<int64_t>(*i)), Type::Int64()};
    }
    if (const auto *d = std::get_if<double>(&value)) {
        return {core::rtdata::toSlot(static_cast<double>(*d)), Type::Float64()};
    }
    return {core::rtdata::toSlot(std::get<bool>(value)), Type::Bool()};
}

} // namespace

// ---- The store -------------------------------------------------------------------------------

DefinitionStore::DefinitionStore(core::context::Context &context) : context_(context) {}

DefinitionStore::~DefinitionStore() = default;

const DefinitionGraph *DefinitionStore::definitionOf(core::OperId oper, std::string *problem) {
    std::lock_guard lock(mutex_);
    Entry &entry = build(oper);
    // Recursion (design 9.2): no definition reaches its own operator through the operators it
    // uses and their definitions. Checked again whenever the declarations changed since: a
    // definition declared later can close a cycle through ones built before.
    const uint64_t epoch = core::OperatorTable::instance().declarationEpoch();
    if (entry.built && entry.checkedEpoch != epoch) {
        // A prior cycle can disappear when a dependency's declaration changes.
        // Built entries have passed the local checks; their remaining refusal is
        // the epoch-dependent cycle check, which must be recomputed both ways.
        entry.problem.clear();
        if (const auto cycle = cycleThrough(oper); !cycle.empty()) {
            std::string chain;
            for (core::OperId id : cycle) {
                chain += std::string(chain.empty() ? "" : " -> ") +
                         std::string(core::OperatorTable::instance().uri(id));
            }
            entry.problem = std::format(
                "the definition of '{}' is refused: a cycle of definitions ({})",
                core::OperatorTable::instance().uri(oper),
                chain);
        }
        entry.checkedEpoch = epoch;
    }
    if (problem != nullptr) {
        *problem = entry.problem;
    }
    return entry.problem.empty() ? entry.built.get() : nullptr;
}

std::vector<core::OperId> DefinitionStore::cycleThrough(core::OperId oper) {
    const auto &table = core::OperatorTable::instance();
    std::vector<core::OperId> path{oper};
    std::set<core::OperId> visited{oper};
    // Depth first over "the definition of x uses y, which has a definition".
    std::function<bool(core::OperId)> reaches = [&](core::OperId from) {
        const Entry &e = build(from);
        if (!e.built) {
            return false;
        }
        for (core::OperId used : e.uses) {
            if (used == oper) {
                path.push_back(used);
                return true;
            }
            const sem::OperatorSemantics *s = table.semantics(used);
            if (s == nullptr || !s->definition || !visited.insert(used).second) {
                continue;
            }
            path.push_back(used);
            if (reaches(used)) {
                return true;
            }
            path.pop_back();
        }
        return false;
    };
    return reaches(oper) ? path : std::vector<core::OperId>{};
}

const DefinitionStore::LibraryEntry &
DefinitionStore::importLibrary(const std::string &name) {
    if (auto it = libraries_.find(name); it != libraries_.end()) {
        return it->second;
    }
    LibraryEntry entry;
    try {
        // The program may have imported the library itself (it was then built with the
        // program); otherwise the import finds the file and the library is compiled on its own.
        entry.module = std::dynamic_pointer_cast<UserDefinedModule>(context_.importModule(name));
        if (!entry.module) {
            entry.problem = std::format("its library '{}' is not a module written in Camel", name);
            entry.absent  = true;
        }
    } catch (const core::error::Diagnostic &d) {
        entry.problem = std::format(
            "its library '{}' cannot be loaded: {}", name, loadFailureReason(name, d.message));
        // A module that is not there and one that is there and refused are both reached here
        // (a `.cmo` that will not load, a file that does not declare itself). The second has a
        // reason the user needs (H130): only a name with no file behind it is absent.
        entry.absent = !context_.moduleFileExists(name);
    } catch (const std::exception &e) {
        entry.problem = std::format("its library '{}' cannot be loaded: {}", name, e.what());
        entry.absent  = !context_.moduleFileExists(name);
    }
    return libraries_.emplace(name, std::move(entry)).first->second;
}

std::shared_ptr<UserDefinedModule>
DefinitionStore::library(const std::string &name, std::string &problem) {
    const LibraryEntry &entry = importLibrary(name);
    problem                   = entry.problem;
    auto module               = entry.module;
    if (!module) {
        return nullptr;
    }
    if (!module->compileAsLibrary()) {
        problem = std::format("its library '{}' does not compile: {}", name, firstError(*module));
        // Remembered as refused: the module is not compiled again (as before), and a pass that
        // names it gets the reason rather than "not found".
        libraries_.erase(name);
        libraries_.emplace(name, LibraryEntry{nullptr, problem, false});
        return nullptr;
    }
    return module;
}

std::optional<DefinitionStore::LibraryFunction> DefinitionStore::libraryFunction(
    const std::string &moduleName, const std::string &functionName, std::string &problem,
    std::optional<core::error::DiagLabel> *errorLocation, LibraryLookup *lookup) {
    std::lock_guard lock(mutex_);
    const std::string key = moduleName + "::" + functionName;
    if (auto it = functions_.find(key); it != functions_.end()) {
        problem = it->second.problem;
        if (lookup != nullptr) {
            *lookup = it->second.lookup;
        }
        if (errorLocation != nullptr) {
            *errorLocation = it->second.errorLocation;
        }
        return it->second.found;
    }
    const auto remember = [&](
                              std::optional<LibraryFunction> found, bool isAbsent,
                              std::optional<core::error::DiagLabel> location,
                              const LibraryLookup &what) {
        if (lookup != nullptr) {
            *lookup = what;
        }
        if (errorLocation != nullptr) {
            *errorLocation = location;
        }
        functions_[key] = {found, problem, isAbsent, location, what};
        return found;
    };
    const LibraryEntry &library = importLibrary(moduleName);
    problem                     = library.problem;
    if (!library.module) {
        LibraryLookup what;
        what.moduleFound = !library.absent;
        what.absent      = library.absent;
        return remember(std::nullopt, library.absent, std::nullopt, what);
    }
    const auto &module = library.module;
    // The tool program is the library module as a program whose entry is the function: its root
    // graph computes the module-level values and calls the function, so the function's capture
    // ports are bound from values that were computed (design principles 10.1). The graph the
    // caller runs is that program's root; its signature is the function's.
    std::shared_ptr<camel::compile::gir::DraftGraphBuilder> rootGraph;
    UserDefinedModule::ProgramStatus status = UserDefinedModule::ProgramStatus::Failed;
    try {
        status = module->compileAsProgram(functionName, problem, rootGraph);
    } catch (const core::error::DiagnosticsLimitExceededBaseException &) {
        // The compile stopped at the diagnostics limit of the context. In a `--timeit` context
        // that limit is the first error, which is a property of the iteration, not of the
        // library: the library still did not compile, and it is reported as such below, with
        // its first error as the secondary location. Letting the limit escape here is what made
        // `--timeit` print a bare `SyntaxDiag` instead of `InvalidPassParameter` (H210).
        status = UserDefinedModule::ProgramStatus::Failed;
    }
    LibraryLookup what;
    what.moduleFound = true;
    what.module      = module;
    if (status == UserDefinedModule::ProgramStatus::NoSuchFunction) {
        // The module is there and compiles; the name is what is wrong. Say which function it
        // declares without exporting, and which ones it does export (H130).
        what.absent             = true;
        what.declaresUnexported = module->declaresFunction(functionName);
        return remember(std::nullopt, true, std::nullopt, what);
    }
    if (status == UserDefinedModule::ProgramStatus::NotAFunction) {
        return remember(std::nullopt, false, std::nullopt, what);
    }
    if (status != UserDefinedModule::ProgramStatus::Built) {
        // The library's own first error becomes a secondary location, so the pipeline's
        // InvalidPassParameter carries the file, line, column and source line of what really
        // failed instead of stitching that text into its message (H36c). A failure with no
        // position (an imported module that does not compile) keeps its reason in the message.
        auto location = firstErrorLocation(*module);
        if (location) {
            problem = std::format("its library '{}' does not compile", moduleName);
        } else {
            problem = std::format(
                "its library '{}' does not compile: {}", moduleName, firstError(*module));
        }
        return remember(std::nullopt, false, std::move(location), what);
    }
    Program *program = context_.reserveProgram();
    camel::compile::gir::encodeToRuntimeProgram(rootGraph, *program, &context_.graphIds());
    context_.pinRuntimeProgram(program);
    if (program->root() == nullptr) {
        problem = std::format("'{}::{}' could not be encoded", moduleName, functionName);
        return remember(std::nullopt, false, std::nullopt, what);
    }
    problem.clear();
    return remember(LibraryFunction{program, program->root()}, false, std::nullopt, what);
}

DefinitionStore::Entry &DefinitionStore::build(core::OperId oper) {
    const auto &table                         = core::OperatorTable::instance();
    const sem::OperatorSemantics *s           = table.semantics(oper);
    const std::optional<sem::OperatorKey> key = s != nullptr ? std::optional(s->key) : std::nullopt;
    // Built (or refused) for the declaration in force: a declaration replaced since (a new
    // version of the operator, another definition) is built again.
    if (auto it = entries_.find(oper);
        it != entries_.end() && it->second.key == key && it->second.declaration == s) {
        return it->second;
    }
    entries_.erase(oper);
    const std::string uri(table.uri(oper));
    const auto fresh = [&]() -> Entry & {
        Entry &entry      = entries_[oper];
        entry.key         = key;
        entry.declaration = s;
        return entry;
    };
    const auto refuse = [&](std::string why) -> Entry & {
        Entry &entry  = fresh();
        entry.problem = std::format("the definition of '{}' is refused: {}", uri, why);
        return entry;
    };
    if (s == nullptr || !s->definition) {
        Entry &entry  = fresh();
        entry.problem = std::format("'{}' declares no definition", uri);
        return entry;
    }
    const sem::Definition &d = *s->definition;

    // The library and the function's graph.
    std::string problem;
    const auto module = library(d.module, problem);
    if (!module) {
        return refuse(problem);
    }
    // The library is declared into the context's program (compileAsLibrary); its exported names
    // are that program's table (one table per program, task H42).
    const auto entity = context_.programExports().exportedEntity(*module, Reference(d.function));
    using Access      = core::module::detail::EntityAccess;
    if (!entity) {
        return refuse(std::format("'{}' exports no '{}'", d.module, d.function));
    }
    if (Access::isGenericTemplate(*entity)) {
        return refuse(
            std::format("'{}::{}' is generic; a definition has one graph", d.module, d.function));
    }
    if (!Access::isGraphSet(*entity) || !Access::graphSet(*entity)) {
        return refuse(std::format("'{}::{}' is not a function", d.module, d.function));
    }
    camel::compile::gir::graph_ptr_t function;
    for (const auto &candidate : *Access::graphSet(*entity)) {
        const auto *type = candidate ? candidate->funcType() : nullptr;
        if (type != nullptr && type->normTypesCount() == d.params.size() &&
            type->withTypesCount() == 0) {
            function = candidate;
            break;
        }
    }
    if (!function) {
        return refuse(
            std::format(
                "'{}::{}' has no overload of {} ordinary parameters and no with parameter",
                d.module,
                d.function,
                d.params.size()));
    }
    // Kept for as long as the context, refused or not: nothing else owns them.
    Program *program = context_.reserveProgram();
    camel::compile::gir::encodeToRuntimeProgram(function, *program, &context_.graphIds());
    context_.pinRuntimeProgram(program);
    GCGraph *graph = program->root();
    if (graph == nullptr) {
        return refuse(std::format("'{}::{}' could not be encoded", d.module, d.function));
    }

    // The signature (design 9.2): the operator's operands are the function's ordinary
    // parameters; nothing else is bound.
    if (graph->normPorts().size() != d.params.size()) {
        return refuse("its graph does not take exactly the declared parameters");
    }
    if (!graph->withPorts().empty()) {
        return refuse(
            "its graph takes a with parameter, and a definition is called with the operator's "
            "operands only");
    }
    if (!graph->closureNodes().empty()) {
        // A capture is a module-level value of the definition's own module (task H37): a
        // definition library is encoded as the function graph alone, so there is no root graph
        // that could compute the value and no call site whose closure could carry it.
        std::string captured;
        for (const auto name : graph->captureNames()) {
            if (!captured.empty()) {
                captured += ", ";
            }
            captured += std::format("'{}'", core::symbolText(name));
        }
        return refuse(
            std::format(
                "its graph captures {} of the module's top level; a definition is a function of "
                "the operator's operands, so a module-level value cannot be bound",
                captured.empty() ? std::format("{} value(s)", graph->closureNodes().size())
                                 : captured));
    }
    // The effects (design 9.2): within the operator's.
    const auto traits           = table.find(oper);
    const EffectSummary effects = effectSummaryOf(*program, graph);
    if (traits && traits->pure && (effects.external || effects.writesVariables)) {
        return refuse("the operator is pure and its definition has an effect");
    }
    const bool neverFaults = (traits && !traits->mayFault) || (s->faults && s->faults->empty());
    if (neverFaults && effects.mayFault) {
        return refuse("the operator never faults and its definition may");
    }
    Entry &entry = fresh();
    entry.uses   = operatorsUsedBy(*program, graph);
    entry.built  = std::make_unique<DefinitionGraph>(DefinitionGraph{
         .oper        = oper,
         .key         = s->key,
         .definition  = d,
         .graph       = graph,
         .program     = program,
         .contentHash = fact::portableGraphHash(*program, graph),
     });
    return entry;
}

// ---- Expansion -----------------------------------------------------------------------------

ExpandResult expandDefinition(
    RuntimeGraphDraftSession &session, GraphDraft &draft, gc_node_ref_t node,
    ExpandOptions options) {
    ExpandResult result;
    const DraftNodeHeader *header = draft.header(node);
    if (header == nullptr || header->kind != GCNodeKind::Oper) {
        result.problem = "not an OPER node";
        return result;
    }
    const auto payload = draft.payloadOf(node);
    if (payload.size_bytes() < sizeof(GCOperBody)) {
        result.problem = "an OPER node without its operator";
        return result;
    }
    const core::OperId oper      = reinterpret_cast<const GCOperBody *>(payload.data())->oper;
    core::type::Type *resultType = header->dataType; // read now: adding nodes may move headers
    const DefinitionGraph *definition =
        session.context()->definitions().definitionOf(oper, &result.problem);
    if (definition == nullptr) {
        return result;
    }

    // The arguments: the node's operands, by the declared binding, each of a type the
    // definition takes (an operator may accept more than its definition does: a softmax of an
    // integer tensor is not expanded). Checked before the draft is changed.
    const std::vector<gc_node_ref_t> norm(
        draft.normInputsOf(node).begin(),
        draft.normInputsOf(node).end());
    const std::vector<gc_node_ref_t> with(
        draft.withInputsOf(node).begin(),
        draft.withInputsOf(node).end());
    const auto ports = definition->graph->normPorts();
    std::vector<std::optional<gc_node_ref_t>> bound;
    for (size_t i = 0; i < definition->definition.params.size(); ++i) {
        const sem::DefinitionParam &param = definition->definition.params[i];
        const auto &operands       = param.kind == sem::DefinitionParam::Kind::Norm ? norm : with;
        core::type::Type *portType = definition->graph->node(ports[i])->dataType();
        if (param.operand < operands.size()) {
            core::type::Type *argType = draft.header(operands[param.operand])->dataType;
            if (portType != nullptr && argType != nullptr && !portType->assignableFrom(argType)) {
                result.problem = std::format(
                    "operand {} is a {}, the definition takes a {}",
                    param.operand,
                    argType->toString(),
                    portType->toString());
                return result;
            }
            bound.push_back(operands[param.operand]);
            continue;
        }
        if (!param.fallback) {
            result.problem =
                std::format("the call leaves out operand {}, which has no default", param.operand);
            return result;
        }
        bound.push_back(std::nullopt);
    }
    std::vector<gc_node_ref_t> arguments;
    for (size_t i = 0; i < bound.size(); ++i) {
        if (bound[i]) {
            arguments.push_back(*bound[i]);
            continue;
        }
        const auto [value, type] = constantOf(*definition->definition.params[i].fallback);
        arguments.push_back(draft.materializeStaticValue(value, type));
    }

    // The call of the definition, in the node's place: its value, its control edges, its
    // source position.
    const camel::core::sem::PermissionMask permissions = draft.nodePermissions(node);
    const uint32_t attrs      = draft.nodeAttrs(node);
    const uint64_t origin     = draft.nodeOrigin(node);
    const std::vector<gc_node_ref_t> ctrl(
        draft.ctrlInputsOf(node).begin(),
        draft.ctrlInputsOf(node).end());
    session.adoptProgram(*definition->program);
    const gc_node_ref_t call = draft.addFuncNode(definition->graph->id(), resultType);
    draft.setNormInputs(call, arguments);
    draft.setCtrlInputs(call, ctrl);
    draft.setNodeOrigin(call, origin);
    // The call stands where the node stood, so it is a statement of the same block: the statements
    // written after it follow it, and it follows what the node followed
    // (docs/design/effects/edges.cn.md 4).
    draft.takeStatementPlace(node, call);
    draft.replaceAllValueUses(node, call);
    draft.replaceAllCtrlUses(node, call);
    draft.eraseNode(node);
    if (!options.inlineBody) {
        result.value = call;
        return result;
    }

    std::vector<bool> before(draft.nodeSlotCount(), false);
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        before[id] = draft.alive(id);
    }
    const DraftInlineResult inlined = inlineCallableInDraft(session, draft, call);
    if (!inlined) {
        result.value = call; // expanded, kept as a call
        return result;
    }
    // The definition's own operations are the expanded operation: they get its permissions
    // and its attributes too (design 6.4). Calls of other functions keep theirs (they are not
    // inlined here).
    if (permissions != 0 || attrs != 0) {
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!draft.alive(id) || (id < before.size() && before[id])) {
                continue;
            }
            const GCNodeKind kind = draft.header(id)->kind;
            if (kind == GCNodeKind::Oper || kind == GCNodeKind::Loop) {
                draft.setNodePermissions(id, draft.nodePermissions(id) | permissions);
                draft.setNodeAttrs(id, camel::core::attr::unite(draft.nodeAttrs(id), attrs));
            }
        }
    }
    result.value = inlined.valueExit;
    return result;
}

} // namespace camel::runtime

