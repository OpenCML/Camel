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
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
#include "camel/core/type/base.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/fact_persist.h"
#include "camel/runtime/loop.h"
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

/// The first error of a module's diagnostics, as one line.
std::string firstError(const UserDefinedModule &module) {
    if (const auto diags = module.diagnostics()) {
        if (const auto errors = diags->errors(); !errors.empty()) {
            return errors.front()->name + ": " + errors.front()->message;
        }
    }
    return "it does not compile";
}

/// The operators whose OPER nodes appear in `graph` and every graph reachable from it.
std::vector<core::OperId> operatorsUsedBy(GCGraph *graph) {
    std::vector<core::OperId> used;
    std::unordered_set<core::OperId> seen;
    for (GCGraph *g : collectReachableGraphs(graph)) {
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

std::shared_ptr<UserDefinedModule>
DefinitionStore::library(const std::string &name, std::string &problem) {
    if (auto it = libraries_.find(name); it != libraries_.end()) {
        if (!it->second) {
            problem = std::format("its library '{}' does not compile", name);
        }
        return it->second;
    }
    std::shared_ptr<UserDefinedModule> module;
    try {
        // The program may have imported the library itself (it was then built with the
        // program); otherwise the import finds the file and the library is compiled on its own.
        module = std::dynamic_pointer_cast<UserDefinedModule>(context_.importModule(name));
        if (!module) {
            problem = std::format("its library '{}' is not a module written in Camel", name);
        } else if (!module->compileAsLibrary()) {
            problem =
                std::format("its library '{}' does not compile: {}", name, firstError(*module));
            module.reset();
        }
    } catch (const core::error::Diagnostic &d) {
        problem = std::format(
            "its library '{}' cannot be loaded: {}",
            name,
            d.message.substr(0, d.message.find('\n')));
        module.reset();
    } catch (const std::exception &e) {
        problem = std::format("its library '{}' cannot be loaded: {}", name, e.what());
        module.reset();
    }
    libraries_.emplace(name, module);
    return module;
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
    const auto entity = module->getExportedEntity(Reference(d.function));
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
    GCGraph *graph = camel::compile::gir::encodeToRuntimeGraph(function);
    if (graph == nullptr) {
        return refuse(std::format("'{}::{}' could not be encoded", d.module, d.function));
    }
    // Kept for as long as the context, refused or not: nothing else owns them.
    const std::vector<GCGraph *> closure = collectReachableGraphs(graph);
    context_.pinRuntimeGraphs(closure);

    // The signature (design 9.2): the operator's operands are the function's ordinary
    // parameters; nothing else is bound.
    if (graph->normPorts().size() != d.params.size() || !graph->withPorts().empty() ||
        !graph->closureNodes().empty()) {
        return refuse("its graph does not take exactly the declared parameters");
    }
    // The effects (design 9.2): within the operator's.
    const auto traits           = table.find(oper);
    const EffectSummary effects = effectSummaryOf(graph);
    if (traits && traits->pure && (effects.external || effects.writesVariables)) {
        return refuse("the operator is pure and its definition has an effect");
    }
    const bool neverFaults = (traits && !traits->mayFault) || (s->faults && s->faults->empty());
    if (neverFaults && effects.mayFault) {
        return refuse("the operator never faults and its definition may");
    }
    Entry &entry = fresh();
    entry.uses   = operatorsUsedBy(graph);
    entry.built  = std::make_unique<DefinitionGraph>(DefinitionGraph{
        .oper        = oper,
        .key         = s->key,
        .definition  = d,
        .graph       = graph,
        .contentHash = fact::portableGraphHash(graph),
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
    const uint8_t permissions = draft.nodePermissions(node);
    const uint32_t attrs      = draft.nodeAttrs(node);
    const uint64_t origin     = draft.nodeOrigin(node);
    const std::vector<gc_node_ref_t> ctrl(
        draft.ctrlInputsOf(node).begin(),
        draft.ctrlInputsOf(node).end());
    const gc_node_ref_t call = draft.addFuncNode(definition->graph, resultType);
    draft.setNormInputs(call, arguments);
    draft.setCtrlInputs(call, ctrl);
    draft.setNodeOrigin(call, origin);
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
