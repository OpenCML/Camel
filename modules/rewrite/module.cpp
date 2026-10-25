// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The rewrite module: rules as values, and the engines that apply them to a program.
 *
 * `rule name(params) { left => right }` is read as two functions of the same parameters and
 * `let name = __rule('name', ...)` (graph store 5.2: a rule is the shorthand of a pair of
 * functions; rewrite design 2 (second version) 8.5: the rule is a value); the front end writes
 * the import of this module itself. The engines, `rewrite`, `run`, `saturate` and `extract`, take
 * the rules as an array.
 *
 * The callable operators are declared in `stdlib/rewrite.cml` (handover H183; `run`, H226); this
 * file keeps their kernels, the executor and their purity. The reserved groups the language's
 * `rule` and `analysis` declarations compile to (`__rule`, `__rule_when`, `__rule_generic`,
 * `__analysis`) have no callable Camel name, so they stay exported from here.
 */

#include "checkreport.h"
#include "proof.h"
#include "rule.h"

#include "analysis.h"

#include "../graph/handle.h"

#include "camel/compile/gir/rule_templates.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "camel/runtime/rules.h"
#include "camel/runtime/writings.h"
#include "camel/utils/assert.h"
#include "src/passes/sched/nodevm/nodevm.h"
#include "verify/translate.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <ostream>
#include <random>
#include <set>
#include <span>
#include <stdexcept>
#include <unordered_map>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::compile::gir::GenericRuleEntry;
using camel::compile::gir::GenericRuleInstance;
using camel::compile::gir::RuleConditionAt;
using camel::graphmod::Handle;
using camel::graphmod::HandleKind;
using camel::graphmod::HandleType;
using camel::rewritemod::CheckReportObject;
using camel::rewritemod::CheckRow;
using camel::rewritemod::kPropositionEquality;
using camel::rewritemod::kPropositionErrorBound;
using camel::rewritemod::kPropositionRealEquality;
using camel::rewritemod::kVerdictProved;
using camel::rewritemod::kVerdictRefuted;
using camel::rewritemod::kVerdictSampled;
using camel::rewritemod::linkAfter;
using camel::rewritemod::linkBefore;
using camel::rewritemod::ProofObject;
using camel::rewritemod::RewriteRecord;
using camel::rewritemod::RuleObject;
using camel::rewritemod::RuleType;

namespace {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;

slot_t ruleKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto name = camel::core::internSymbol(norm.get<String *>(0)->view());
    auto *object    = RuleObject::create(
        name,
        norm.get<rtdata::Bool>(1),
        static_cast<camel::core::sem::PermissionMask>(norm.get<rtdata::Int64>(2)),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(3)),
        norm.get<::Function *>(4),
        norm.get<::Function *>(5),
        nullptr,
        mm::autoSpace());
    return toSlot(object);
}

slot_t ruleWhenKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto name = camel::core::internSymbol(norm.get<String *>(0)->view());
    auto *object    = RuleObject::create(
        name,
        norm.get<rtdata::Bool>(1),
        static_cast<camel::core::sem::PermissionMask>(norm.get<rtdata::Int64>(2)),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(3)),
        norm.get<::Function *>(4),
        norm.get<::Function *>(5),
        norm.get<::Function *>(6),
        mm::autoSpace());
    return toSlot(object);
}

/// `__rule_generic('name', two_way, needs, consts, id)`: the maker of a rule whose head declares
/// type parameters. What it is given is the id the rule's templates were registered under (the
/// front end registers them while it builds this call): a generic rule has no function values to
/// hold, so it keeps the id, and the engine makes its sides where it matches, one instance per
/// type tuple (the ruling of 2026-10-23 on generic-rule-instantiation).
slot_t genericRuleKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto name = camel::core::internSymbol(norm.get<String *>(0)->view());
    auto *object    = RuleObject::create(
        name,
        norm.get<rtdata::Bool>(1),
        static_cast<camel::core::sem::PermissionMask>(norm.get<rtdata::Int64>(2)),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(3)),
        nullptr,
        nullptr,
        nullptr,
        mm::autoSpace(),
        static_cast<uint64_t>(norm.get<rtdata::Int64>(4)));
    return toSlot(object);
}

slot_t nameKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(String::from(std::string(norm.get<RuleObject *>(0)->name()), mm::autoSpace()));
}

slot_t twoWayKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<rtdata::Bool>(norm.get<RuleObject *>(0)->twoWay());
}

slot_t needsKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<rtdata::Int64>(norm.get<RuleObject *>(0)->needs());
}

slot_t constsKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot<rtdata::Int64>(static_cast<int64_t>(norm.get<RuleObject *>(0)->constParams()));
}

/// The state a condition closure needs: the rule object, rooted so that the closure can read the
/// condition function from it after every safepoint, and the node VM the conditions of one engine
/// call share. An instance of a generic rule has no rule object to read: its program and its
/// condition graph belong to the context, which pins them for its own lifetime, and the fields
/// after `vm` carry what the engine needs of the declaration the instance was made from -- how
/// many parameters the rule has, which of them are `const`, how many the condition takes, and the
/// name the messages use (camel/compile/gir/rule_templates.h).
struct WhenCall {
    std::shared_ptr<mm::RootHandle> rule;
    std::shared_ptr<NodeVMSchedPass> vm;
    /// The instance, when the condition belongs to one: its program and its condition graph.
    const camel::runtime::Program *program = nullptr;
    const camel::runtime::GCGraph *graph   = nullptr;
    /// The rule's own parameters, the condition's, and which of the rule's are `const`.
    size_t own            = 0;
    size_t arity          = 0;
    uint64_t constParams  = 0;
    /// The name the messages use.
    std::string name;
};

/// Whether a rule's condition holds at this match (rewrite design 2 (second version) 1). A `const`
/// parameter is the constant's value, of the type the parameter is declared with, so the guard is
/// written on the value (`c > 0.0`) instead of reading it out of a node. Every other parameter is
/// a Node handle of the draft being rewritten. The condition's last parameters are the hidden node
/// parameters of the `const` ones -- one per `const` parameter, whether or not the condition writes
/// `node_of` for it -- which is what `node_of(c)` was rewritten to at build time; a `const`
/// parameter reaches its node only through them. A parameter the left side does not use names no
/// node and faults when it is read.
bool callWhen(const WhenCall &call, const camel::runtime::Match &match) {
    // The rule is read from the rooted handle, and the errors below read it again from there: a
    // condition is Camel code, so making the argument handles below collects, and a RuleObject the
    // engine held by address across one of those safepoints would be stale. The closure that calls
    // this holds only the WhenCall, whose RootHandle the collector keeps up to date. An instance of
    // a generic rule has no rule object: how many parameters the rule has and which are `const` was
    // read from the declaration through its root when the instance was made, and the call
    // description has carried those copies since.
    const RuleObject *rule = call.rule != nullptr ? call.rule->getAs<RuleObject>() : nullptr;
    const size_t arity =
        rule != nullptr
            ? static_cast<size_t>(rule->when()->graph()->funcType()->normTypesCount())
            : call.arity;
    const size_t own = rule != nullptr
                           ? static_cast<size_t>(rule->lhs()->graph()->normPorts().size())
                           : call.own;
    const uint64_t constParams = rule != nullptr ? rule->constParams() : call.constParams;
    // The rule's name, read where it is needed: an ordinary rule's object moves under the
    // collector, an instance's name is part of the call description.
    const auto ruleName = [&]() -> std::string_view {
        const RuleObject *current = call.rule != nullptr ? call.rule->getAs<RuleObject>() : nullptr;
        return current != nullptr ? current->name() : std::string_view(call.name);
    };
    camel::runtime::GraphDraft &draft = match.writings.draft();
    // One slot per parameter: a rooted Node handle for a node parameter, a rooted object for a
    // constant the collector can move, and nothing for a constant whose bits are copied below.
    std::vector<std::unique_ptr<mm::RootHandle>> roots;
    std::vector<slot_t> args;
    roots.reserve(arity);
    args.reserve(arity);
    const auto constantOf = [&](size_t i) {
        return i < match.vars.size()
                   ? match.constant(static_cast<uint32_t>(i))
                   : std::optional<std::pair<slot_t, camel::core::type::Type *>>{};
    };
    // The `const` parameter the hidden node parameter `i` belongs to: the hidden ones follow the
    // rule's own in the order of the `const` parameters, so it is the (i - own)-th of them.
    const auto constParamOfNode = [&](size_t i) {
        const size_t wanted = i - own;
        size_t seen         = 0;
        for (size_t p = 0; p < own; ++p) {
            if (p < 64 && ((constParams >> p) & 1) != 0) {
                if (seen == wanted) {
                    return p;
                }
                ++seen;
            }
        }
        return SIZE_MAX;
    };
    for (size_t i = 0; i < arity; ++i) {
        const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
        if (i < own) {
            if (isConst) {
                const auto found = constantOf(i);
                if (!found) {
                    throw std::runtime_error(std::format(
                        "Rule '{}': parameter {} of its condition is `const`, and the left side "
                        "bound no constant there.",
                        ruleName(),
                        i + 1));
                }
                roots.push_back(
                    found->second != nullptr && found->second->isGCTraced()
                        ? std::make_unique<mm::RootHandle>(
                              mm::autoSpace(),
                              rtdata::fromSlot<rtdata::Object *>(found->first),
                              found->second,
                              "rewrite.whenConst")
                        : nullptr);
                continue;
            }
            const camel::runtime::gc_node_ref_t ref =
                i < match.vars.size() ? match.vars[i] : camel::runtime::kInvalidNodeRef;
            Handle *handle = Handle::createInDraft(draft, ref, mm::autoSpace());
            roots.push_back(
                std::make_unique<mm::RootHandle>(
                    mm::autoSpace(),
                    handle,
                    HandleType::get(HandleKind::Node),
                    "rewrite.whenArg"));
            continue;
        }
        // A hidden node parameter: the node the `const` parameter it belongs to was bound to.
        const size_t constParam = constParamOfNode(i);
        if (constParam == SIZE_MAX || !constantOf(constParam)) {
            // A `const` parameter the left side does not use has no node to hand over: a node the
            // condition could read does not exist, and the rule says nothing about the program.
            throw camel::runtime::RuleCompileError(std::format(
                "Rule '{}': parameter {} of its condition is the node of a `const` parameter "
                "(`node_of`), but the left side binds no node for it.",
                ruleName(),
                i + 1));
        }
        const camel::runtime::gc_node_ref_t bound = match.vars[constParam];
        if (bound == camel::runtime::kInvalidNodeRef) {
            throw camel::runtime::RuleCompileError(std::format(
                "Rule '{}': parameter {} of its condition is the node of a `const` parameter "
                "(`node_of`), and the left side bound no node there.",
                ruleName(),
                i + 1));
        }
        Handle *handle = Handle::createInDraft(draft, bound, mm::autoSpace());
        roots.push_back(
            std::make_unique<mm::RootHandle>(
                mm::autoSpace(),
                handle,
                HandleType::get(HandleKind::Node),
                "rewrite.whenNodeArg"));
    }
    // Making a handle can collect: the arguments are read from the roots after the last one, so
    // each slot names where the collector put it, or the constant's bits.
    for (size_t i = 0; i < arity; ++i) {
        if (i < own && i < 64 && ((constParams >> i) & 1) != 0) {
            args.push_back(
                roots[i] != nullptr ? toSlot(roots[i]->get()) : constantOf(i)->first);
            continue;
        }
        args.push_back(toSlot(roots[i]->getAs<Handle>()));
    }
    // A call can move the objects: the condition is read from the rooted rule after the last
    // allocation and immediately before the call. An instance's condition is the graph the context
    // published for it, which no safepoint moves.
    ::Function *when = rule != nullptr ? rule->when() : nullptr;
    const camel::runtime::Program *whenProgram = when != nullptr ? when->program() : call.program;
    const camel::runtime::GCGraph *whenGraph   = when != nullptr ? when->graph() : call.graph;
    ASSERT(
        whenProgram != nullptr && whenGraph != nullptr,
        "A condition is a function of a rule value or the graph of an instance.");
    const slot_t result = [&]() -> slot_t {
        try {
            // The graphs of a program are read and written by the node VM (its value slots), so it
            // takes them without `const`; an instance's condition graph is the context's, and no
            // other reader holds it.
            return call.vm->callGraph(
                whenProgram, const_cast<camel::runtime::GCGraph *>(whenGraph), args);
        } catch (const camel::core::error::FactQueryNoValue &) {
            // A fact query inside the condition named parts that are no value's operands: the
            // condition is false -- this rule does not apply here -- and not a failure of the pass.
            // A condition is a question of whether the rule applies, so a query that finds nothing
            // answers no rather than ending the run.
            return toSlot<rtdata::Bool>(false);
        }
    }();
    return fromSlot<rtdata::Bool>(result);
}

/// What a rule object says, as the compiler of rules takes it. `call` is set when the rule has a
/// condition; the closure shares it, and with it the rooted rule object.
camel::runtime::RuleSource
sourceOf(const RuleObject &rule, uint16_t id, const std::shared_ptr<WhenCall> &call) {
    camel::runtime::RuleSource source;
    source.name          = std::string(rule.name());
    source.id            = id;
    source.needs         = rule.needs();
    source.bidirectional = rule.twoWay();
    source.program       = rule.lhs()->program();
    source.lhs           = rule.lhs()->graphId();
    source.rhs           = rule.rhs()->graphId();
    for (int bit = 0; bit < 64; ++bit) {
        source.constParams.push_back((rule.constParams() >> bit) & 1);
    }
    if (rule.when() != nullptr) {
        source.whenArity =
            static_cast<uint32_t>(rule.when()->graph()->funcType()->normTypesCount());
        // The parameters of the condition that are the rule's own are its first ones: it takes
        // them in the order the rule writes them and then one hidden node per `const` parameter,
        // in the same order. The hidden node parameters therefore begin where the sides' parameters
        // end, whatever order the `const` ones stand in.
        source.whenNodesStart = static_cast<uint32_t>(rule.lhs()->graph()->normPorts().size());
        // The closure keeps the rooted rule (`call`), never its address: the engine holds it across
        // the safepoints of every condition the application evaluates, and the rule moves under it.
        source.when = [call](const camel::runtime::Match &match) {
            return callWhen(*call, match);
        };
    }
    return source;
}

/// Whether a graph is one side of a rule: those are the patterns, not part of the program the
/// rules are applied to.
bool isRuleSide(const camel::runtime::GCGraph &graph) {
    const std::string &name = graph.name();
    return name.ends_with("__lhs") || name.ends_with("__rhs") || name.ends_with("__when");
}

/// The compiled rules of the instances of one generic rule, one entry per instance that was made:
/// an instance's place in its rule's list is the index here (the entry's list grows as the types
/// of the programs it is applied to are seen, so nothing keeps a reference into it).
struct CompiledInstances {
    std::vector<std::vector<camel::runtime::Rule>> perInstance;
    std::vector<bool> made;
};

/// What one instance of a generic rule says, as the compiler of rules takes it: the graphs the
/// context published for the type tuple, under the name the reports use (`add_zero<i32>`, so
/// `explain` names the instance a step came from). There is no rule object behind an instance: the
/// closure keeps the call description, whose parts do not move.
camel::runtime::RuleSource instanceOf(
    const RuleObject &rule, uint16_t id, const GenericRuleInstance &instance,
    const std::shared_ptr<WhenCall> &call) {
    camel::runtime::RuleSource source;
    source.name          = std::format("{}<{}>", rule.name(), instance.typeText);
    source.id            = id;
    source.needs         = rule.needs();
    source.bidirectional = rule.twoWay();
    source.program       = instance.program;
    source.lhs           = instance.lhs->id();
    source.rhs           = instance.rhs->id();
    for (int bit = 0; bit < 64; ++bit) {
        source.constParams.push_back((rule.constParams() >> bit) & 1);
    }
    if (instance.when != nullptr) {
        source.whenArity = instance.whenArity;
        // The parameters of the condition that are the rule's own are its first ones, as for an
        // ordinary rule: the condition takes them in the order the rule writes them.
        source.whenNodesStart = static_cast<uint32_t>(instance.lhs->normPorts().size());
        source.when = [call](const camel::runtime::Match &match) { return callWhen(*call, match); };
    }
    return source;
}

/// How many type tuples of one generic rule are enumerated at most, wherever a set of tuples is
/// built (the engine's own tuple list, `why_not`'s candidates, and `check`'s instance walk). A law
/// of one type parameter over the registered types is far below it; the product for a law of two is
/// not, and it is capped rather than instantiated body by body (the ruling of 2026-10-24 on
/// `generic-rule-check-surface-and-multi-parameter-instances`).
constexpr size_t kCheckTupleCap = 64;

/// The types of every value the program has, each once, in the order the graphs write them. Two
/// things ask for them: the tuples a generic rule is instantiated at where the engine walks the
/// program's own side, and the types a `check` given a program adds to its candidate set (the
/// ruling of 2026-10-24 on `generic-rule-check-surface-and-multi-parameter-instances`, §1).
std::vector<Type *> programValueTypes(const camel::runtime::Program &program) {
    std::vector<Type *> types;
    for (const camel::runtime::GCGraph *graph : program.graphs()) {
        if (isRuleSide(*graph)) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            // The type of the value, not the cell a `var` parameter is held in: a law is
            // instantiated at the type of the value it applies to (`float`, not `var float`),
            // and built for `var float` its guard would pass a reference where the guard's own
            // instance takes the value (measured 2026-10-24: `float_combine__when<var float>`
            // passes a `var float` to `combineSameSign<float>`).
            Type *type = camel::core::type::valueTypeOf(graph->node(it.ref())->dataType());
            if (type == nullptr || type->code() == TypeCode::Void) {
                continue;
            }
            const bool known = std::any_of(
                types.begin(), types.end(),
                [&](const Type *seen) { return seen == type || seen->equals(type); });
            if (!known) {
                types.push_back(type);
            }
        }
    }
    return types;
}

/// The type tuples a generic rule is instantiated at, one per type parameter: for a rule of one
/// parameter the types themselves, and for a rule of more the product of the types over its
/// parameters, capped at `kCheckTupleCap` tuples in a fixed order. The types are those of every
/// value the program being rewritten has, each once, in the order the graphs write them. The ruling
/// of 2026-10-23 on generic-rule-instantiation binds the rule's type variables from the nodes it
/// matches, so the types are the program's own (not the registered meta-types `check` adds). A
/// tuple that cannot be written for is remembered as one that did not come about, and the engine
/// skips it.
std::vector<std::vector<Type *>> tuplesOf(const camel::runtime::Program &program, size_t arity) {
    const std::vector<Type *> types = programValueTypes(program);
    std::vector<std::vector<Type *>> tuples;
    if (types.empty()) {
        return tuples;
    }
    const size_t count = std::max<size_t>(1, arity);
    std::vector<size_t> at(count, 0);
    for (;;) {
        std::vector<Type *> tuple(count);
        for (size_t i = 0; i < count; ++i) {
            tuple[i] = types[at[i]];
        }
        tuples.push_back(std::move(tuple));
        if (tuples.size() >= kCheckTupleCap) {
            break;
        }
        size_t i = 0;
        for (; i < count; ++i) {
            if (++at[i] < types.size()) {
                break;
            }
            at[i] = 0;
        }
        if (i == count) {
            break;
        }
    }
    return tuples;
}

/// The type tuples of the registered types a value of `type` could stand at, for a rule of `arity`
/// type parameters: every tuple with `type` at one of its positions, in the order the types were
/// registered, at most `kCheckTupleCap` of them. A rule of one parameter is named by the value's
/// type alone; a rule of two is not, and this is what a question about one value can still ask
/// (`why_not`).
std::vector<std::vector<Type *>> tuplesHolding(const Type *type, size_t arity) {
    std::vector<std::vector<Type *>> tuples;
    if (arity <= 1) {
        tuples.push_back({const_cast<Type *>(type)});
        return tuples;
    }
    const std::vector<Type *> universe = camel::compile::gir::ruleInstanceTypes();
    if (universe.empty()) {
        return tuples;
    }
    std::vector<size_t> at(arity, 0);
    for (;;) {
        std::vector<Type *> tuple(arity);
        bool holds = false;
        for (size_t i = 0; i < arity; ++i) {
            tuple[i] = universe[at[i]];
            holds    = holds || tuple[i] == type || tuple[i]->equals(const_cast<Type *>(type));
        }
        if (holds) {
            tuples.push_back(std::move(tuple));
            if (tuples.size() >= kCheckTupleCap) {
                break;
            }
        }
        size_t i = 0;
        for (; i < arity; ++i) {
            if (++at[i] < universe.size()) {
                break;
            }
            at[i] = 0;
        }
        if (i == arity) {
            break;
        }
    }
    return tuples;
}

/// The compiled rules of one instance of a generic rule, made now if this is the first read and
/// kept in the entry: the instance is compiled once, so a second application of the same rule at
/// the same types pays nothing (the ruling of 2026-10-23 on generic-rule-instantiation: the type
/// part is folded once per tuple and cached).
const std::vector<camel::runtime::Rule> &instanceRules(
    Context &ctx, const RuleObject &rule, GenericRuleEntry &entry, size_t index) {
    static const std::vector<camel::runtime::Rule> none;
    if (entry.compiled == nullptr) {
        entry.compiled = std::make_shared<CompiledInstances>();
    }
    auto *compiled = static_cast<CompiledInstances *>(entry.compiled.get());
    compiled->perInstance.resize(entry.instances.size());
    compiled->made.resize(entry.instances.size(), false);
    if (index >= entry.instances.size()) {
        return none;
    }
    if (!compiled->made[index]) {
        const GenericRuleInstance &instance = entry.instances[index];
        std::shared_ptr<WhenCall> call;
        if (instance.when != nullptr) {
            call              = std::make_shared<WhenCall>();
            call->vm          = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
            call->program     = instance.program;
            call->graph       = instance.when;
            call->own         = instance.lhs->normPorts().size();
            call->arity       = instance.whenArity;
            call->constParams = rule.constParams();
            call->name        = std::format("{}<{}>", rule.name(), instance.typeText);
        }
        // The instance's own place in its rule is its id, so that the id a step records is the
        // same in every application. Ids are numbered within each rule, not across the rules of a
        // run: the engine records this id and reads nothing by it (an ordinary rule's ids are
        // numbered per run, which is the same thing when a run holds one rule).
        const camel::runtime::RuleSource source =
            instanceOf(rule, static_cast<uint16_t>(index + 1), instance, call);
        compiled->perInstance[index] = camel::runtime::compileRule(ctx, source);
        compiled->made[index]        = true;
    }
    return compiled->perInstance[index];
}

/// The engine's rules for the rule values of an array.
///
/// Compiling a rule makes constants and can collect: the array is read again from the argument,
/// which the collector keeps up to date, each time. A generic rule has no sides of its own: it
/// contributes one instance per type tuple of the program it is applied to, made once and kept.
std::vector<camel::runtime::Rule> engineRules(
    Context &ctx, ArgsView &norm, size_t argument, const camel::runtime::Program *base) {
    std::vector<camel::runtime::Rule> out;
    auto vm            = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
    uint16_t next      = 1;
    const size_t count = norm.get<Array *>(argument)->size();
    for (size_t i = 0; i < count; ++i) {
        RuleObject *rule = norm.get<Array *>(argument)->get<RuleObject *>(i);
        if (rule->ruleId() != 0) {
            const uint64_t ruleId = rule->ruleId();
            if (base == nullptr) {
                continue;
            }
            // The rule's own number of type parameters decides how many types a tuple has: a law of
            // two is instantiated at a pair of the program's types, not at one of them (the ruling
            // of 2026-10-23 on generic-rule-instantiation assumes one type per value, and asking a
            // two-parameter rule for one type is what left such a law with no instance at all).
            GenericRuleEntry *entry = camel::compile::gir::genericRuleEntry(ctx, ruleId);
            const size_t arity      = entry != nullptr ? entry->typeParams.size() : 1;
            for (const std::vector<Type *> &types : tuplesOf(*base, arity)) {
                const size_t index = camel::compile::gir::genericRuleInstance(ctx, ruleId, types);
                // Making an instance builds a body and encodes graphs: the value is read again
                // from the array, and the entry again from the context, after it.
                entry = camel::compile::gir::genericRuleEntry(ctx, ruleId);
                if (entry == nullptr || index >= entry->instances.size() ||
                    !entry->instances[index].made) {
                    // A type tuple the rule cannot be written for says nothing: the ruling of
                    // 2026-10-23 keeps a failed instantiation quiet, and `why_not` is where the
                    // reason is read.
                    continue;
                }
                RuleObject *current = norm.get<Array *>(argument)->get<RuleObject *>(i);
                for (const camel::runtime::Rule &compiled :
                     instanceRules(ctx, *current, *entry, index)) {
                    out.push_back(compiled);
                }
            }
            continue;
        }
        std::shared_ptr<WhenCall> call;
        if (rule->when() != nullptr) {
            call       = std::make_shared<WhenCall>();
            call->rule = std::make_shared<mm::RootHandle>(
                mm::autoSpace(),
                rule,
                RuleType::get(),
                "rewrite.rule");
            call->vm = vm;
        }
        const camel::runtime::RuleSource source = sourceOf(*rule, next++, call);
        for (camel::runtime::Rule &compiled : camel::runtime::compileRule(ctx, source)) {
            out.push_back(std::move(compiled));
        }
    }
    return out;
}

// ---------------------------------------------------------------------- the proof

/// `Proof`, the opaque type of `explain` and `explain_all` (rewrite design 8.7). A proof holds a
/// copy of the run's record: the steps are values, not references into a graph that may be gone.
class ProofType final : public OtherType {
  public:
    static ProofType *get();
    static bool is(const Type *type);

    std::string toString() const override { return "Proof"; }
    std::string mangle() const override { return "rewrite.Proof"; }
    Type *clone(bool deep = false) const override;
    bool equals(Type *other) const override;
    CastSafety castSafetyFrom(Type *source) const override;
    bool assignableFrom(Type *source) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    explicit ProofType(TypeCode code) : OtherType(code) {}
};

/// The record of one attributed application (rewrite design 8.7): the steps, and for each graph
/// the runtime node id every live draft node took in the program the run produced. A step names
/// its nodes by draft id, the identity that chains one step to the next; the map is what lets a
/// `Node` of the produced program find the rewrite it stands in.
///
/// The definition is in proof.h: the debugger reads a proof's record from the proof the module
/// hands it, so the format has one owner, not a second copy in the tool.
/// `Proof`: what `explain` and `explain_all` answer (rewrite design 8.7), declared in proof.h for
/// the same reason. This file builds and prints it (proof.cpp holds those methods).

ProofType *ProofType::get() {
    static ProofType *instance = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        const TypeCode code = registerOtherType("rewrite::Proof", TypeFlag::GC_Traced);
        void *mem = mm::permSpace().alloc(sizeof(ProofType), alignof(ProofType));
        ASSERT(mem != nullptr, "Failed to allocate ProofType from permSpace");
        instance = new (mem) ProofType(code);
    });
    return instance;
}

bool ProofType::is(const Type *type) {
    return type != nullptr && type->code() == get()->code();
}

Type *ProofType::clone(bool) const { return get(); }

bool ProofType::equals(Type *other) const { return is(other); }

CastSafety ProofType::castSafetyFrom(Type *source) const {
    return is(source) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool ProofType::assignableFrom(Type *source) const { return is(source); }

OtherType *ProofType::cloneWithParams(std::span<Type *const>) const { return get(); }

/// `CheckReport`, the type of what `check` answers (H247, slice S0). A report is a table of rows
/// (checkreport.h), not a string: the fixed rendering of the table is its `print`, so a reader
/// keeps the fields and gets the text, and no caller has to parse the text back into fields. The
/// type holds text and no reference into any GC space, so it is an ordinary other type.
class CheckReportType final : public OtherType {
  public:
    static CheckReportType *get();
    static bool is(const Type *type);

    std::string toString() const override { return "CheckReport"; }
    std::string mangle() const override { return "rewrite.CheckReport"; }
    Type *clone(bool deep = false) const override;
    bool equals(Type *other) const override;
    CastSafety castSafetyFrom(Type *source) const override;
    bool assignableFrom(Type *source) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    explicit CheckReportType(TypeCode code) : OtherType(code) {}
};

CheckReportType *CheckReportType::get() {
    static CheckReportType *instance = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        const TypeCode code = registerOtherType("rewrite::CheckReport", TypeFlag::GC_Traced);
        void *mem = mm::permSpace().alloc(sizeof(CheckReportType), alignof(CheckReportType));
        ASSERT(mem != nullptr, "Failed to allocate CheckReportType from permSpace");
        instance = new (mem) CheckReportType(code);
    });
    return instance;
}

bool CheckReportType::is(const Type *type) {
    return type != nullptr && type->code() == get()->code();
}

Type *CheckReportType::clone(bool) const { return get(); }

bool CheckReportType::equals(Type *other) const { return is(other); }

CastSafety CheckReportType::castSafetyFrom(Type *source) const {
    return is(source) ? CastSafety::Safe : CastSafety::Forbidden;
}

bool CheckReportType::assignableFrom(Type *source) const { return is(source); }

OtherType *CheckReportType::cloneWithParams(std::span<Type *const>) const { return get(); }

/// What a verdict is about, read off the permissions the rule asks for (the ruling of 2026-10-24 on
/// `verification-licence-to-precondition`):
///
///   * `reassociate` and `contract` let the rewriting change the rounding, so the proposition is
///     the two halves the ruling gives it: real equality on the finite inputs and IEEE equality,
///     bit for bit, on the values a real model does not have (NaN, the infinities, -0) -- unless
///     the rule asks for `no_nans` / `no_infs` / `no_signed_zeros`, which narrow the domain the
///     assumptions cover rather than change what is claimed.
///   * `refine_accuracy` proves an error bound, not an equality (licence 2).
///   * `change_precision` claims the difference stays inside the error bound of the precision the
///     rewriting moves to (design principles 7.2), which is an error bound as well.
///   * everything else claims bit-for-bit equality, which is what a rule that needs no permission
///     is checked against (`kNoPermissions` compares slots, no tolerance).
std::string propositionOf(camel::core::sem::PermissionMask needs) {
    using camel::core::sem::kChangePrecision;
    using camel::core::sem::kContract;
    using camel::core::sem::kReassociate;
    using camel::core::sem::kRefineAccuracy;
    if ((needs & kRefineAccuracy) != 0) {
        return kPropositionErrorBound;
    }
    if ((needs & (kReassociate | kContract)) != 0) {
        return kPropositionRealEquality;
    }
    if ((needs & kChangePrecision) != 0) {
        return kPropositionErrorBound;
    }
    return kPropositionEquality;
}

/// The stream one rule's samples are drawn from (the ruling of 2026-10-24 on
/// `accuracy-check-sampling`, 1): the seed is derived from the rule's identity -- its name together
/// with the type tuple of the instance, `add_zero<i32>` -- and not from its place in the array the
/// caller passed, so the same rule draws the same samples in any call, whatever else is checked
/// beside it. `seed` stays the caller's knob: the identity decides *which* stream, the seed *where*
/// in it.
uint64_t sampleStreamOf(uint64_t seed, std::string_view identity) {
    uint64_t hash = 1469598103934665603ull; // FNV-1a, 64 bit
    for (const char c : identity) {
        hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
        hash *= 1099511628211ull;
    }
    return seed ^ (hash * 1000003ull);
}


/// One application of rules: the program it produced and what the engine counted while applying
/// them (the rewrites and the nodes are summed over the graphs; the rounds are the rounds the
/// whole application made, each graph advancing in the same round).
struct Application {
    const camel::runtime::Program *program = nullptr;
    camel::runtime::RewriteStats stats;
};

/// Applies `compiled` one way, in place, to a fixpoint over at most `maxRounds` rounds, to every
/// graph of `base` that is not a side of a rule. A condition is Camel code, and so is a safepoint:
/// every draft the session has opened must keep its static values relocatable until the commit
/// reads them again, not only the one being rewritten now.
///
/// `log`, when given, is the attribution of the application (rewrite design 8.7): the engine
/// records one step per rewrite, and this function names the graph each step was made in and the
/// runtime node id each live draft node took in the program the commit made. It is passed in from
/// `run` and is null for every other caller, so a plain `rewrite` records nothing and allocates
/// nothing for the record.
Application applyRewrite(
    Context &ctx, const camel::runtime::Program *base,
    std::span<const camel::runtime::Rule> compiled, camel::core::sem::NumericPolicy policy,
    size_t maxRounds, RewriteRecord *record = nullptr) {
    camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), base);
    camel::runtime::RuntimeGraphDraftSession::RootScope draftRoots(session);
    camel::runtime::RewriteStats total;
    total.fixpoint = true;
    camel::runtime::RewriteLog *log = record != nullptr ? &record->steps : nullptr;
    for (camel::runtime::GCGraph *graph : base->graphs()) {
        if (isRuleSide(*graph)) {
            continue;
        }
        camel::runtime::GraphDraft &draft = session.edit(graph);
        camel::runtime::DraftWritings writings(draft, policy);
        const size_t from = log != nullptr ? log->size() : 0;
        const camel::runtime::RewriteStats stats = writings.rewrite(compiled, maxRounds, log);
        if (log != nullptr) {
            for (size_t i = from; i < log->size(); ++i) {
                (*log)[i].graph   = graph->name();
                (*log)[i].graphId = graph->id();
            }
            // The ids the steps name are draft ids; the encoder numbers the committed nodes its
            // own way. Kept before the commit, which encodes the drafts as they are now.
            record->nodeRefsByGraph[graph->id()] = session.committedNodeRefs(graph);
        }
        total.rounds = std::max(total.rounds, stats.rounds);
        total.visited += stats.visited;
        total.applied += stats.applied;
        total.erased += stats.erased;
        total.fixpoint = total.fixpoint && stats.fixpoint;
    }
    return {session.commitChecked(), total};
}

/// `rewrite(p, rules)` and `rewrite(p, rules, strict)`: the program with the rules applied one way,
/// in place, to a fixpoint (rewrite design 3). The third parameter chooses the numeric policy of
/// the pass itself (design principles 7.4, 7.5): `strict` grants no permission, as
/// `std::opt safe_fp=true` does, so only the laws that need none fire.
/// An error value when a rule cannot be a rule, holds both ways, or the result is not a program.
/// Only this module's own rule errors become error values. Anything else leaves by the exception
/// it already is: a limit, a fault or a defect of another part of the compiler reported as a
/// rule that is badly written would name the wrong cause and drop whatever the exception carried.
slot_t rewriteKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const bool strict           = norm.size() > 2 && norm.get<Bool>(2);
    try {
        const auto compiled = engineRules(ctx, norm, 1, base);
        const Application application = applyRewrite(
            ctx, base, compiled,
            strict ? camel::core::sem::NumericPolicy::strict()
                   : camel::core::sem::NumericPolicy::relaxed(),
            16);
        return makeOk(
            toSlot(const_cast<camel::runtime::Program *>(application.program)), resultType);
    } catch (const camel::runtime::GraphCommitConflict &e) {
        // The program the rewrite started from was replaced while it ran: a rule's condition that
        // commits -- through a rewrite or a graph transaction of its own -- does this. Which rules
        // were given is the caller's decision, so the failure is theirs to see as an error value,
        // and not the internal error of a pass that found a program it cannot handle.
        return makeError("RewriteConflict", e.what(), "<rewrite::rewrite>");
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::rewrite>");
    } catch (const std::invalid_argument &e) {
        return makeError("RuleTwoWay", e.what(), "<rewrite::rewrite>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    }
}

/// `run<rules, rounds: 16, strict: false, explain: false>(p)`: one application of the rules, with
/// what it cost (H226). The configuration is the declaration's `with` parameters (handover H235),
/// which is why the kernel reads them from `with` and the program from `norm`. It answers the
/// `Run<Program>` that `stdlib/rewrite.cml` declares: a struct with the program, the rewrites
/// applied, the rounds, the nodes visited, the nodes erased, whether the last round reached a
/// fixpoint, the wall time in seconds, and the proof when the attribution was recorded. The fields
/// are built in the declaration's order, which is the only place their names and positions are
/// decided together with `Run`.
///
/// `explain` opens the attribution record (rewrite design 8.7). Only then is a record kept and a
/// `Proof` made and put in the struct; without it the field is null and no step was recorded, so a
/// plain run pays nothing for a reader that never comes.
/// Only this module's own rule errors become error values (see `rewriteKernel`).
slot_t runKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    const auto *base     = norm.get<const camel::runtime::Program *>(0);
    const int64_t rounds = with.size() > 1 ? with.get<Int64>(1) : 16;
    const bool strict    = with.size() > 2 && with.get<Bool>(2);
    const bool explain   = with.size() > 3 && with.get<Bool>(3);
    TupleType *proofType = TupleType::nullable(ProofType::get());
    StructTypeFactory factory;
    factory.add("result", norm.type(0));
    factory.add("rewrites", Type::Int());
    factory.add("rounds", Type::Int());
    factory.add("visited", Type::Int());
    factory.add("erased", Type::Int());
    factory.add("fixpoint", Type::Bool());
    factory.add("seconds", Type::Float64());
    factory.add("proof", proofType);
    StructType *runType         = factory.build();
    const TupleType *resultType = TupleType::fallible(runType);
    try {
        const auto start    = std::chrono::steady_clock::now();
        const auto compiled = engineRules(ctx, with, 0, base);
        RewriteRecord record;
        record.strict = strict;
        record.input  = base;
        const Application application = applyRewrite(
            ctx, base, compiled,
            strict ? camel::core::sem::NumericPolicy::strict()
                   : camel::core::sem::NumericPolicy::relaxed(),
            rounds < 0 ? 0 : static_cast<size_t>(rounds), explain ? &record : nullptr);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        // The struct is rooted while the fallible box is made: that allocation can collect.
        Struct *run = Struct::create(8, mm::autoSpace());
        run->set(0, toSlot(const_cast<camel::runtime::Program *>(application.program)));
        run->set(1, toSlot<Int64>(static_cast<int64_t>(application.stats.applied)));
        run->set(2, toSlot<Int64>(static_cast<int64_t>(application.stats.rounds)));
        run->set(3, toSlot<Int64>(static_cast<int64_t>(application.stats.visited)));
        run->set(4, toSlot<Int64>(static_cast<int64_t>(application.stats.erased)));
        run->set(5, toSlot<Bool>(application.stats.fixpoint));
        run->set(6, toSlot<Float64>(seconds));
        run->set(7, NullSlot);
        mm::RootHandle root(mm::autoSpace(), run, runType, "rewrite.run");
        if (explain) {
            // The proof allocation can collect; the run is rooted, and the program it produced is
            // already in its first slot, so both survive while the proof is made.
            ProofObject *proof =
                ProofObject::create(record, application.stats, mm::autoSpace());
            mm::RootHandle proofRoot(
                mm::autoSpace(), proof, ProofType::get(), "rewrite.proof");
            run->set(7, makeSome(toSlot(proof), proofType));
        }
        return makeOk(toSlot(root.getAs<Struct>()), resultType);
    } catch (const camel::runtime::GraphCommitConflict &e) {
        // As in `rewrite`: a condition that commits replaces the program this run started from,
        // and the caller's own rules are what did it.
        return makeError("RewriteConflict", e.what(), "<rewrite::run>");
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::run>");
    } catch (const std::invalid_argument &e) {
        return makeError("RuleTwoWay", e.what(), "<rewrite::run>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    }
}

namespace {
/// The draft node id a handle names: the inverse of the map the run recorded for a node of the
/// program it produced, and the node's own id for a node of the program it started from (a
/// decoded draft numbers its nodes exactly as the program it was decoded from, draft.cpp). Null
/// for a handle of another program, or of a value no step of the run named.
std::optional<camel::runtime::gc_node_ref_t> draftNodeOf(
    const RewriteRecord &record, const camel::runtime::Program *input,
    const camel::runtime::Program *result, const camel::graphmod::Handle *handle) {
    const auto found = record.nodeRefsByGraph.find(handle->graph());
    if (found == record.nodeRefsByGraph.end()) {
        return std::nullopt;
    }
    if (handle->program() == result) {
        for (size_t draft = 0; draft < found->second.size(); ++draft) {
            if (found->second[draft] == handle->index()) {
                return static_cast<camel::runtime::gc_node_ref_t>(draft);
            }
        }
        return std::nullopt;
    }
    if (handle->program() == input && handle->index() < found->second.size()) {
        return static_cast<camel::runtime::gc_node_ref_t>(handle->index());
    }
    return std::nullopt;
}
} // namespace

/// The shortest chain of rewrites of `record` that connects `a` and `b`, over the steps as
/// undirected edges: a rewrite is an equality, so a chain may read a step backwards (a value that
/// was replaced is a writing of the value that replaced it, not just the other way round). When
/// two chains are equally short, both are returned, in schedule order — the order of the step with
/// the smallest index each chain uses, then the next, so the answer does not depend on how the
/// graph happens to be stored.
///
/// The answer refuses to guess: a value the run never rewrote has no chain to anything but itself,
/// and the proof prints why, rather than an empty chain. A chain may pass through a value that was
/// rewritten away later; that value is named by the text the step recorded, and the step that
/// reads it backwards is marked in the print.
ProofObject::Chain findChain(
    const RewriteRecord &record, const camel::runtime::Program *input,
    const camel::runtime::Program *result, const camel::graphmod::Handle *a,
    const camel::graphmod::Handle *b) {
    using namespace camel::runtime;
    ProofObject::Chain chain;
    chain.from = std::format("%{}", a->index());
    chain.to   = std::format("%{}", b->index());
    if (a->graph() != b->graph()) {
        chain.none = std::format(
            "the two values are in different graphs (%{} in graph {} and %{} in graph {}): no "
            "rewrite connects them",
            a->index(), camel::runtime::toText(a->graph()), b->index(),
            camel::runtime::toText(b->graph()));
        return chain;
    }
    const auto startDraft = draftNodeOf(record, input, result, a);
    const auto goalDraft  = draftNodeOf(record, input, result, b);
    if (!startDraft || !goalDraft) {
        chain.none = std::format(
            "the run made no rewrite at {}: it is not a value any step of the run matched or "
            "produced, so no chain starts there",
            startDraft ? chain.to : chain.from);
        return chain;
    }
    if (*startDraft == *goalDraft) {
        chain.same = true;
        return chain;
    }
    struct Edge {
        gc_node_ref_t other;
        size_t step;
        bool reverse;
    };
    std::unordered_map<gc_node_ref_t, std::vector<Edge>> adjacency;
    for (size_t i = 0; i < record.steps.size(); ++i) {
        const RewriteStep &step = record.steps[i];
        if (step.graphId != a->graph() || step.matched.empty()) {
            continue;
        }
        const gc_node_ref_t lhs = step.matched.front();
        const gc_node_ref_t rhs = step.produced;
        if (lhs == kInvalidNodeRef || rhs == kInvalidNodeRef || lhs == rhs) {
            continue;
        }
        adjacency[lhs].push_back({rhs, i, false});
        adjacency[rhs].push_back({lhs, i, true});
    }
    // Breadth first, so the first time the goal is reached is the shortest chain. Every shortest
    // predecessor is kept, which is how the equally short chains are enumerated afterwards.
    std::unordered_map<gc_node_ref_t, size_t> distance;
    std::unordered_map<gc_node_ref_t, std::vector<std::pair<gc_node_ref_t, Edge>>> from;
    std::deque<gc_node_ref_t> queue;
    distance[*startDraft] = 0;
    queue.push_back(*startDraft);
    while (!queue.empty()) {
        const gc_node_ref_t node = queue.front();
        queue.pop_front();
        const auto found = adjacency.find(node);
        if (found == adjacency.end()) {
            continue;
        }
        for (const Edge &edge : found->second) {
            const auto seen = distance.find(edge.other);
            if (seen == distance.end()) {
                distance[edge.other] = distance[node] + 1;
                from[edge.other].push_back({node, edge});
                queue.push_back(edge.other);
            } else if (seen->second == distance[node] + 1) {
                from[edge.other].push_back({node, edge});
            }
        }
    }
    const auto reached = distance.find(*goalDraft);
    if (reached == distance.end()) {
        chain.none = std::format(
            "no chain of rewrites connects {} and {}: the run made no step that turns one into the "
            "other, directly or through a value it rewrote in between",
            chain.from, chain.to);
        return chain;
    }
    // At most this many equally short chains are printed; a run that ties more often than this is
    // reported with the first ones rather than with a proof nobody can read.
    constexpr size_t kMaxChains = 16;
    ProofObject::ChainPath reversed;
    std::vector<ProofObject::ChainPath> paths;
    std::function<void(gc_node_ref_t)> walk = [&](gc_node_ref_t node) {
        if (paths.size() >= kMaxChains) {
            return;
        }
        if (node == *startDraft) {
            paths.emplace_back(reversed.rbegin(), reversed.rend());
            return;
        }
        const auto found = from.find(node);
        if (found == from.end()) {
            return;
        }
        for (const auto &[previous, edge] : found->second) {
            if (distance[previous] + 1 != distance[node]) {
                continue;
            }
            reversed.push_back({edge.step, edge.reverse});
            walk(previous);
            reversed.pop_back();
        }
    };
    walk(*goalDraft);
    std::sort(
        paths.begin(), paths.end(),
        [](const ProofObject::ChainPath &x, const ProofObject::ChainPath &y) {
            for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
                if (x[i].step != y[i].step) {
                    return x[i].step < y[i].step;
                }
            }
            return x.size() < y.size();
        });
    chain.paths = std::move(paths);
    if (!chain.paths.empty()) {
        const ProofObject::ChainPath &first = chain.paths.front();
        chain.from = linkBefore(record.steps[first.front().step], first.front().reverse);
        chain.to   = linkAfter(record.steps[first.back().step], first.back().reverse);
    }
    return chain;
}

/// `explain_all(r)`: the full record of the run `r` (rewrite design 8.7), every step it made in
/// the order it made them. This is the debugging view; `explain` narrows the same record to the
/// chain between two values.
slot_t explainAllKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *run    = norm.get<Struct *>(0);
    const slot_t field = run->slotAt(7);
    if (isNullValue(field)) {
        return makeError(
            "NoAttribution",
            "the run was made without attribution: call run(..., explain: true) to record it",
            "<rewrite::explain_all>");
    }
    const TupleType *resultType = TupleType::fallible(ProofType::get());
    auto *full                  = fromSlot<ProofObject *>(nullablePayload(field));
    ProofObject *copy           = ProofObject::create(full->record(), full->stats(), mm::autoSpace());
    mm::RootHandle root(mm::autoSpace(), copy, ProofType::get(), "rewrite.proof");
    return makeOk(toSlot(copy), resultType);
}

/// `explain(r, a, b)`: the shortest chain of rewrites of the run `r` that connects the values `a`
/// and `b` (rewrite design 8.7). Only that chain is printed — the full record stays available as
/// `explain_all` — and a pair no chain connects is an error value, never an empty chain.
slot_t explainKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *run    = norm.get<Struct *>(0);
    const slot_t field = run->slotAt(7);
    if (isNullValue(field)) {
        return makeError(
            "NoAttribution",
            "the run was made without attribution: call run(..., explain: true) to record it",
            "<rewrite::explain>");
    }
    const auto *a = norm.get<const camel::graphmod::Handle *>(1);
    const auto *b = norm.get<const camel::graphmod::Handle *>(2);
    if (a == nullptr || b == nullptr || a->kind() != camel::graphmod::HandleKind::Node ||
        b->kind() != camel::graphmod::HandleKind::Node) {
        return makeError(
            "InvalidArgument", "explain reads two nodes of the program the run produced",
            "<rewrite::explain>");
    }
    auto *full = fromSlot<ProofObject *>(nullablePayload(field));
    const camel::runtime::Program *program =
        fromSlot<const camel::runtime::Program *>(run->slotAt(0));
    const camel::runtime::Program *input = full->input();
    const auto known                      = [&](const camel::graphmod::Handle *h) {
        return h->program() == program || (input != nullptr && h->program() == input);
    };
    if (!known(a) || !known(b)) {
        return makeError(
            "InvalidArgument",
            "a names a node of a program the run neither started from nor produced: there is no "
            "record of it",
            "<rewrite::explain>");
    }
    ProofObject::Chain chain = findChain(full->record(), input, program, a, b);
    if (!chain.none.empty()) {
        return makeError("NoChain", chain.none, "<rewrite::explain>");
    }
    ProofObject *proof =
        ProofObject::createChain(full->record(), full->stats(), std::move(chain), mm::autoSpace());
    mm::RootHandle root(mm::autoSpace(), proof, ProofType::get(), "rewrite.proof");
    return makeOk(toSlot(proof), TupleType::fallible(ProofType::get()));
}

/// `why_not(r, rule, v)`: why `rule` did not fire at the value `v` of the run `r` (rewrite design
/// 8.7). The answer names the first place the left side fails to match with the value the program
/// has there, or the permission the value lacks, or the condition that is false with the values it
/// matched, or the rule that did fire and made `v`. It matches the way the run did: with the
/// same numeric policy and against the same program.
slot_t whyNotKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const auto *run    = norm.get<Struct *>(0);
    const slot_t field = run->slotAt(7);
    if (isNullValue(field)) {
        return makeError(
            "NoAttribution",
            "the run was made without attribution: call run(..., explain: true) to record it",
            "<rewrite::why_not>");
    }
    const auto *rule  = norm.get<RuleObject *>(1);
    const auto *value = norm.get<const camel::graphmod::Handle *>(2);
    if (value == nullptr || value->kind() != camel::graphmod::HandleKind::Node) {
        return makeError(
            "InvalidArgument", "why_not reads a node of the program the run produced",
            "<rewrite::why_not>");
    }
    if (value->program() != fromSlot<const camel::runtime::Program *>(run->slotAt(0))) {
        return makeError(
            "InvalidArgument",
            "why_not reads a node of the program the run produced; a value the run started from is "
            "followed with explain, which names the value it became",
            "<rewrite::why_not>");
    }
    auto *full                  = fromSlot<ProofObject *>(nullablePayload(field));
    const RewriteRecord &record = full->record();
    // The record is the proof's own copy of the run's log, on the C++ heap: this reference stays
    // good whatever the collector does with the proof itself.
    //
    // Everything else this kernel reads lives on the GC heap, and every call below can collect -- a
    // generic rule's instance is made here and a rule's `when` condition runs here -- so each is
    // read again from where the collector keeps it, which is rule 2 of
    // docs/technical/36_gc_handle_rules.md. The rule is the address this kernel used to keep, and
    // the one the fix in `callWhen` did not reach (6e0eb52e4): under `CAMEL_GC_POISON=1` the
    // collector wrote its poison word where the rule had been, and `rule->name()` in the answer at
    // the end of this function read that word -- SIGSEGV with `nameId` 0xf3900000, measured
    // 2026-10-24 on rules.why_not_names_the_false_condition. The rule is rooted for the length of
    // the kernel now and read from the handle at every use; the program and its graphs are read
    // from the frame again wherever a collecting call stands between the read and the use; and the
    // value's graph and index are read once, as numbers.
    mm::RootHandle ruleRoot(
        mm::autoSpace(), const_cast<RuleObject *>(rule), RuleType::get(), "rewrite.whyNotRule");
    const auto ruleNow = [&ruleRoot]() { return ruleRoot.getAs<RuleObject>(); };
    const auto runNow  = [&norm]() { return norm.get<Struct *>(0); };
    const auto programNow = [&runNow]() {
        return fromSlot<const camel::runtime::Program *>(runNow()->slotAt(0));
    };
    const camel::runtime::GraphId graphId = value->graph();
    const uint32_t valueIndex             = value->index();
    const auto draftNode = draftNodeOf(record, nullptr, programNow(), value);
    const TupleType *resultType = TupleType::fallible(Type::String());
    const auto answer = [&](std::string text) -> slot_t {
        String *out = String::from(std::move(text), mm::autoSpace());
        mm::RootHandle root(mm::autoSpace(), out, Type::String(), "rewrite.why_not");
        return makeOk(toSlot(out), resultType);
    };
    if (!draftNode) {
        return answer(std::format(
            "the run made no rewrite at %{}: the value is not one any step matched or produced",
            valueIndex));
    }
    // A value the run produced has a step behind it: the question "why not" is answered by saying
    // which rewrite did happen, and by which rule.
    // `answer` allocates, so the iteration below reads the step it answers about and returns: the
    // record's iterators are not read after that allocation (the record itself does not move).
    for (const camel::runtime::RewriteStep &step : record.steps) {
        if (step.graphId == graphId && step.produced == *draftNode) {
            const std::string before =
                step.matchedText.empty() ? std::string("%?") : step.matchedText.front();
            if (step.rule == ruleNow()->name()) {
                return answer(std::format(
                    "the rule fired here in round {} of graph {}: {} was made from {}",
                    step.round, step.graph, step.producedText, before));
            }
            if (step.rule.starts_with(std::string(ruleNow()->name()) + "<")) {
                // The step came from an instance of this generic rule: the answer names it, since
                // which types an instance was made for is the one thing a generic rule's report
                // has to say (the ruling of 2026-10-23 on generic-rule-instantiation).
                return answer(std::format(
                    "the rule fired here in round {} of graph {} as `{}`: {} was made from {}",
                    step.round, step.graph, step.rule, step.producedText, before));
            }
            return answer(std::format(
                "another rule fired here in round {} of graph {}: {} was made from {} by `{}`",
                step.round, step.graph, step.producedText, before, step.rule));
        }
    }
    if (programNow() == nullptr || programNow()->graph(graphId) == nullptr) {
        return answer(std::format(
            "the graph {} the value belongs to is not in the program the run produced",
            camel::runtime::toText(graphId)));
    }
    try {
        if (ruleNow()->ruleId() != 0) {
            // A generic rule has no sides of its own: what the value's type has is an instance,
            // and the question is about it (the ruling of 2026-10-23 on
            // generic-rule-instantiation). Which types the rule was instantiated at, and what came
            // of each, is what a reader of a generic rule needs to know.
            const camel::runtime::GCGraph *graph = programNow()->graph(graphId);
            const Type *type =
                graph != nullptr ? graph->node(valueIndex)->dataType() : nullptr;
            if (type == nullptr) {
                return answer(std::format(
                    "`{}` is a generic rule, and the type of this value is not known here",
                    ruleNow()->name()));
            }
const uint64_t ruleId = ruleNow()->ruleId();
            // What the messages below say about the type is read before the instance is made:
            // making it runs the compiler, which can collect, and `type` is an address on the GC
            // heap.
            const std::string typeText = type->toString();
            // A rule of one type parameter is named by this value's type; a rule of more is not,
            // and the tuple that could hold this value is looked for among the registered types
            // (one candidate per position the value's type can stand at). The reason a candidate
            // could not be made is then the body's own reason -- `genericRuleInstance` asked for
            // one type where the rule writes two says only that the numbers differ, which is not
            // what a reader of a failed law needs to hear (the ruling of 2026-10-23 on
            // generic-rule-instantiation-set-and-predicate, §3: `why_not` is where the reason is
            // read).
            GenericRuleEntry *entry = camel::compile::gir::genericRuleEntry(ctx, ruleId);
            const size_t arity = entry != nullptr && !entry->typeParams.empty()
                                     ? entry->typeParams.size()
                                     : 1;
            std::string firstTuple;
            std::string firstWhy;
            size_t tried = 0;
            const std::vector<std::vector<Type *>> candidates =
                tuplesHolding(type, arity);
            for (const std::vector<Type *> &tuple : candidates) {
                ++tried;
                const size_t index = camel::compile::gir::genericRuleInstance(ctx, ruleId, tuple);
                // Making an instance can collect: the rule value, the entry and the types are read
                // from where they live, again.
                entry = camel::compile::gir::genericRuleEntry(ctx, ruleId);
                if (entry == nullptr || index >= entry->instances.size()) {
                    continue;
                }
                const GenericRuleInstance &instance = entry->instances[index];
                if (!instance.made) {
                    if (firstWhy.empty()) {
                        firstTuple = instance.typeText;
                        firstWhy   = instance.why;
                    }
                    continue;
                }
                // The first tuple that was made is the instance the question is about: the answer
                // names it and says why it did not fire at this value.
                const std::string instanceText = instance.typeText;
                const std::vector<camel::runtime::Rule> &compiled =
                    instanceRules(ctx, *ruleNow(), *entry, index);
                if (compiled.empty()) {
                    return answer(std::format(
                        "`{}<{}>` compiles to no rule", ruleNow()->name(), instanceText));
                }
                camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), programNow());
                camel::runtime::GraphDraft &draft =
                    session.edit(const_cast<camel::runtime::GCGraph *>(programNow()->graph(graphId)));
                camel::runtime::DraftWritings writings(
                    draft, record.strict ? camel::core::sem::NumericPolicy::strict()
                                         : camel::core::sem::NumericPolicy::relaxed());
                const camel::runtime::RuleMiss miss = writings.whyNot(compiled.front(), *draftNode);
                return answer(std::format(
                    "`{}<{}>` did not fire at {}: {}",
                    ruleNow()->name(),
                    instanceText,
                    writings.textOf(*draftNode),
                    miss.detail));
            }
            if (firstWhy.empty()) {
                return answer(std::format(
                    "`{}` is a generic rule, and no instance of it could be made for '{}'",
                    ruleNow()->name(), typeText));
            }
            if (arity <= 1) {
                // One type parameter: the tuple the value's type makes is the tuple, and the reason
                // is the whole answer.
                return answer(std::format(
                    "`{}` is a generic rule, and no instance of it could be made for '{}': {}",
                    ruleNow()->name(), typeText, firstWhy));
            }
            // Two or more: the question gave one value, and which tuple of the registered types
            // that value stood at is what the reader has to be told to judge the reason.
            return answer(std::format(
                "`{}` is a generic rule, and no instance of it could be made for '{}': "
                "`{}<{}>` is the first of the {} type tuple(s) that could hold it, and it did not "
                "come about: {}",
                ruleNow()->name(), typeText, ruleNow()->name(), firstTuple, tried, firstWhy));
        }
        std::shared_ptr<WhenCall> call;
        if (ruleNow()->when() != nullptr) {
            call       = std::make_shared<WhenCall>();
            call->rule = std::make_shared<mm::RootHandle>(
                mm::autoSpace(), const_cast<RuleObject *>(ruleNow()), RuleType::get(),
                "rewrite.whyNotRule");
            call->vm = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
        }
        const camel::runtime::RuleSource source = sourceOf(*ruleNow(), 1, call);
        std::vector<camel::runtime::Rule> compiled = camel::runtime::compileRule(ctx, source);
        if (compiled.empty()) {
            return answer(std::format("`{}` compiles to no rule", ruleNow()->name()));
        }
        camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), programNow());
        camel::runtime::GraphDraft &draft =
            session.edit(const_cast<camel::runtime::GCGraph *>(programNow()->graph(graphId)));
        camel::runtime::DraftWritings writings(
            draft, record.strict ? camel::core::sem::NumericPolicy::strict()
                                 : camel::core::sem::NumericPolicy::relaxed());
        const camel::runtime::RuleMiss miss = writings.whyNot(compiled.front(), *draftNode);
        return answer(std::format(
            "`{}` did not fire at {}: {}", ruleNow()->name(), writings.textOf(*draftNode),
            miss.detail));
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::why_not>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    }
}

/// `saturate(p, rules, rounds)` and `saturate(p, rules, rounds, strict)`: the program with the
/// rules applied both ways to the writings of every value, adding writings until nothing new is
/// found or `rounds` rounds were made (rewrite design 3, graph store 3.5). The last parameter is
/// the pass's numeric policy, as for `rewrite`. The result has values with several writings:
/// `extract` (or the pipeline, before a pass that runs or translates it) chooses one.
slot_t saturateKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const int64_t rounds        = norm.get<Int64>(2);
    const bool strict           = norm.size() > 3 && norm.get<Bool>(3);
    try {
        const auto compiled = engineRules(ctx, norm, 1, base);
        camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), base);
        session.setGraphState(camel::runtime::GraphState::Tabled);
        // A condition is Camel code, and so is a safepoint: every draft this session has opened
        // must keep its static values relocatable until the commit reads them again, not only the
        // one the engine is saturating now.
        camel::runtime::RuntimeGraphDraftSession::RootScope draftRoots(session);
        camel::runtime::Budget budget;
        budget.rounds = rounds < 0 ? 0 : static_cast<size_t>(rounds);
        for (camel::runtime::GCGraph *graph : base->graphs()) {
            if (isRuleSide(*graph)) {
                continue;
            }
            camel::runtime::GraphDraft &draft = session.edit(graph);
            camel::runtime::DraftWritings writings(
                draft,
                strict ? camel::core::sem::NumericPolicy::strict()
                       : camel::core::sem::NumericPolicy::relaxed());
            writings.build();
            writings.saturate(compiled, budget);
            writings.save();
        }
        const camel::runtime::Program *committed = session.commitChecked();
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(committed)), resultType);
    } catch (const camel::runtime::GraphCommitConflict &e) {
        // As in `rewrite`: a condition that commits replaces the program this saturation started
        // from, and the caller's own rules are what did it.
        return makeError("RewriteConflict", e.what(), "<rewrite::saturate>");
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::saturate>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    }
}

/// `extract(p, cost)`: one writing for every value, chosen by the cost model of that name.
slot_t extractKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const std::string cost(norm.get<String *>(1)->view());
    const auto model = camel::runtime::findCostModel(cost);
    if (!model) {
        return makeError(
            "NoCostModel",
            "There is no cost model '" + cost + "'.",
            "<rewrite::extract>");
    }
    const camel::runtime::Program *extracted =
        camel::runtime::extractWritings(ctx.shared_from_this(), base, *model);
    return makeOk(toSlot(const_cast<camel::runtime::Program *>(extracted)), resultType);
}

// ---- the check of a rule (graph store decision 5) ----

/// A tensor sample: its elements as bytes, so it outlives the calls that follow. A collection may
/// move the Array a value was made in, and the next call reads the sample again; the copy has no
/// references into the GC heap.
struct TensorSample {
    TypeCode dtype = TypeCode::Float32;
    std::vector<int64_t> shape;
    std::vector<uint8_t> bytes;
};

/// A value of a parameter type, as the operators read it. `tensor` is set for a tensor type, whose
/// slot is made afresh right before each call (`Sampler::materialize`).
struct Sample {
    TypeCode code       = TypeCode::Void;
    slot_t slot         = 0;
    double asDouble     = 0.0;
    std::shared_ptr<TensorSample> tensor;
};

/// The element type a tensor dtype holds, as the type an Array of it is made with. Every element
/// type a tensor stores is here, the two reduced-precision floating types included (a `bf16` or
/// an `f16` element is two bytes, camel/core/rtdata/half.h): a dtype this returns nothing for is
/// one the check cannot make a value of, and it says so by name rather than widening it to f32.
const Type *tensorElementType(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return Type::Float32();
    case TypeCode::BFloat16:
        return Type::BFloat16();
    case TypeCode::Float16:
        return Type::Float16();
    case TypeCode::Int64:
        return Type::Int64();
    case TypeCode::Bool:
        return Type::Bool();
    default:
        return nullptr;
    }
}

/// The bytes one element of a tensor dtype takes.
size_t elementBytesOf(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float64:
    case TypeCode::Int64:
        return 8;
    case TypeCode::Float32:
    case TypeCode::Int32:
        return 4;
    case TypeCode::BFloat16:
    case TypeCode::Float16:
        return 2;
    default:
        return 1;
    }
}

/// One element of a tensor sample, as a double. A bf16 or an f16 element is widened to the f32 it
/// is exactly (camel/core/rtdata/half.h), which is the value the operator the rule was written
/// with saw: the check never compares the two-byte pattern of an element whose arithmetic is f32.
double elementOfBytes(TypeCode dtype, const uint8_t *at) {
    switch (dtype) {
    case TypeCode::Float64: {
        double value = 0.0;
        std::memcpy(&value, at, sizeof(double));
        return value;
    }
    case TypeCode::Float32: {
        float value = 0.0F;
        std::memcpy(&value, at, sizeof(float));
        return value;
    }
    case TypeCode::Int32: {
        int32_t value = 0;
        std::memcpy(&value, at, sizeof(int32_t));
        return static_cast<double>(value);
    }
    case TypeCode::Int64: {
        int64_t value = 0;
        std::memcpy(&value, at, sizeof(int64_t));
        return static_cast<double>(value);
    }
    case TypeCode::BFloat16: {
        uint16_t bits = 0;
        std::memcpy(&bits, at, sizeof(bits));
        return static_cast<double>(halfToFloat<BFloat16>(halfSlotOfBits(bits)));
    }
    case TypeCode::Float16: {
        uint16_t bits = 0;
        std::memcpy(&bits, at, sizeof(bits));
        return static_cast<double>(halfToFloat<Float16>(halfSlotOfBits(bits)));
    }
    default:
        return *at != 0 ? 1.0 : 0.0;
    }
}

/// The text of one element of a bf16 or an f16 tensor sample: the element type's own shortest
/// text (camel/core/rtdata/half.h, `halfText`), never the f32 text of the value it widens to
/// (design principles 7.7; the ruling of 2026-10-23 on `reduced-precision-text`).
std::string halfTextOf(const TensorSample &t, size_t at) {
    uint16_t bits = 0;
    std::memcpy(&bits, t.bytes.data() + at * 2, sizeof(bits));
    return t.dtype == TypeCode::BFloat16 ? halfText(BFloat16::fromBits(bits))
                                         : halfText(Float16::fromBits(bits));
}

/// The least positive subnormal of a floating element type, in the type's own value. A rounding
/// below the smallest normal is absolute rather than relative, so the error bound carries one such
/// step per rounding; a type that has none here reads as 0 and the term drops out.
double smallestSubnormalOf(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::BFloat16:
        return 0x1p-133; // bf16 has f32's exponent range: 2^-133 is its least subnormal
    case TypeCode::Float16:
        return 0x1p-24; // the least subnormal of binary16
    case TypeCode::Float64:
        return 4.9e-324;
    default:
        return 1.4e-45;
    }
}

/// The epsilon two floating sides of an element type are compared with. It is the element type's
/// own (camel::core::sem::elementEpsilon: bf16 2^-8, f16 2^-10, and the reduced types' instances
/// are what this lane exists to make checkable), except that f32 and f64 keep the two constants
/// the check has always measured their verdicts with. Those two are one *full* ulp of the type
/// (1.2e-7 = 2^-23, 2.3e-16 = 2^-52) where `elementEpsilon` declares the half of it (2^-24,
/// 2^-53), and a lane may neither loosen an f32 bound nor move an f32 or f64 reading: changing
/// them halves every f32 bound and belongs to a numeric-contract change of its own.
double checkEpsilon(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return 1.2e-7;
    case TypeCode::Float64:
        return 2.3e-16;
    default:
        return camel::core::sem::elementEpsilon(dtype);
    }
}

// ---- the higher-precision reference of the check ------------------------------------------

/// One value of the reference: a scalar or a row-major tensor, in long double. A rule that asks
/// for `refine_accuracy` is judged by the error of each side against this, not by bits, so the
/// reference must hold more precision than the element type being judged.
struct RefTensor {
    std::vector<int64_t> shape;
    std::vector<long double> values;
};

struct RefValue {
    bool tensor        = false;
    long double scalar = 0.0L;
    RefTensor t;

    static RefValue ofScalar(long double value) {
        RefValue out;
        out.scalar = value;
        return out;
    }
    static RefValue ofTensor(RefTensor value) {
        RefValue out;
        out.tensor = true;
        out.t = std::move(value);
        return out;
    }
};

/// The row-major strides of a shape.
std::vector<int64_t> refStrides(const std::vector<int64_t> &shape) {
    std::vector<int64_t> strides(shape.size(), 1);
    for (size_t i = shape.size(); i-- > 1;) {
        strides[i - 1] = strides[i] * shape[i];
    }
    return strides;
}

/// `value` with `fn` applied to every scalar or every element.
template <typename Fn> RefValue refMap(const RefValue &value, Fn &&fn) {
    if (!value.tensor) {
        return RefValue::ofScalar(fn(value.scalar));
    }
    RefTensor out;
    out.shape = value.t.shape;
    out.values.resize(value.t.values.size());
    for (size_t i = 0; i < out.values.size(); ++i) {
        out.values[i] = fn(value.t.values[i]);
    }
    return RefValue::ofTensor(std::move(out));
}

/// `fn(a, b)` with the same broadcasting the tensor operators give: a scalar applies to every
/// element, and two tensors are aligned from the right.
RefValue refBinary(
    const RefValue &a, const RefValue &b,
    const std::function<long double(long double, long double)> &fn) {
    if (!a.tensor && !b.tensor) {
        return RefValue::ofScalar(fn(a.scalar, b.scalar));
    }
    if (a.tensor && !b.tensor) {
        return refMap(a, [&](long double x) { return fn(x, b.scalar); });
    }
    if (!a.tensor && b.tensor) {
        return refMap(b, [&](long double y) { return fn(a.scalar, y); });
    }
    const size_t rank = std::max(a.t.shape.size(), b.t.shape.size());
    std::vector<int64_t> aShape(rank, 1), bShape(rank, 1), outShape(rank, 1);
    for (size_t i = 0; i < a.t.shape.size(); ++i) {
        aShape[rank - a.t.shape.size() + i] = a.t.shape[i];
    }
    for (size_t i = 0; i < b.t.shape.size(); ++i) {
        bShape[rank - b.t.shape.size() + i] = b.t.shape[i];
    }
    for (size_t i = 0; i < rank; ++i) {
        if (aShape[i] != bShape[i] && aShape[i] != 1 && bShape[i] != 1) {
            throw std::invalid_argument("the reference cannot broadcast the two shapes");
        }
        outShape[i] = std::max(aShape[i], bShape[i]);
    }
    const std::vector<int64_t> aStrides = refStrides(aShape);
    const std::vector<int64_t> bStrides = refStrides(bShape);
    RefTensor out;
    out.shape = outShape;
    int64_t count = 1;
    for (int64_t extent : outShape) {
        count *= extent;
    }
    out.values.resize(static_cast<size_t>(count));
    std::vector<int64_t> coords(rank, 0);
    for (int64_t flat = 0; flat < count; ++flat) {
        int64_t rest = flat;
        for (size_t d = rank; d-- > 0;) {
            coords[d] = rest % outShape[d];
            rest /= outShape[d];
        }
        int64_t ia = 0, ib = 0;
        for (size_t d = 0; d < rank; ++d) {
            ia += (aShape[d] == 1 ? 0 : coords[d]) * aStrides[d];
            ib += (bShape[d] == 1 ? 0 : coords[d]) * bStrides[d];
        }
        out.values[static_cast<size_t>(flat)] =
            fn(a.t.values[static_cast<size_t>(ia)], b.t.values[static_cast<size_t>(ib)]);
    }
    return RefValue::ofTensor(std::move(out));
}

/// The axis a reduction over `rank` dimensions names, as a 0-based index.
size_t refAxis(int64_t axis, size_t rank) {
    int64_t normalized = axis < 0 ? axis + static_cast<int64_t>(rank) : axis;
    if (normalized < 0 || normalized >= static_cast<int64_t>(rank)) {
        throw std::invalid_argument(
            "axis " + std::to_string(axis) + " is out of range for a rank-" +
            std::to_string(rank) + " tensor");
    }
    return static_cast<size_t>(normalized);
}

enum class RefReduce { Sum, Max, Min, Mean };

/// The reduction of `x` over `axis`, keeping the reduced extent as 1 when `keep` is set.
RefValue refReduce(const RefTensor &x, int64_t axisArg, bool keep, RefReduce kind) {
    const size_t axis = refAxis(axisArg, x.shape.size());
    int64_t outer = 1, inner = 1;
    for (size_t d = 0; d < axis; ++d) {
        outer *= x.shape[d];
    }
    for (size_t d = axis + 1; d < x.shape.size(); ++d) {
        inner *= x.shape[d];
    }
    const int64_t extent = x.shape[axis];
    std::vector<int64_t> outShape;
    for (size_t d = 0; d < x.shape.size(); ++d) {
        if (d == axis) {
            if (keep) {
                outShape.push_back(1);
            }
        } else {
            outShape.push_back(x.shape[d]);
        }
    }
    RefTensor out;
    out.shape = outShape;
    out.values.resize(static_cast<size_t>(outer * inner));
    for (int64_t o = 0; o < outer; ++o) {
        for (int64_t i = 0; i < inner; ++i) {
            long double value = kind == RefReduce::Max   ? -std::numeric_limits<long double>::infinity()
                : kind == RefReduce::Min                 ? std::numeric_limits<long double>::infinity()
                                                         : 0.0L;
            for (int64_t e = 0; e < extent; ++e) {
                const long double element =
                    x.values[static_cast<size_t>(o * extent * inner + e * inner + i)];
                switch (kind) {
                case RefReduce::Sum:
                case RefReduce::Mean:
                    value += element;
                    break;
                case RefReduce::Max:
                    value = std::isnan(value) || std::isnan(element) ? std::numeric_limits<long double>::quiet_NaN()
                                                            : std::max(value, element);
                    break;
                case RefReduce::Min:
                    value = std::isnan(value) || std::isnan(element) ? std::numeric_limits<long double>::quiet_NaN()
                                                            : std::min(value, element);
                    break;
                }
            }
            if (kind == RefReduce::Mean && extent != 0) {
                value /= static_cast<long double>(extent);
            }
            out.values[static_cast<size_t>(o * inner + i)] = value;
        }
    }
    return RefValue::ofTensor(std::move(out));
}

/// `softmax(x, axis)` or `log_softmax(x, axis)` in the reference's precision, computed as its
/// definition: the maximum is subtracted first, then exp, sum and divide (or `(x - m) - log s`).
RefValue refSoftmax(const RefTensor &x, int64_t axisArg, bool logarithm) {
    const size_t axis = refAxis(axisArg, x.shape.size());
    int64_t outer = 1, inner = 1;
    for (size_t d = 0; d < axis; ++d) {
        outer *= x.shape[d];
    }
    for (size_t d = axis + 1; d < x.shape.size(); ++d) {
        inner *= x.shape[d];
    }
    const int64_t extent = x.shape[axis];
    RefTensor out;
    out.shape = x.shape;
    out.values.resize(x.values.size());
    for (int64_t o = 0; o < outer; ++o) {
        for (int64_t i = 0; i < inner; ++i) {
            long double maximum = -std::numeric_limits<long double>::infinity();
            for (int64_t e = 0; e < extent; ++e) {
                maximum = std::max(
                    maximum,
                    x.values[static_cast<size_t>(o * extent * inner + e * inner + i)]);
            }
            long double sum = 0.0L;
            for (int64_t e = 0; e < extent; ++e) {
                sum += std::exp(
                    x.values[static_cast<size_t>(o * extent * inner + e * inner + i)] - maximum);
            }
            const long double logSum = std::log(sum);
            for (int64_t e = 0; e < extent; ++e) {
                const long double element =
                    x.values[static_cast<size_t>(o * extent * inner + e * inner + i)];
                out.values[static_cast<size_t>(o * extent * inner + e * inner + i)] =
                    logarithm ? (element - maximum) - logSum : std::exp(element - maximum) / sum;
            }
        }
    }
    return RefValue::ofTensor(std::move(out));
}

/// The tensor of a sample as long double elements. A bf16 or an f16 element is the f32 it widens
/// to exactly, which is the value the operator the reference is a reference for computes on.
RefTensor tensorOfSample(const TensorSample &sample) {
    RefTensor out;
    out.shape = sample.shape;
    size_t count = 1;
    for (int64_t extent : sample.shape) {
        count *= static_cast<size_t>(extent < 0 ? 0 : extent);
    }
    out.values.resize(count);
    const size_t elementBytes = elementBytesOf(sample.dtype);
    for (size_t i = 0; i < count; ++i) {
        if (sample.bytes.size() < (i + 1) * elementBytes) {
            out.values[i] = 0.0L;
            continue;
        }
        out.values[i] = static_cast<long double>(
            elementOfBytes(sample.dtype, sample.bytes.data() + i * elementBytes));
    }
    return out;
}

/**
 * The higher-precision reference of one side of a rule: the side's expression evaluated over the
 * same argument values in long double. It reads the operators the accuracy library uses, and
 * refuses (rather than guesses) an operator it does not know, so a rule the check cannot examine
 * fails the check instead of passing it silently.
 */
class RefEvaluator {
  public:
    RefEvaluator(const camel::runtime::GCGraph &graph, const std::vector<Sample> &args)
        : graph_(graph), args_(args),
          ports_(graph.normPorts().begin(), graph.normPorts().end()) {}

    RefValue eval(camel::runtime::gc_node_ref_t ref) {
        const auto found = memo_.find(ref);
        if (found != memo_.end()) {
            return found->second;
        }
        RefValue value = evalUncached(ref);
        memo_.emplace(ref, value);
        return value;
    }

  private:
    RefValue evalUncached(camel::runtime::gc_node_ref_t ref) {
        const camel::runtime::GCNode *node = graph_.node(ref);
        if (node == nullptr || node->kind == camel::runtime::GCNodeKind::Dead) {
            throw std::runtime_error("the reference reads a node that is not there");
        }
        switch (node->kind) {
        case camel::runtime::GCNodeKind::Data:
            return constant(*node);
        case camel::runtime::GCNodeKind::Port:
            return argument(ref);
        case camel::runtime::GCNodeKind::Cast:
        case camel::runtime::GCNodeKind::Copy: {
            const auto inputs = graph_.normInputsOf(ref);
            if (inputs.empty()) {
                throw std::runtime_error("the reference reads a copy without an input");
            }
            return eval(inputs.front());
        }
        case camel::runtime::GCNodeKind::Oper:
            return operate(ref);
        default:
            throw std::runtime_error("the reference cannot evaluate this kind of node");
        }
    }

    RefValue constant(const camel::runtime::GCNode &node) {
        if (node.dataIndex >= 0) {
            throw std::runtime_error("the reference reads a value that is not a constant");
        }
        const size_t index = static_cast<size_t>(-node.dataIndex);
        if (index >= graph_.staticSlots().size()) {
            throw std::runtime_error("the reference reads a constant the graph does not have");
        }
        const Type *type = node.dataType();
        if (type == nullptr) {
            throw std::runtime_error("the reference reads a constant without a type");
        }
        const slot_t slot = graph_.staticSlots()[index];
        if (const auto shape = camel::runtime::fact::shapeOfType(type)) {
            const auto *array = fromSlot<Array *>(slot);
            if (array == nullptr) {
                throw std::runtime_error("the reference reads a tensor constant that is not an array");
            }
            TensorSample sample;
            sample.dtype = array->dtype();
            sample.shape.assign(array->shape(), array->shape() + array->rank());
            const auto *bytes = array->dataAs<uint8_t>();
            if (array->byteSize() != 0) {
                sample.bytes.assign(bytes, bytes + array->byteSize());
            }
            return RefValue::ofTensor(tensorOfSample(sample));
        }
        switch (type->code()) {
        case TypeCode::Float32:
            return RefValue::ofScalar(static_cast<long double>(fromSlot<Float32>(slot)));
        case TypeCode::Float64:
            return RefValue::ofScalar(static_cast<long double>(fromSlot<Float64>(slot)));
        case TypeCode::Int32:
            return RefValue::ofScalar(static_cast<long double>(fromSlot<Int32>(slot)));
        case TypeCode::Int64:
            return RefValue::ofScalar(static_cast<long double>(fromSlot<Int64>(slot)));
        case TypeCode::Bool:
            return RefValue::ofScalar(fromSlot<Bool>(slot) ? 1.0L : 0.0L);
        case TypeCode::BFloat16:
            // A constant of a reduced-precision type is the f32 it widens to, exactly: the
            // reference holds more precision than the element type, which is the one thing an
            // accuracy comparison needs of it.
            return RefValue::ofScalar(
                static_cast<long double>(halfToFloat<BFloat16>(slot)));
        case TypeCode::Float16:
            return RefValue::ofScalar(static_cast<long double>(halfToFloat<Float16>(slot)));
        default:
            throw std::runtime_error("the reference cannot read a constant of this type");
        }
    }

    RefValue argument(camel::runtime::gc_node_ref_t ref) {
        size_t index = 0;
        for (; index < ports_.size(); ++index) {
            if (ports_[index] == ref) {
                break;
            }
        }
        if (index >= ports_.size()) {
            throw std::runtime_error("the reference reads a port that is not a parameter");
        }
        const Sample &sample = args_[index];
        if (sample.tensor != nullptr) {
            return RefValue::ofTensor(tensorOfSample(*sample.tensor));
        }
        return RefValue::ofScalar(static_cast<long double>(sample.asDouble));
    }

    RefValue operate(camel::runtime::gc_node_ref_t ref) {
        if (!graph_.withInputsOf(ref).empty() || !graph_.ctrlInputsOf(ref).empty()) {
            throw std::runtime_error("the reference cannot read an operator with a context argument");
        }
        const auto *body = graph_.nodeBodyAs<camel::runtime::GCOperBody>(ref);
        std::vector<RefValue> in;
        for (camel::runtime::gc_node_ref_t input : graph_.normInputsOf(ref)) {
            in.push_back(eval(input));
        }
        const std::string uri(body->uri());
        const auto unary = [&](auto &&fn) {
            if (in.size() != 1) {
                throw std::runtime_error("the reference reads an operator with the wrong number of inputs");
            }
            return refMap(in[0], fn);
        };
        const auto binary = [&](auto &&fn) {
            if (in.size() != 2) {
                throw std::runtime_error("the reference reads an operator with the wrong number of inputs");
            }
            return refBinary(
                in[0],
                in[1],
                [&](long double a, long double b) { return fn(a, b); });
        };
        if (uri == ":op/add_f" || uri == ":op/add_d") {
            return binary([](long double a, long double b) { return a + b; });
        }
        if (uri == ":op/sub_f" || uri == ":op/sub_d") {
            return binary([](long double a, long double b) { return a - b; });
        }
        if (uri == ":op/mul_f" || uri == ":op/mul_d") {
            return binary([](long double a, long double b) { return a * b; });
        }
        if (uri == ":op/div_f" || uri == ":op/div_d") {
            return binary([](long double a, long double b) { return a / b; });
        }
        if (uri == ":op/neg_f" || uri == ":op/neg_d") {
            return unary([](long double a) { return -a; });
        }
        if (uri == "math:sqrt") {
            return unary([](long double a) { return std::sqrt(a); });
        }
        if (uri == "math:exp_f" || uri == "math:exp_d") {
            return unary([](long double a) { return std::exp(a); });
        }
        if (uri == "math:log_f" || uri == "math:log_d") {
            return unary([](long double a) { return std::log(a); });
        }
        if (uri == "math:log1p_f" || uri == "math:log1p_d") {
            return unary([](long double a) { return std::log1p(a); });
        }
        if (uri == "math:expm1_f" || uri == "math:expm1_d") {
            return unary([](long double a) { return std::expm1(a); });
        }
        if (uri == "math:abs_f" || uri == "math:abs_d") {
            return unary([](long double a) { return std::fabs(a); });
        }
        if (uri == "tensor:add") {
            return binary([](long double a, long double b) { return a + b; });
        }
        if (uri == "tensor:subtract") {
            return binary([](long double a, long double b) { return a - b; });
        }
        if (uri == "tensor:multiply") {
            return binary([](long double a, long double b) { return a * b; });
        }
        if (uri == "tensor:divide") {
            return binary([](long double a, long double b) { return a / b; });
        }
        if (uri == "tensor:maximum") {
            return binary([](long double a, long double b) { return std::max(a, b); });
        }
        if (uri == "tensor:minimum") {
            return binary([](long double a, long double b) { return std::min(a, b); });
        }
        if (uri == "tensor:neg") {
            return unary([](long double a) { return -a; });
        }
        if (uri == "tensor:abs") {
            return unary([](long double a) { return std::fabs(a); });
        }
        if (uri == "tensor:exp") {
            return unary([](long double a) { return std::exp(a); });
        }
        if (uri == "tensor:log") {
            return unary([](long double a) { return std::log(a); });
        }
        if (uri == "tensor:sqrt") {
            return unary([](long double a) { return std::sqrt(a); });
        }
        if (uri == "tensor:sum" && in.size() == 1) {
            long double total = 0.0L;
            for (long double value : in[0].t.values) {
                total += value;
            }
            return RefValue::ofScalar(total);
        }
        if (uri == "tensor:sum_axis" || uri == "tensor:max_axis" || uri == "tensor:min_axis" ||
            uri == "tensor:mean_axis") {
            if (in.size() < 2 || !in[0].tensor || in[1].tensor) {
                throw std::runtime_error("the reference reads a reduction without its axis");
            }
            const bool keep = in.size() > 2 && !in[2].tensor && in[2].scalar != 0.0L;
            const auto kind = uri == "tensor:sum_axis"    ? RefReduce::Sum
                : uri == "tensor:max_axis"                ? RefReduce::Max
                : uri == "tensor:min_axis"                ? RefReduce::Min
                                                          : RefReduce::Mean;
            return refReduce(in[0].t, static_cast<int64_t>(in[1].scalar), keep, kind);
        }
        if (uri == "tensor:softmax" || uri == "tensor:log_softmax") {
            if (in.empty() || !in[0].tensor) {
                throw std::runtime_error("the reference reads a softmax without its tensor");
            }
            const int64_t axis = in.size() > 1 && !in[1].tensor ? static_cast<int64_t>(in[1].scalar) : -1;
            return refSoftmax(in[0].t, axis, uri == "tensor:log_softmax");
        }
        throw std::runtime_error("the check has no higher-precision reference for operator '" + uri + "'");
    }

    const camel::runtime::GCGraph &graph_;
    const std::vector<Sample> &args_;
    std::vector<camel::runtime::gc_node_ref_t> ports_;
    std::unordered_map<camel::runtime::gc_node_ref_t, RefValue> memo_;
};

/// The normalized error of a native value against the reference: the difference relative to the
/// magnitude (absolutely when the value is small), with NaNs that agree and equal infinities
/// treated as exact.
long double refScalarError(long double native, long double reference) {
    if (std::isnan(native) || std::isnan(reference)) {
        return std::isnan(native) && std::isnan(reference)
            ? 0.0L
            : std::numeric_limits<long double>::infinity();
    }
    if (std::isinf(native) || std::isinf(reference)) {
        return native == reference ? 0.0L : std::numeric_limits<long double>::infinity();
    }
    if (reference == 0.0L) {
        return std::fabs(native);
    }
    // Relative error, not absolute: the benefit of `log1p` is its accuracy near zero, where an
    // absolute bound would call the 100% error of `log(1 + x)` invisible.
    return std::fabs(native - reference) / std::fabs(reference);
}

/// The largest normalized error of a sample against the reference of its side.
long double refSampleError(const Sample &native, const RefValue &reference) {
    if (native.tensor != nullptr) {
        if (!reference.tensor || reference.t.shape != native.tensor->shape) {
            return std::numeric_limits<long double>::infinity();
        }
        const RefTensor values = tensorOfSample(*native.tensor);
        long double worst      = 0.0L;
        for (size_t i = 0; i < values.values.size(); ++i) {
            worst = std::max(worst, refScalarError(values.values[i], reference.t.values[i]));
        }
        return worst;
    }
    if (reference.tensor) {
        return std::numeric_limits<long double>::infinity();
    }
    return refScalarError(static_cast<long double>(native.asDouble), reference.scalar);
}

/// The one rounding step the element type can make, the measurement noise of "not worse". f32 and
/// f64 keep the numbers this comparison has always used (their full ulp); the two reduced-precision
/// types take their own element epsilon (camel/core/operator_semantics.h, `elementEpsilon`:
/// bf16 2^-8, f16 2^-10), because no accuracy rule of the library has an instance at either of
/// them today and the next one that does must not be measured in f32's rounding.
double refTypeEpsilon(TypeCode code) {
    switch (code) {
    case TypeCode::Float32:
        return 1.1920928955078125e-7;
    case TypeCode::Float64:
        return 2.220446049250313e-16;
    default:
        return camel::core::sem::elementEpsilon(code);
    }
}

/// Whether the right side's error is not larger than the left side's, allowing one rounding step
/// of the element type so that the measurement itself does not make an accurate rule fail.
bool refNotWorse(long double left, long double right, TypeCode code) {
    if (std::isinf(left)) {
        return true; // both sides missed the same special value; the rewrite adds nothing
    }
    return right <= left + 2.0L * static_cast<long double>(refTypeEpsilon(code));
}

/// The reported ratio of the right side's error to the left side's, with the element type's two
/// ULP measurement floor so that an exact left side does not divide by zero.
double refErrorRatio(long double left, long double right, TypeCode code) {
    if (right == 0.0L) {
        return 0.0;
    }
    const long double floor = 2.0L * static_cast<long double>(refTypeEpsilon(code));
    return static_cast<double>(right / std::max(left, floor));
}

/// What the check reports for one sample of a rule that carries `refine_accuracy`.
struct AccuracyResult {
    bool ok = true;
    bool referenceAvailable = true;
    long double leftError = 0.0L;
    long double rightError = 0.0L;
    double ratio = 0.0;
    std::string reason;
};

/// One side of a rule as its real value: the side's expression over the same argument values,
/// evaluated in the reference's precision. Throws for an operator outside the reference's fragment
/// (rewrite 8.10), which each caller turns into what it can say instead.
RefValue realValueOf(const camel::runtime::GCGraph &graph, const std::vector<Sample> &values) {
    RefEvaluator evaluator(graph, values);
    return evaluator.eval(graph.exitNodeRef());
}

AccuracyResult compareAgainstReference(
    const camel::runtime::GCGraph &lhsGraph, const camel::runtime::GCGraph &rhsGraph,
    const std::vector<Sample> &values, const Sample &left, const Sample &right, TypeCode code) {
    AccuracyResult result;
    try {
        const RefValue referenceLeft  = realValueOf(lhsGraph, values);
        const RefValue referenceRight = realValueOf(rhsGraph, values);
        result.leftError  = refSampleError(left, referenceLeft);
        result.rightError = refSampleError(right, referenceRight);
    } catch (const std::exception &e) {
        result.ok               = false;
        result.referenceAvailable = false;
        result.reason           = e.what();
        return result;
    }
    result.ratio = refErrorRatio(result.leftError, result.rightError, code);
    result.ok = refNotWorse(result.leftError, result.rightError, code);
    return result;
}

AccuracyResult compareBeforeAfter(
    const camel::runtime::GCGraph &graph, const std::vector<Sample> &values,
    const Sample &before, const Sample &after, TypeCode code) {
    AccuracyResult result;
    try {
        const RefValue reference = realValueOf(graph, values);
        result.leftError         = refSampleError(before, reference);
        result.rightError        = refSampleError(after, reference);
    } catch (const std::exception &e) {
        result.ok                 = false;
        result.referenceAvailable = false;
        result.reason             = e.what();
        return result;
    }
    result.ratio = refErrorRatio(result.leftError, result.rightError, code);
    result.ok = refNotWorse(result.leftError, result.rightError, code);
    return result;
}

/// The element type a sample's result is measured in.
TypeCode elementCodeOf(const Sample &sample) {
    return sample.tensor != nullptr ? sample.tensor->dtype : sample.code;
}

/// Random values of one parameter type (the scalar ones and the tensors the shape reader knows).
/// Floats include the special values the permissions of the rule do not rule out.
class Sampler {
  public:
    Sampler(uint64_t seed, camel::core::sem::PermissionMask needs) : rng_(seed), needs_(needs) {}

    /// Whether `value` can make a sample of this type: the scalars it knows (int, i32, float, f32,
    /// `bf16`, `f16`, bool) and a tensor whose element type it knows. A rule instance whose
    /// parameters are of another type cannot be checked, and the report says so instead of failing
    /// the whole check (`the check cannot make a value of type '...'`).
    bool canDraw(const Type *type) const {
        const auto shape = camel::runtime::fact::shapeOfType(type);
        if (shape) {
            return tensorElementType(shape->dtype.value_or(TypeCode::Float32)) != nullptr;
        }
        switch (type->code()) {
        case TypeCode::Int64:
        case TypeCode::Int32:
        case TypeCode::Float64:
        case TypeCode::Float32:
        case TypeCode::BFloat16:
        case TypeCode::Float16:
        case TypeCode::Bool:
            return true;
        default:
            return false;
        }
    }

    /// A scalar or tensor sample of `type`. `rankHint` is used only when the type does not carry
    /// its rank: the caller derives it from the axis operands of the rule, so a reduction over
    /// axis 2 is given a rank-3 tensor instead of the old fixed rank-1 vector.
    Sample value(const Type *type, bool constant, size_t rankHint = 1) {
        const auto shape = camel::runtime::fact::shapeOfType(type);
        if (shape) {
            return tensor(*shape, constant, rankHint);
        }
        switch (type->code()) {
        case TypeCode::Int64: {
            const int64_t v = pickInt(constant);
            return {TypeCode::Int64, toSlot<Int64>(v), static_cast<double>(v), nullptr};
        }
        case TypeCode::Int32: {
            const int32_t v = static_cast<int32_t>(pickInt(constant));
            return {TypeCode::Int32, toSlot<Int32>(v), static_cast<double>(v), nullptr};
        }
        case TypeCode::Float64: {
            const double v = pickFloat(constant, TypeCode::Float64);
            return {TypeCode::Float64, toSlot<Float64>(v), v, nullptr};
        }
        case TypeCode::Float32: {
            const float v = static_cast<float>(pickFloat(constant, TypeCode::Float32));
            return {TypeCode::Float32, toSlot<Float32>(v), static_cast<double>(v), nullptr};
        }
        case TypeCode::BFloat16:
            return reducedFloat<BFloat16>(TypeCode::BFloat16, constant);
        case TypeCode::Float16:
            return reducedFloat<Float16>(TypeCode::Float16, constant);
        case TypeCode::Bool: {
            const bool v = (rng_() & 1) != 0;
            return {TypeCode::Bool, toSlot<Bool>(v), v ? 1.0 : 0.0, nullptr};
        }
        default:
            throw std::invalid_argument("a parameter of a rule has a type the check cannot make");
        }
    }

    /// A fixed integer sample, for an operand the sampler must choose (an axis) rather than draw.
    Sample fixedInt(const Type *type, int64_t value) {
        if (type->code() == TypeCode::Int32) {
            return {
                TypeCode::Int32,
                toSlot<Int32>(static_cast<int32_t>(value)),
                static_cast<double>(value),
                nullptr};
        }
        return {TypeCode::Int64, toSlot<Int64>(value), static_cast<double>(value), nullptr};
    }

    /// The sample as an argument slot. A scalar keeps its slot; a tensor is an Array made now,
    /// from the bytes of the sample. Call it right before the call that reads the argument: no
    /// allocation may run in between (the Array can move).
    slot_t materialize(Sample &sample) {
        if (sample.tensor == nullptr) {
            return sample.slot;
        }
        const TensorSample &t = *sample.tensor;
        const Type *element   = tensorElementType(t.dtype);
        if (element == nullptr) {
            throw std::invalid_argument("a tensor parameter has an element type the check cannot make");
        }
        auto *arrayType = ArrayType::create(const_cast<Type *>(element));
        Array *array    = Array::create(mm::autoSpace(), arrayType, t.shape, true);
        if (array->byteSize() != t.bytes.size()) {
            throw std::logic_error("a tensor sample and its array have different sizes");
        }
        if (!t.bytes.empty()) {
            std::memcpy(array->dataAs<uint8_t>(), t.bytes.data(), t.bytes.size());
        }
        sample.slot = toSlot(array);
        return sample.slot;
    }

  private:
    /// A sample of one of the two reduced-precision floating types. The drawn value is rounded
    /// once into the element type -- that rounding is the type's own store rounding, the same one
    /// a literal and a written conversion use -- and both the slot the argument is passed in and
    /// the double the check compares are that stored element, never the wider value it was drawn
    /// from. A check that compared the drawn value would be comparing a number the operator never
    /// saw.
    template <typename Half> Sample reducedFloat(TypeCode code, bool constant) {
        const slot_t slot = halfFromFloat<Half>(static_cast<float>(pickFloat(constant, code)));
        return {code, slot, static_cast<double>(halfToFloat<Half>(slot)), nullptr};
    }

    /// A tensor of the type's element type. The shape is the one the type carries; an unknown
    /// rank is a vector of three and an unknown extent is two. A type with a large known shape is
    /// sampled in the same rank at two elements per extent (elementwise laws do not depend on
    /// the extents, and the check stays small).
    Sample tensor(
        const camel::runtime::fact::TensorShape &shape, bool constant, size_t rankHint) {
        auto sample  = std::make_shared<TensorSample>();
        sample->dtype = shape.dtype.value_or(TypeCode::Float32);
        const TypeCode dtype = sample->dtype;
        const Type *element  = tensorElementType(dtype);
        if (element == nullptr) {
            throw std::invalid_argument(
                "a tensor parameter has an element type the check cannot make");
        }
        std::vector<int64_t> dims;
        if (shape.dims) {
            dims = *shape.dims;
        } else {
            // The rank comes from the operator's operands (an axis), and rotates through 1, 2 and
            // 3 so that every rule sees a vector, a matrix and a rank-3 tensor in one check.
            const size_t rank = std::clamp<size_t>(rankHint, 1, 4);
            if (rank == 1) {
                dims = {3};
            } else if (rank == 2) {
                dims = {2, 3};
            } else {
                dims = std::vector<int64_t>(rank, 2);
            }
        }
        int64_t elements = 1;
        for (int64_t &extent : dims) {
            if (extent < 0) {
                extent = 2;
            }
            elements *= extent;
        }
        if (elements > 1024) {
            for (int64_t &extent : dims) {
                extent = 2;
            }
            elements = 1;
            for (int64_t extent : dims) {
                elements *= extent;
            }
        }
        sample->shape = dims;
        switch (dtype) {
        case TypeCode::Float32: {
            // The bytes of a vector of bytes are not aligned for a float: fill a typed buffer and
            // copy it in (the same on the way out, `tensorElement`).
            std::vector<float> values(static_cast<size_t>(elements));
            for (float &value : values) {
                value = static_cast<float>(pickFloat(false, TypeCode::Float32));
            }
            // The measured counterexample for the stable softmax: a vector whose maximum is far
            // from one element. The stable form's `x - max` is then about -27, and rounding the
            // difference (and exp's argument scaling) makes its relative error about 1.9e-6,
            // while the naive form exponentiates values near zero and stays near 7.8e-8. The
            // first rank-1 accuracy tensor is this vector so the rejection is reproducible; the
            // rest of the domain is sampled as before.
            if (!usedFarApartProbe_ && needs_ == camel::core::sem::kRefineAccuracy &&
                values.size() == 3) {
                values = {-1e-16f, 27.004f, -33.563f};
                usedFarApartProbe_ = true;
            }
            sample->bytes.resize(values.size() * sizeof(float));
            std::memcpy(sample->bytes.data(), values.data(), sample->bytes.size());
            break;
        }
        case TypeCode::BFloat16:
        case TypeCode::Float16: {
            // Two bytes per element, in the element's own bit pattern (camel/core/rtdata/half.h;
            // the tensor module stores these two types the same way). Each element is rounded once
            // into the element type, exactly as a bf16 or f16 literal is.
            sample->bytes.resize(static_cast<size_t>(elements) * 2);
            for (int64_t i = 0; i < elements; ++i) {
                const float drawn = static_cast<float>(pickFloat(false, dtype));
                const uint16_t bits = dtype == TypeCode::BFloat16
                    ? halfBitsOfSlot(halfFromFloat<BFloat16>(drawn))
                    : halfBitsOfSlot(halfFromFloat<Float16>(drawn));
                std::memcpy(sample->bytes.data() + static_cast<size_t>(i) * 2, &bits, 2);
            }
            break;
        }
        case TypeCode::Int64: {
            std::vector<int64_t> values(static_cast<size_t>(elements));
            for (int64_t &value : values) {
                value = pickInt(false);
            }
            sample->bytes.resize(values.size() * sizeof(int64_t));
            std::memcpy(sample->bytes.data(), values.data(), sample->bytes.size());
            break;
        }
        case TypeCode::Bool: {
            sample->bytes.resize(static_cast<size_t>(elements));
            for (int64_t i = 0; i < elements; ++i) {
                sample->bytes[static_cast<size_t>(i)] = (rng_() & 1) != 0 ? 1 : 0;
            }
            break;
        }
        default:
            throw std::invalid_argument(
                "a tensor parameter has an element type the check cannot make");
        }
        Sample result;
        result.code   = shape.dtype.value_or(TypeCode::Float32);
        result.tensor = std::move(sample);
        return result;
    }

    int64_t pickInt(bool constant) {
        static const int64_t edges[] = {
            0,
            1,
            -1,
            2,
            3,
            7,
            -5,
            100,
            255,
            1000,
            -1000,
            std::numeric_limits<int32_t>::max(),
            std::numeric_limits<int32_t>::min()};
        const uint64_t roll = rng_() % 8;
        if (roll < 4) {
            return edges[rng_() % (sizeof(edges) / sizeof(edges[0]))];
        }
        const int64_t range = constant ? 64 : 100000;
        return static_cast<int64_t>(rng_() % (2 * range + 1)) - range;
    }

    double pickFloat(bool constant, TypeCode code) {
        const bool noNans  = (needs_ & camel::core::sem::kNoNans) != 0;
        const bool noInfs  = (needs_ & camel::core::sem::kNoInfs) != 0;
        const bool noZeros = (needs_ & camel::core::sem::kNoSignedZeros) != 0;
        std::vector<double> specials{0.0, 1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 3.0, -3.0, 4.0, 0.1};
        // A special has to be a value of the parameter's own type: 1e300 is finite as a float64
        // and an infinity as a float32, which would smuggle an infinity past `no_infs` and make
        // every float32 rule that assumes finiteness fail its own check. The two reduced-precision
        // types need the same care, one step further: bf16's range is f32's (8 exponent bits) but
        // its largest finite value is 3.3895314e38, so 3.4e38 already rounds to an infinity, and
        // f16's range is 65504, where 1e30 and 1e-300 are an infinity and a zero. Each type's list
        // below is a list of values *of that type*.
        // The accuracy rules are the ones that promise a smaller error over the whole interesting
        // domain, so their sampler includes the overflow, underflow, denormal and near-zero
        // regions. The other rules keep the narrower set: a law like
        // `(x * c) + (y * c) => (x + y) * c` is exact on finite values but not at an overflow, and
        // it does not claim to be.
        const bool accuracy = (needs_ & camel::core::sem::kRefineAccuracy) != 0;
        if (code == TypeCode::Float32 || code == TypeCode::BFloat16) {
            // bf16 has f32's exponent range and one byte of f32's significand: the same regions,
            // with the largest finite value of bf16 instead of f32's for the near-overflow probe.
            specials.push_back(1e-30);
            specials.push_back(1e30);
            specials.push_back(1e-40); // subnormal
            if (accuracy) {
                specials.push_back(code == TypeCode::BFloat16 ? 1e-41 : 1e-45); // least subnormal
                specials.push_back(code == TypeCode::BFloat16 ? 3.3e38 : 3.4e38); // near overflow
                specials.push_back(100.0);  // exp overflows f32 here (e^100 = 2.7e43)
                specials.push_back(-100.0);
                specials.push_back(1000.0);
                specials.push_back(-1000.0);
                specials.push_back(1e-8); // the log(1 + x) near-zero region
                specials.push_back(-1e-8);
                specials.push_back(1e-16);
                specials.push_back(-1e-16);
            }
        } else if (code == TypeCode::Float16) {
            // The whole region of binary16: 65504 is its largest finite value, 6.1035156e-05 its
            // least normal and 5.9604645e-08 its least subnormal.
            specials.push_back(6.1e-5); // the least normal
            specials.push_back(1.0e-6);
            specials.push_back(1.0e-7); // subnormal
            specials.push_back(1.0e-4);
            specials.push_back(1.0e4);
            if (accuracy) {
                specials.push_back(6.0e-8);  // the least subnormal
                specials.push_back(65000.0); // near overflow
                specials.push_back(100.0);
                specials.push_back(-100.0);
                specials.push_back(1000.0);
                specials.push_back(-1000.0);
                specials.push_back(1e-3); // the log(1 + x) near-zero region of this type
                specials.push_back(-1e-3);
            }
        } else {
            specials.push_back(1e-300);
            specials.push_back(1e300);
            specials.push_back(1e-310); // subnormal
            if (accuracy) {
                specials.push_back(1e-320); // smallest subnormal
                specials.push_back(1.7e308); // near overflow
                specials.push_back(1000.0);
                specials.push_back(-1000.0);
                specials.push_back(1e-16); // the log(1 + x) near-zero region
                specials.push_back(-1e-16);
                specials.push_back(1e-300);
                specials.push_back(-1e-300);
            }
        }
        if (!noZeros) {
            specials.push_back(-0.0);
        }
        if (!noInfs && !constant) {
            specials.push_back(std::numeric_limits<double>::infinity());
            specials.push_back(-std::numeric_limits<double>::infinity());
        }
        if (!noNans && !constant) {
            specials.push_back(std::numeric_limits<double>::quiet_NaN());
        }
        const uint64_t roll = rng_() % 8;
        if (roll < 3) {
            return specials[rng_() % specials.size()];
        }
        if (constant) {
            // A coefficient a law reads is normally a value its element type holds exactly, and
            // the guards of the coefficient laws are about those values. Small integers are exact
            // in every element type; the inexact specials above still reach the guards.
            return static_cast<double>(static_cast<int64_t>(rng_() % 129) - 64);
        }
        const double range = 1000.0;
        return (static_cast<double>(rng_() % 2000001) / 1000000.0 - 1.0) * range;
    }

    std::mt19937_64 rng_;
    bool usedFarApartProbe_ = false;
    camel::core::sem::PermissionMask needs_;
};

/// Whether two float results agree: NaN agrees with NaN, and otherwise bit for bit when the rule
/// needs no permission, or within the numeric contract below when it does. `element` is the
/// element type the two values are of (`f32`, `f64`, `bf16` or `f16`): it decides both the bit
/// pattern an exact comparison compares and the epsilon a bounded one is measured in, so a
/// reduced-precision instance is never judged with f32's tolerance.
bool agreeFloat(double x, double y, TypeCode element,
    const camel::core::sem::RewriteErrorBound &bound, double scale) {
    if (std::isnan(x) || std::isnan(y)) {
        return std::isnan(x) && std::isnan(y);
    }
    if (!bound.checkable) {
        return true; // no contract to compare against (reported as unchecked)
    }
    if (bound.ulps == 0.0 && bound.relative == 0.0) {
        // A bf16 or an f16 widens to its f32 exactly (camel/core/rtdata/half.h), so comparing the
        // widened f32 bits compares the two-byte patterns: the two paths are one comparison.
        return element == TypeCode::Float64
            ? std::bit_cast<uint64_t>(x) == std::bit_cast<uint64_t>(y)
            : std::bit_cast<uint32_t>(static_cast<float>(x)) ==
                      std::bit_cast<uint32_t>(static_cast<float>(y)) &&
                  std::signbit(x) == std::signbit(y);
    }
    if (std::isinf(x) || std::isinf(y)) {
        return x == y;
    }
    const double eps       = checkEpsilon(element);
    const double magnitude = std::abs(x) + std::abs(y) + scale;
    // The smallest subnormal: a rounding in the subnormal range is absolute, not relative.
    const double smallest  = smallestSubnormalOf(element);
    const double allowed   = (bound.ulps * eps + bound.relative) * magnitude + bound.ulps * smallest;
    return std::abs(x - y) <= allowed;
}

/// One element of a tensor sample, as a double (the bytes of a sample are not aligned for the
/// element type).
double tensorElement(const TensorSample &t, size_t at) {
    return elementOfBytes(t.dtype, t.bytes.data() + at * elementBytesOf(t.dtype));
}

/// Whether two tensor results agree elementwise (the scalar rule above, per element): a NaN agrees
/// with a NaN, an exact rule compares bits (a signed zero included), a permission-bound rule uses
/// its bound per element. Every floating element type is compared elementwise, the reduced
/// precision ones included, so an instance at `bf16` is measured with bf16's epsilon and not with
/// a byte comparison (which would fail a legal rounding) nor with f32's (which would call an
/// illegal one equal).
bool agreeTensor(const TensorSample &a, const TensorSample &b,
    const camel::core::sem::RewriteErrorBound &bound, double scale) {
    if (a.dtype != b.dtype || a.shape != b.shape || a.bytes.size() != b.bytes.size()) {
        return false;
    }
    if (!isFloatingScalar(a.dtype)) {
        return a.bytes == b.bytes;
    }
    const size_t elementBytes = elementBytesOf(a.dtype);
    const size_t count = elementBytes == 0 ? 0 : a.bytes.size() / elementBytes;
    for (size_t i = 0; i < count; ++i) {
        if (!agreeFloat(tensorElement(a, i), tensorElement(b, i), a.dtype, bound, scale)) {
            return false;
        }
    }
    return true;
}

/// Whether two results agree: bit for bit when the rule needs no permission (a NaN is a NaN),
/// otherwise within the numeric contract of the rule's permissions and the operators' declared
/// bounds (camel/core/operator_semantics.h, rewriteErrorBound).
bool agree(
    const Sample &a, const Sample &b, const camel::core::sem::RewriteErrorBound &bound,
    double scale) {
    if (a.tensor != nullptr || b.tensor != nullptr) {
        return a.tensor != nullptr && b.tensor != nullptr && agreeTensor(*a.tensor, *b.tensor, bound, scale);
    }
    if (isFloatingScalar(a.code)) {
        return agreeFloat(a.asDouble, b.asDouble, a.code, bound, scale);
    }
    if (a.code == TypeCode::Int32) {
        return fromSlot<Int32>(a.slot) == fromSlot<Int32>(b.slot);
    }
    return a.slot == b.slot;
}

// ---- the proposition of a `reassociate` / `contract` rule ------------------------------------
//
// The ruling of 2026-10-24 on `verification-report-surface-s0` corrects what these rules are
// checked against. `reassociate` licenses a different rounding, and a different rounding may differ
// from the written one by far more than one rounding step when the expression cancels -- the right
// side is then the *more* accurate one -- so "the two sides differ by less than a rounding bound"
// is not the proposition. The proposition is the two halves the ruling gives it: real equality on
// the finite values, and IEEE equality, bit for bit, on the values the real model does not have
// (NaN, the infinities, a signed zero) -- unless the rule asked for `no_nans` / `no_infs` /
// `no_signed_zeros`, which narrow its domain.
//
// The finite half is compared where the real value is: both sides are evaluated in the reference's
// precision and the comparison is made there, never one element-type result against another. The
// contract that remains is the rule's own -- a rule whose syntax rounds (`x * (c1 + c2)` folds
// `c1 + c2` in the element type, one rounding the permission covers) may differ by that rounding --
// but the cancellation that inflates two element-type results is not part of the comparison any
// more, which is the whole of the correction: at `x = 1e+30` the library's `tensor_f32_combine_mixed`
// has two element-type results 27 ulps apart and one real value.
//
// The method needs the reference, so it applies to the element types the reference reads; a rule
// whose fragment the reference cannot evaluate falls back to the element type's own comparison,
// which is what it got before this change (a `reassociate` rule outside the fragment is not newly
// refuted for the check's own limitation).

/// Whether the element type is one the higher-precision reference can evaluate: the values
/// `tensorOfSample` reads and the operators of the reference's fragment are declared for.
bool realComparable(TypeCode code) {
    return code == TypeCode::Float32 || code == TypeCode::Float64;
}

/// Whether two real values are the same number under the rule's numeric contract: the comparison the
/// element type's own results used to get (`agreeFloat`), applied to the values the reference
/// computed. The contract stays the rule's -- a rule whose own syntax rounds (`c1 + c2` folded in the
/// element type is one rounding the permission covers) may differ by that rounding -- but the
/// cancellation that inflates two element-type results is no longer in the comparison at all.
bool refScalarsAgree(long double x, long double y, bool single,
    const camel::core::sem::RewriteErrorBound &bound, double scale) {
    if (std::isnan(x) || std::isnan(y)) {
        return std::isnan(x) && std::isnan(y);
    }
    if (!bound.checkable) {
        return true; // no contract to compare against (reported as unchecked)
    }
    if (bound.ulps == 0.0 && bound.relative == 0.0) {
        return x == y;
    }
    if (std::isinf(x) || std::isinf(y)) {
        return x == y;
    }
    const long double eps       = single ? 1.2e-7L : 2.3e-16L;
    const long double magnitude = std::fabs(x) + std::fabs(y) + scale;
    // The smallest subnormal: a rounding in the subnormal range is absolute, not relative.
    const long double smallest  = single ? 1.4e-45L : 4.9e-324L;
    const long double allowed =
        (bound.ulps * eps + bound.relative) * magnitude + bound.ulps * smallest;
    return std::fabs(x - y) <= allowed;
}

/// Whether two reference values are the same number, elementwise for tensors.
bool refValuesAgree(const RefValue &a, const RefValue &b, bool single,
    const camel::core::sem::RewriteErrorBound &bound, double scale) {
    if (a.tensor != b.tensor) {
        return false;
    }
    if (!a.tensor) {
        return refScalarsAgree(a.scalar, b.scalar, single, bound, scale);
    }
    if (a.t.shape != b.t.shape || a.t.values.size() != b.t.values.size()) {
        return false;
    }
    for (size_t i = 0; i < a.t.values.size(); ++i) {
        if (!refScalarsAgree(a.t.values[i], b.t.values[i], single, bound, scale)) {
            return false;
        }
    }
    return true;
}

/// Whether the two natives are both zeros of different sign: a special value the element type's own
/// bits decide and the real model does not have. A zero against a nonzero is not this case -- an
/// underflowed zero and a subnormal are both roundings of one real value, and the licence covers the
/// path that lost the value.
bool zeroSignsDiffer(const Sample &a, const Sample &b) {
    if (a.tensor != nullptr || b.tensor != nullptr) {
        if (a.tensor == nullptr || b.tensor == nullptr) {
            return false;
        }
        if (a.tensor->dtype != TypeCode::Float32 || b.tensor->dtype != TypeCode::Float32 ||
            a.tensor->shape != b.tensor->shape ||
            a.tensor->bytes.size() != b.tensor->bytes.size()) {
            return false;
        }
        const size_t count = a.tensor->bytes.size() / sizeof(float);
        for (size_t i = 0; i < count; ++i) {
            const double x = tensorElement(*a.tensor, i);
            const double y = tensorElement(*b.tensor, i);
            if (x == 0.0 && y == 0.0 && std::signbit(x) != std::signbit(y)) {
                return true;
            }
        }
        return false;
    }
    if (a.code != TypeCode::Float32 && a.code != TypeCode::Float64) {
        return false;
    }
    return a.asDouble == 0.0 && b.asDouble == 0.0 &&
           std::signbit(a.asDouble) != std::signbit(b.asDouble);
}

/// What the check found when it compared two values under the proposition of a `reassociate` /
/// `contract` rule.
struct RealComparison {
    /// Whether the two values agree under that proposition (meaningless when `compared` is false).
    bool agree = true;
    /// True when the higher-precision reference decided the comparison. False when the ruling's
    /// method does not apply to this rule -- its proposition is not real equality, or its element
    /// type is not one the reference reads -- or when the reference cannot evaluate a side; the
    /// caller then compares the two element-type values as it did before.
    bool compared = false;
    /// Which half of the proposition failed, for the row's line.
    std::string why;
};

/// The comparison the ruling gives a rule of this proposition: real equality on the finite values,
/// bit for bit on a special value. `leftGraph` and `rightGraph` are the two sides over the same
/// arguments (`values`), and `leftValue` / `rightValue` the two element-type results the VM made of
/// them -- the special half is read off those, the finite half off the reference.
RealComparison propositionComparison(const std::string &proposition,
    const camel::runtime::GCGraph &leftGraph, const camel::runtime::GCGraph &rightGraph,
    const std::vector<Sample> &values, const Sample &leftValue, const Sample &rightValue,
    camel::core::sem::PermissionMask needs, const camel::core::sem::RewriteErrorBound &bound,
    double scale) {
    RealComparison result;
    if (proposition != kPropositionRealEquality || !realComparable(elementCodeOf(leftValue)) ||
        leftGraph.normPorts().size() != rightGraph.normPorts().size()) {
        return result;
    }
    RefValue referenceLeft, referenceRight;
    try {
        referenceLeft  = realValueOf(leftGraph, values);
        referenceRight = realValueOf(rightGraph, values);
    } catch (const std::exception &) {
        return result; // the reference cannot read this fragment: the element type's own comparison
    }
    result.compared = true;
    // A side whose native value is a NaN or an infinity where the real model has a finite value is
    // not a rounding of that value: there the proposition is the IEEE half, and `agree` with no
    // numeric contract is exactly that comparison (a NaN agrees with a NaN, the bits otherwise).
    const bool special = std::isinf(refSampleError(leftValue, referenceLeft)) ||
                         std::isinf(refSampleError(rightValue, referenceRight));
    if (special) {
        result.agree = agree(leftValue, rightValue, {}, 1.0);
        result.why   = "a special value is not the same bit for bit";
        return result;
    }
    if ((needs & camel::core::sem::kNoSignedZeros) == 0 && zeroSignsDiffer(leftValue, rightValue)) {
        result.agree = false;
        result.why   = "a zero of the other sign is a different bit pattern";
        return result;
    }
    result.agree = refValuesAgree(
        referenceLeft, referenceRight, elementCodeOf(leftValue) == TypeCode::Float32, bound, scale);
    result.why = "the real values differ by more than the permission's rounding";
    return result;
}

std::string sampleText(const Sample &s) {
    if (s.tensor != nullptr) {
        const TensorSample &t = *s.tensor;
        std::string text       = std::format("tensor<{}>", typeCodeToString(t.dtype));
        text += " [";
        for (size_t i = 0; i < t.shape.size(); ++i) {
            text += (i == 0 ? "" : ", ") + std::to_string(t.shape[i]);
        }
        text += "] {";
        const size_t count =
            t.bytes.size() / elementBytesOf(t.dtype);
        const size_t shown = std::min<size_t>(count, 8);
        for (size_t i = 0; i < shown; ++i) {
            if (i != 0) {
                text += ", ";
            }
            switch (t.dtype) {
            case TypeCode::Float32:
                text += std::format("{}", static_cast<float>(tensorElement(t, i)));
                break;
            case TypeCode::Float64:
                text += std::format("{}", tensorElement(t, i));
                break;
            case TypeCode::Int64:
                text += std::to_string(static_cast<int64_t>(tensorElement(t, i)));
                break;
            case TypeCode::BFloat16:
            case TypeCode::Float16:
                // The text of the element is the element type's own (design principles 7.7 and
                // the ruling of 2026-10-23 on `reduced-precision-text`): the shortest digits that
                // read back as the same bf16 or f16, not the f32 text of the value it widens to.
                text += halfTextOf(t, i);
                break;
            default:
                text += t.bytes[i] != 0 ? "true" : "false";
                break;
            }
        }
        if (count > shown) {
            text += ", ...";
        }
        return text + "}";
    }
    switch (s.code) {
    case TypeCode::Int64:
        return std::to_string(fromSlot<Int64>(s.slot));
    case TypeCode::Int32:
        return std::to_string(fromSlot<Int32>(s.slot));
    case TypeCode::Bool:
        return fromSlot<Bool>(s.slot) ? "true" : "false";
    case TypeCode::BFloat16:
        return halfText(BFloat16::fromBits(halfBitsOfSlot(s.slot)));
    case TypeCode::Float16:
        return halfText(Float16::fromBits(halfBitsOfSlot(s.slot)));
    default:
        return std::format("{}", s.asDouble);
    }
}

Sample resultOf(slot_t slot, const Type *type) {
    if (camel::runtime::fact::shapeOfType(type)) {
        const auto *array = fromSlot<Array *>(slot);
        auto tensor       = std::make_shared<TensorSample>();
        tensor->dtype     = array->dtype();
        tensor->shape.assign(array->shape(), array->shape() + array->rank());
        if (array->byteSize() != 0) {
            const auto *bytes = array->dataAs<uint8_t>();
            tensor->bytes.assign(bytes, bytes + array->byteSize());
        }
        Sample sample;
        sample.code   = type->code();
        sample.slot   = slot;
        sample.tensor = std::move(tensor);
        return sample;
    }
    switch (type->code()) {
    case TypeCode::Int64:
        return {TypeCode::Int64, slot, static_cast<double>(fromSlot<Int64>(slot)), nullptr};
    case TypeCode::Int32:
        return {TypeCode::Int32, slot, static_cast<double>(fromSlot<Int32>(slot)), nullptr};
    case TypeCode::Float32:
        return {TypeCode::Float32, slot, static_cast<double>(fromSlot<Float32>(slot)), nullptr};
    case TypeCode::Float64:
        return {TypeCode::Float64, slot, fromSlot<Float64>(slot), nullptr};
    case TypeCode::BFloat16:
        // The slot holds the two bytes of the format; the value the check compares is the f32 it
        // widens to, exactly (camel/core/rtdata/half.h).
        return {
            TypeCode::BFloat16,
            slot,
            static_cast<double>(halfToFloat<BFloat16>(slot)),
            nullptr};
    case TypeCode::Float16:
        return {
            TypeCode::Float16, slot, static_cast<double>(halfToFloat<Float16>(slot)), nullptr};
    default:
        return {type->code(), slot, fromSlot<Bool>(slot) ? 1.0 : 0.0, nullptr};
    }
}

/// Counts the operators of a function graph (the steps that round).
size_t stepsOf(const camel::runtime::GCGraph &graph) {
    size_t steps = 0;
    for (camel::runtime::gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        steps += graph.node(ref)->kind == camel::runtime::GCNodeKind::Oper ? 1 : 0;
    }
    return steps;
}

/// The ulps the operators of one side declare between them (operator_semantics.h, stepUlps).
double declaredUlpsOf(const camel::runtime::GCGraph &graph) {
    const auto &table = camel::core::OperatorTable::instance();
    double ulps       = 0.0;
    for (camel::runtime::gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        const camel::runtime::GCNode *node = graph.node(ref);
        if (node->kind != camel::runtime::GCNodeKind::Oper || !node->hasPayload()) {
            continue;
        }
        const camel::core::sem::OperatorSemantics *declared =
            table.semantics(graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)->oper);
        ulps += camel::core::sem::stepUlps(
            declared != nullptr ? declared->numeric : camel::core::sem::NumericSemantics{});
    }
    return ulps;
}

/// The index of `ref` among the graph's form ports, the function's parameters.
std::optional<size_t> portIndexIn(
    const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const auto ports = graph.normPorts();
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i] == ref) {
            return i;
        }
    }
    return std::nullopt;
}

/// Whether the operator takes an axis operand at index 1 (2 for the gradient form). The check
/// reads the operator's own declaration, not the rule's parameter names, so it knows which
/// integer operand is an axis and can give the tensor a rank the axis fits.
bool takesAxisAt(std::string_view uri, size_t &index) {
    if (uri == "tensor:sum_axis" || uri == "tensor:mean_axis" || uri == "tensor:max_axis" ||
        uri == "tensor:min_axis" || uri == "tensor:argmax_axis" ||
        uri == "tensor:argmin_axis" || uri == "tensor:softmax" || uri == "tensor:log_softmax") {
        index = 1;
        return true;
    }
    if (uri == "tensor:softmax_grad") {
        index = 2;
        return true;
    }
    return false;
}

/// Marks the parameters the rule's own operators use as an axis. An axis parameter is sampled
/// from the valid range of its tensor's rank, and the tensor's rank is `axis + 1` at least.
std::vector<uint8_t> axisPortsOf(const camel::runtime::GCGraph &graph) {
    std::vector<uint8_t> marks(graph.normPorts().size(), 0);
    for (camel::runtime::gc_node_ref_t ref = 0; ref < graph.nodeCount(); ++ref) {
        const camel::runtime::GCNode *node = graph.node(ref);
        if (node->kind != camel::runtime::GCNodeKind::Oper || !node->hasPayload()) {
            continue;
        }
        const std::string_view uri =
            graph.nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri();
        size_t index = 0;
        if (!takesAxisAt(uri, index)) {
            continue;
        }
        const auto inputs = graph.normInputsOf(ref);
        if (index >= inputs.size()) {
            continue;
        }
        if (const auto port = portIndexIn(graph, inputs[index])) {
            marks[*port] = 1;
        }
    }
    return marks;
}

/// The smallest rank a tensor parameter carries in its type, if the type states one.
std::optional<size_t> knownRankOf(const Type *type) {
    const auto shape = camel::runtime::fact::shapeOfType(type);
    if (!shape || !shape->dims) {
        return std::nullopt;
    }
    return shape->dims->size();
}

/// The `const` parameters of a rule as the bit mask a rule object keeps them in: a rule source
/// carries one byte per parameter.
uint64_t constMaskOf(const camel::runtime::RuleSource &source) {
    uint64_t mask = 0;
    for (size_t i = 0; i < source.constParams.size() && i < 64; ++i) {
        if (source.constParams[i] != 0) {
            mask |= (uint64_t{1} << i);
        }
    }
    return mask;
}

/// One rule of the check's list, whatever it came from: an ordinary rule, or one instance of a
/// generic law (`add_zero<i32>`), whose two sides, condition and permissions are the ones the
/// instance's graphs carry. `samples` random instances of its two sides (random constants for the
/// const parameters, random values for the others) must agree, and the engine must fire the rule on
/// its own left side and agree too. A rule that asks for `refine_accuracy` is compared by its error
/// against a higher-precision reference instead, sample by sample, and the worst error ratio is
/// reported. `stream` picks the samples; the caller derives it from the rule's identity, not from
/// its place in the array (the ruling of 2026-10-24 on `accuracy-check-sampling`, 1).
///
/// The rows this appends are the verdict table `check` answers (H247, slice S0): the verdict, what
/// it is about, and the evidence or the reason it is what it is. With no solver in the build a rule
/// whose two sides agree on every sample is `sampled`, never `proved`: the check compared samples,
/// and `proved` is reserved for a licence's proposition being established (the ruling of
/// 2026-10-24 on `verification-licence-to-precondition`).
void checkOneRule(
    Context &ctx, const camel::runtime::RuleSource &source, int64_t samples, uint64_t stream,
    std::string_view instance, std::vector<CheckRow> &rows) {
    // The table has a column for the rule and a column for the instance, so the rule's own name is
    // the law's name without the type tuple the instance adds: the engine names an instance
    // `int_add_zero<int>`, and the row says `int_add_zero` and `int` (the rendering puts them back
    // together, so the name a reader sees is the one the engine, `explain` and the old report use).
    std::string ruleName = source.name;
    const std::string instanceSuffix = std::format("<{}>", instance);
    if (!instance.empty() && ruleName.ends_with(instanceSuffix)) {
        ruleName.resize(ruleName.size() - instanceSuffix.size());
    }
    const std::string proposition              = propositionOf(source.needs);
    const bool conditional                       = source.when != nullptr;
    const camel::core::sem::PermissionMask needs = source.needs;
    // What the translation layer made of this rule's two sides (H247, slice S1). The sides are read
    // once, here: the same two sides are the rule's own graphs, whatever the instance. A rule the
    // layer cannot translate is `sampled` with the reason written in the coverage column (the ruling
    // of 2026-10-24 on `verification-verdict-surface`, 3).
    const std::string coverage = camel::verify::translate(source).column();
    const auto addRow = [&](std::string_view verdict, std::string evidence, std::string reason,
                            std::string detail) {
        CheckRow row;
        row.rule        = ruleName;
        row.instance    = std::string(instance);
        row.verdict     = std::string(verdict);
        row.proposition = proposition;
        row.evidence    = std::move(evidence);
        row.reason      = std::move(reason);
        row.detail      = std::move(detail);
        row.coverage    = coverage;
        rows.push_back(std::move(row));
    };
    const uint64_t constParams                   = constMaskOf(source);
    const camel::runtime::Program *program       = source.program;
    const camel::runtime::GraphId lhsId          = source.lhs;
    const camel::runtime::GraphId rhsId          = source.rhs;
    const camel::runtime::GCGraph *lhsGraph = program->graph(lhsId);
    const auto *lhsType                     = lhsGraph->funcType();
    const size_t params                     = lhsType->normTypesCount();
    const Type *resultType                  = lhsType->exitType();
    const size_t steps = stepsOf(*lhsGraph) + stepsOf(*program->graph(rhsId));
    const double declaredUlps =
        declaredUlpsOf(*lhsGraph) + declaredUlpsOf(*program->graph(rhsId));
    // The bound is the numeric contract of the permissions the rule needs, read together
    // with the operators' declarations (not one uniform rounding bound).
    const camel::core::sem::RewriteErrorBound bound =
        camel::core::sem::rewriteErrorBound(needs, steps, declaredUlps);
    const bool accuracy = (needs & camel::core::sem::kRefineAccuracy) != 0;
    // The rule's own operators say which integer parameters are axes. The rank of an
    // otherwise unshaped tensor follows the axis it is reduced over, and rotates through 1,
    // 2 and 3 so that a vector, a matrix and a rank-3 tensor are all sampled.
    std::vector<uint8_t> axisPorts = axisPortsOf(*lhsGraph);
    const std::vector<uint8_t> rhsAxisPorts = axisPortsOf(*program->graph(rhsId));
    for (size_t i = 0; i < axisPorts.size() && i < rhsAxisPorts.size(); ++i) {
        axisPorts[i] = static_cast<uint8_t>(axisPorts[i] | rhsAxisPorts[i]);
    }
    std::optional<size_t> knownRank;
    for (size_t i = 0; i < params; ++i) {
        if (const auto rank = knownRankOf(lhsType->normTypeAt(i))) {
            knownRank = knownRank ? std::min(*knownRank, *rank) : *rank;
        }
    }
    Sampler sampler(stream, needs);
    const auto drawValues = [&](int64_t sampleIndex) {
        std::vector<Sample> drawn(params);
        int64_t maxAxis = -1;
        for (size_t i = 0; i < params; ++i) {
            if (i >= axisPorts.size() || axisPorts[i] == 0) {
                continue;
            }
            // The axis is an operand, not a free constant: cycle through the axes the rule's
            // shapes need (rank 1, a matrix, a rank-3 reduction) and the last axis.
            static constexpr int64_t kAxisCycle[] = {0, 1, 2, -1};
            int64_t axis = kAxisCycle[sampleIndex % 4];
            if (knownRank && *knownRank > 0) {
                axis = std::min<int64_t>(axis, static_cast<int64_t>(*knownRank) - 1);
            }
            drawn[i] = sampler.fixedInt(lhsType->normTypeAt(i), axis);
            if (axis >= 0) {
                maxAxis = std::max(maxAxis, axis);
            }
        }
        const size_t rankHint = std::max<size_t>(
            static_cast<size_t>(1 + (sampleIndex % 3)),
            maxAxis >= 0 ? static_cast<size_t>(maxAxis + 1) : 0);
        for (size_t i = 0; i < params; ++i) {
            if (i < axisPorts.size() && axisPorts[i] != 0) {
                continue;
            }
            const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
            drawn[i] = sampler.value(lhsType->normTypeAt(i), isConst, rankHint);
        }
        return drawn;
    };
    NodeVMSchedPass vm(ctx.shared_from_this());
    // What went wrong, in the shape a row wants it: the `detail` line, the `evidence` the verdict
    // rests on ("counterexample" when a value is the witness, "none" when nothing could be
    // compared), and the `reason` the ruling has written apart from the verdict.
    std::string failure;
    std::string failureEvidence;
    std::string failureReason;
    double worstRatio = 0.0;
    std::set<std::vector<int64_t>> shapesSeen;
    std::set<int64_t> axesSeen;
    if (!bound.checkable) {
        // `nondeterministic` has no numeric contract: there is nothing to compare against, so
        // nothing was examined. The verdict stays `sampled` -- the three words are closed and the
        // fourth the check used to write is gone -- but the reason is written in its own column and
        // is never left out, so a reader cannot take this `sampled` for one that sampled (the
        // ruling of 2026-10-24 on `verification-licence-to-precondition`, licence 3, option A).
        addRow(kVerdictSampled, "none", "no numeric bound", "");
        return;
    }
    // A condition is part of the equation: the two sides need not agree on values the
    // condition rejects, and the tool cannot ask the condition about sampled values. Such a
    // rule is checked by the engine path below only.
    for (int64_t sample = 0; !conditional && sample < samples && failure.empty(); ++sample) {
        std::vector<Sample> values = drawValues(sample);
        std::vector<slot_t> args;
        double scale = 1.0;
        for (size_t i = 0; i < values.size(); ++i) {
            scale = std::max(scale, std::abs(values[i].asDouble) + 1.0);
            if (values[i].tensor != nullptr) {
                shapesSeen.insert(values[i].tensor->shape);
            }
            if (i < axisPorts.size() && axisPorts[i] != 0) {
                axesSeen.insert(static_cast<int64_t>(values[i].asDouble));
            }
        }
        // A tensor argument is an Array: it is made right before the call that reads it, so no
        // allocation can move it in between (and the call below is a safepoint).
        const auto fillArgs = [&] {
            args.resize(values.size());
            for (size_t i = 0; i < values.size(); ++i) {
                args[i] = sampler.materialize(values[i]);
            }
        };
        fillArgs();
        const Sample left =
            resultOf(vm.callGraph(program, program->graph(lhsId), args), resultType);
        fillArgs();
        const Sample right =
            resultOf(vm.callGraph(program, program->graph(rhsId), args), resultType);
        if (accuracy) {
            const AccuracyResult checked = compareAgainstReference(
                *lhsGraph,
                *program->graph(rhsId),
                values,
                left,
                right,
                elementCodeOf(left));
            worstRatio = std::max(worstRatio, checked.ratio);
            if (!checked.ok) {
                std::string at;
                for (size_t i = 0; i < values.size(); ++i) {
                    at += (i == 0 ? "" : ", ") + sampleText(values[i]);
                }
                if (checked.referenceAvailable) {
                    // The reference says the right side is worse on this sample: the error bound
                    // the permission asks for is false here, which is a counterexample, not a
                    // missing comparison.
                    failureEvidence = "counterexample";
                    failure         = std::format(
                        "the right side is worse at ({}): its error {:.6g} exceeds the left "
                        "side's {:.6g} (ratio {:.6g})",
                        at,
                        static_cast<double>(checked.rightError),
                        static_cast<double>(checked.leftError),
                        checked.ratio);
                } else {
                    // Nothing could be compared: the rule is outside what the check can examine,
                    // and the ruling has that said out loud rather than reported as a failure.
                    failureEvidence = "none";
                    failureReason   = checked.reason;
                    failure         = std::format("the check cannot examine this rule at ({})", at);
                }
            }
        } else {
            const double sampleScale =
                std::min(scale * scale, std::numeric_limits<double>::max());
            const RealComparison real = propositionComparison(
                proposition, *lhsGraph, *program->graph(rhsId), values, left, right, needs, bound,
                sampleScale);
            const bool holds = real.compared ? real.agree : agree(left, right, bound, sampleScale);
            if (!holds) {
                std::string at;
                for (size_t i = 0; i < values.size(); ++i) {
                    at += (i == 0 ? "" : ", ") + sampleText(values[i]);
                }
                failureEvidence = "counterexample";
                failure         = real.compared
                    ? std::format(
                          "the two sides differ at ({}): {} and {} ({})",
                          at,
                          sampleText(left),
                          sampleText(right),
                          real.why)
                    : std::format(
                          "the two sides differ at ({}): {} and {}",
                          at,
                          sampleText(left),
                          sampleText(right));
            }
        }
    }
    // The engine applies the rule to the left side, with random constants for the const
    // parameters: it must fire, and the function it gives must agree too.
    size_t fired = 0, tried = 0;
    if (failure.empty()) {
        auto compiled            = camel::runtime::compileRule(ctx, source);
        camel::runtime::Rule one = std::move(compiled.front());
        one.twoWay               = false;
        const camel::core::sem::NumericPolicy policy{needs, true};
        // A condition selects its instances: a rule whose condition accepts few constants
        // needs more draws before it can be seen firing.
        const int64_t instances = std::min<int64_t>(samples, conditional ? 64 : 8);
        for (int64_t instance = 0; instance < instances && failure.empty(); ++instance) {
            ++tried;
            std::vector<Sample> consts;
            // A session over the left side alone: its result is a program of its own, the
            // program the rule is in is left as it is.
            camel::runtime::RuntimeGraphDraftSession session(
                ctx.shared_from_this(),
                program,
                const_cast<camel::runtime::GCGraph *>(lhsGraph));
            camel::runtime::GraphDraft &draft =
                session.edit(const_cast<camel::runtime::GCGraph *>(lhsGraph));
            const std::vector<camel::runtime::gc_node_ref_t> ports(
                draft.normPorts().begin(),
                draft.normPorts().end());
            std::vector<slot_t> args;
            std::vector<Sample> values = drawValues(instance);
            double scale               = 1.0;
            for (size_t i = 0; i < params; ++i) {
                const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
                Type *type         = lhsType->normTypeAt(i);
                scale = std::max(scale, std::abs(values[i].asDouble) + 1.0);
                if (isConst) {
                    const auto constant = draft.materializeStaticValue(values[i].slot, type);
                    draft.replaceAllValueUses(ports[i], constant);
                }
            }
            camel::runtime::DraftWritings writings(draft, policy);
            const auto stats = writings.rewrite(std::span<const camel::runtime::Rule>(&one, 1));
            if (stats.applied == 0) {
                continue;
            }
            ++fired;
            const camel::runtime::Program *rewritten = session.commitChecked();
            const auto fillArgs = [&] {
                args.resize(values.size());
                for (size_t i = 0; i < values.size(); ++i) {
                    args[i] = sampler.materialize(values[i]);
                }
            };
            fillArgs();
            const Sample before =
                resultOf(vm.callGraph(program, program->graph(lhsId), args), resultType);
            fillArgs();
            const Sample after =
                resultOf(vm.callGraph(rewritten, rewritten->graph(lhsId), args), resultType);
            if (accuracy) {
                const AccuracyResult checked =
                    compareBeforeAfter(*lhsGraph, values, before, after, elementCodeOf(before));
                worstRatio = std::max(worstRatio, checked.ratio);
                if (!checked.ok) {
                    std::string at;
                    for (const Sample &value : values) {
                        at += (at.empty() ? "" : ", ") + sampleText(value);
                    }
                    if (checked.referenceAvailable) {
                        failureEvidence = "counterexample";
                        failure         = std::format(
                            "the rewritten left side is less accurate at ({}): its error {:.6g} "
                            "exceeds the original's {:.6g} (ratio {:.6g})",
                            at,
                            static_cast<double>(checked.rightError),
                            static_cast<double>(checked.leftError),
                            checked.ratio);
                    } else {
                        failureEvidence = "none";
                        failureReason   = checked.reason;
                        failure =
                            std::format("the check cannot examine the rewritten side at ({})", at);
                    }
                }
            } else {
                const double sampleScale =
                    std::min(scale * scale, std::numeric_limits<double>::max());
                const RealComparison real = propositionComparison(
                    proposition,
                    *lhsGraph,
                    *rewritten->graph(lhsId),
                    values,
                    before,
                    after,
                    needs,
                    bound,
                    sampleScale);
                const bool holds =
                    real.compared ? real.agree : agree(before, after, bound, sampleScale);
                if (!holds) {
                    std::string at;
                    for (const Sample &value : values) {
                        at += (at.empty() ? "" : ", ") + sampleText(value);
                    }
                    failureEvidence = "counterexample";
                    failure         = real.compared
                        ? std::format(
                              "the rewritten left side differs at ({}): {} and {} ({})",
                              at,
                              sampleText(before),
                              sampleText(after),
                              real.why)
                        : std::format(
                              "the rewritten left side differs at ({}): {} and {} "
                              "(bound {} ulps, {} relative, scale {})",
                              at,
                              sampleText(before),
                              sampleText(after),
                              bound.ulps,
                              bound.relative,
                              scale * scale);
                }
            }
        }
        if (failure.empty() && fired == 0) {
            // The engine never applied the rule to its own left side. That is not a counterexample
            // to the equation -- no values were compared -- so the evidence is `none` and the
            // reason says what happened; the verdict is still `refuted`, because a rule the engine
            // cannot fire on its own left side must not pass a check that guards the library.
            failureEvidence = "none";
            failureReason   = std::format(
                "the rule never fired on its own left side in {} tries", tried);
            failure = "the rule never fired on its own left side";
        }
    }
    if (!failure.empty()) {
        addRow(kVerdictRefuted, failureEvidence, failureReason, failure);
    } else if (accuracy) {
        std::string shapesText;
        for (const auto &shape : shapesSeen) {
            shapesText += shapesText.empty() ? "" : " ";
            shapesText += "[";
            for (size_t i = 0; i < shape.size(); ++i) {
                shapesText += (i == 0 ? "" : ", ") + std::to_string(shape[i]);
            }
            shapesText += "]";
        }
        std::string axesText;
        for (int64_t axis : axesSeen) {
            axesText += axesText.empty() ? "" : " ";
            axesText += std::to_string(axis);
        }
        const std::string ratio =
            std::isinf(worstRatio) ? "inf" : std::format("{:.3g}", worstRatio);
        std::string detail = std::format(
            "worst error ratio {}, fired {} of {}", ratio, fired, tried);
        if (!shapesText.empty()) {
            detail += ", shapes " + shapesText;
        }
        if (!axesText.empty()) {
            detail += ", axes " + axesText;
        }
        addRow(kVerdictSampled, std::format("samples {}", samples), "", detail);
    } else if (conditional) {
        // The two sides were never compared: a condition cannot be asked about sampled values, so
        // the engine path is the whole of what this rule got. It is `sampled`, and the detail says
        // which path was walked, so a reader does not take it for a value comparison.
        addRow(
            kVerdictSampled,
            std::format("samples {}", samples),
            "the condition selects the instances, so no sampled value was compared",
            std::format("the condition selects the instances, fired {} of {}", fired, tried));
    } else {
        addRow(
            kVerdictSampled,
            std::format("samples {}", samples),
            "",
            std::format("fired {} of {}", fired, tried));
    }
}

/// The instances of one generic rule, checked one by one (the ruling of 2026-10-24 on
/// `generic-rule-check-surface-and-multi-parameter-instances`).
///
/// The candidate tuples are the product of the types the operator groups registered
/// (`compile::gir::ruleInstanceTypes`) over the rule's type parameters, each tuple filtered by the
/// rule's condition at the type level: a tuple the condition rejects is not an instance of the law
/// and says nothing. A tuple the pattern cannot be written for is not an instance either, and is
/// quiet as well (the ruling of 2026-10-23 on `generic-rule-instantiation-set-and-predicate` §3);
/// `why_not` is where that reason is read. An instance that was made but whose values the check
/// cannot draw is `sampled` with that reason: that is the one place where the check covers less
/// than the law says, and the reason column is where the ruling has it written.
void checkGenericRule(
    Context &ctx, RuleObject &rule, int64_t samples, uint64_t seed,
    const std::vector<Type *> &programTypes, std::vector<CheckRow> &rows) {
    // The rule is a value of the program being checked, and the collector moves it: building an
    // instance below collects. The id and the name are copied here, while the reference this
    // function was given still names the object; every other read of the rule is made through the
    // handle, which the collector keeps up to date.
    const uint64_t ruleId = rule.ruleId();
    const std::string name(rule.name());
    auto root = std::make_shared<mm::RootHandle>(
        mm::autoSpace(), &rule, RuleType::get(), "rewrite.checkGenericRule");
    const auto live = [&]() { return root->getAs<RuleObject>(); };
    const auto whole = [&](const std::string &reason) {
        CheckRow row;
        row.rule        = name;
        row.verdict     = kVerdictSampled;
        row.proposition = propositionOf(live()->needs());
        row.evidence    = "none";
        row.reason      = reason;
        // No two sides were built, so there is nothing the translation layer could read.
        row.coverage = camel::verify::uncoveredColumn(
            "the check did not build the rule's two sides: " + reason);
        rows.push_back(std::move(row));
    };
    GenericRuleEntry *entry = camel::compile::gir::genericRuleEntry(ctx, ruleId);
    if (entry == nullptr) {
        whole("its templates are not in this context");
        return;
    }
    const size_t arity = entry->typeParams.size();
    if (arity == 0) {
        whole("its head declares no type parameter, so it has no instance to make");
        return;
    }
    // The types the operator groups registered, and then the types a program the call was given
    // has (the ruling of 2026-10-24 on `check-program-parameter`): a call that leaves the
    // parameter out reads its null and the candidate set is the registered types alone, which is
    // every reading the check had before the parameter existed.
    std::vector<Type *> universe = camel::compile::gir::ruleInstanceTypes();
    for (Type *type : programTypes) {
        const bool known = std::any_of(
            universe.begin(), universe.end(),
            [&](const Type *seen) { return seen == type || seen->equals(type); });
        if (!known) {
            universe.push_back(type);
        }
    }
    if (universe.empty()) {
        whole("no operator group registered a type to instantiate it at");
        return;
    }
    // Every tuple of the product the condition admits, in the order the types were registered (the
    // first type parameter varies fastest, as a counter does). The condition is asked before
    // anything is instantiated: a rejected tuple costs one compile-time question.
    size_t product = 1;
    for (size_t i = 0; i < arity && product <= kCheckTupleCap * kCheckTupleCap; ++i) {
        product *= universe.size();
    }
    std::vector<std::vector<Type *>> tuples;
    for (size_t n = 0; n < product; ++n) {
        std::vector<Type *> tuple(arity);
        size_t rest = n;
        for (size_t i = 0; i < arity; ++i) {
            tuple[i] = universe[rest % universe.size()];
            rest /= universe.size();
        }
        std::string why;
        if (camel::compile::gir::ruleConditionAt(ctx, ruleId, tuple, why) ==
            RuleConditionAt::Fails) {
            continue;
        }
        tuples.push_back(std::move(tuple));
    }
    const size_t total = tuples.size();
    if (total > kCheckTupleCap) {
        // Deterministic: the same seed takes the same tuples, whatever order the groups registered
        // the types in. The row says which ones were taken and how many there are in all; it is the
        // law's own row, since no single instance stands for it.
        std::mt19937_64 rng(sampleStreamOf(seed, name));
        std::vector<size_t> order(total);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), rng);
        order.resize(kCheckTupleCap);
        std::sort(order.begin(), order.end());
        std::vector<std::vector<Type *>> taken;
        taken.reserve(order.size());
        for (size_t at : order) {
            taken.push_back(tuples[at]);
        }
        tuples = std::move(taken);
        std::string takenText;
        for (const std::vector<Type *> &tuple : tuples) {
            takenText += takenText.empty() ? "(" : " (";
            for (size_t i = 0; i < tuple.size(); ++i) {
                takenText += (i == 0 ? "" : ", ") + tuple[i]->toString();
            }
            takenText += ")";
        }
        CheckRow row;
        row.rule        = name;
        row.verdict     = kVerdictSampled;
        row.proposition = propositionOf(live()->needs());
        row.evidence    = "none";
        row.reason = std::format(
            "more type tuples than the check walks, so {} of {} were taken", tuples.size(), total);
        row.detail = takenText;
        rows.push_back(std::move(row));
    }
    for (const std::vector<Type *> &tuple : tuples) {
        const size_t place = camel::compile::gir::genericRuleInstance(ctx, ruleId, tuple);
        // Making an instance builds a body and encodes graphs: the entry is read again, and the
        // rule object again from its root, after it.
        GenericRuleEntry *current = camel::compile::gir::genericRuleEntry(ctx, ruleId);
        if (current == nullptr || place >= current->instances.size() ||
            !current->instances[place].made) {
            continue;
        }
        const GenericRuleInstance &instance = current->instances[place];
        const std::string instanceName = std::format("{}<{}>", name, instance.typeText);
        const auto *lhsType = instance.lhs->funcType();
        // The seed follows the instance's identity, not its place in the array the caller passed
        // (the ruling of 2026-10-24 on `accuracy-check-sampling`, 1).
        const uint64_t stream = sampleStreamOf(seed, instanceName);
        Sampler probe(stream, live()->needs());
        std::string cannot;
        for (size_t i = 0; i < lhsType->normTypesCount(); ++i) {
            const Type *parameter = lhsType->normTypeAt(i);
            if (!probe.canDraw(parameter)) {
                cannot = parameter->toString();
                break;
            }
        }
        // The rule source is built before the sampler is asked anything: the translation layer reads
        // the same two sides, and its answer belongs on the row even when no value could be made.
        std::shared_ptr<WhenCall> call;
        if (instance.when != nullptr) {
            call              = std::make_shared<WhenCall>();
            call->vm          = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
            call->program     = instance.program;
            call->graph       = instance.when;
            call->own         = instance.lhs->normPorts().size();
            call->arity       = instance.whenArity;
            call->constParams = live()->constParams();
            call->name        = instanceName;
        }
        const RuleObject &currentRule = *live();
        const camel::runtime::RuleSource source = instanceOf(currentRule, 1, instance, call);
        if (!cannot.empty()) {
            CheckRow row;
            row.rule        = name;
            row.instance    = instance.typeText;
            row.verdict     = kVerdictSampled;
            row.proposition = propositionOf(rule.needs());
            row.evidence    = "none";
            row.reason      = std::format("the check cannot make a value of type '{}'", cannot);
            row.coverage    = camel::verify::translate(source).column();
            rows.push_back(std::move(row));
            continue;
        }
        checkOneRule(ctx, source, samples, stream, instance.typeText, rows);
    }
}

/// `check(rules, samples, seed)`: for every rule, `samples` random instances (random constants
/// for the const parameters, random values for the others) of its two sides, which must agree.
/// A rule that asks for `refine_accuracy` is compared by its error against a higher-precision
/// reference instead, sample by sample, and the worst error ratio is reported.
/// A generic rule is checked once per instance its condition admits over the types the operator
/// groups registered (the ruling of 2026-10-24 on
/// `generic-rule-check-surface-and-multi-parameter-instances`).
///
/// It answers a `CheckReport` (H247, slice S0): the verdict table, one row per rule or per instance
/// of a generic law, each row carrying the rule, the instance, the verdict, what the verdict is
/// about and the evidence or the reason (the ruling of 2026-10-24 on
/// `verification-verdict-surface`, 2: the verdict is published by `check` and the text output is
/// the fixed rendering of that table, which is what a reader -- and the acceptance -- reads).
slot_t checkKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const int64_t samples = std::max<int64_t>(1, norm.get<Int64>(1));
    const uint64_t seed   = static_cast<uint64_t>(norm.get<Int64>(2));
    // The program the call was given, or none: the parameter is a `Program?` whose default is
    // `null` (design principles 4.4; the ruling of 2026-10-24 on `check-program-parameter`), so a
    // call that leaves it out reads the null constant of the type and has no types of its own to
    // add. Its types join the candidate set of every generic rule below.
    std::vector<Type *> programTypes;
    if (norm.size() > 3) {
        // A `Program?` is the null slot or a tuple holding the program (rtdata/tagged.h), so the
        // payload is what names the program: reading the slot as a pointer would read the tuple.
        const slot_t given = norm.slot(3);
        if (given != NullSlot) {
            if (const camel::runtime::Program *program =
                    fromSlot<const camel::runtime::Program *>(nullablePayload(given));
                program != nullptr) {
                programTypes = programValueTypes(*program);
            }
        }
    }
    std::vector<CheckRow> rows;
    const size_t count = norm.get<Array *>(0)->size();
    for (size_t index = 0; index < count; ++index) {
        // The array moves when a function runs: it is read again each time.
        RuleObject *made = norm.get<Array *>(0)->get<RuleObject *>(index);
        if (made->ruleId() != 0) {
            // A generic rule has no two sides of its own: it is checked once per instance its
            // condition admits over the types the operator groups registered (the ruling of
            // 2026-10-24 on `generic-rule-check-surface-and-multi-parameter-instances`). The index
            // is not passed: the seed of a rule follows its identity, not its place in this array.
            checkGenericRule(ctx, *made, samples, seed, programTypes, rows);
            continue;
        }
        // A condition is read again from the rule object after every call, so the object is
        // rooted while the check runs.
        std::shared_ptr<mm::RootHandle> ruleRoot;
        std::shared_ptr<WhenCall> call;
        if (made->when() != nullptr) {
            ruleRoot = std::make_shared<mm::RootHandle>(
                mm::autoSpace(),
                made,
                RuleType::get(),
                "rewrite.checkRule");
            call       = std::make_shared<WhenCall>();
            call->rule = ruleRoot;
            call->vm   = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
        }
        const RuleObject *rule = ruleRoot ? ruleRoot->getAs<RuleObject>() : made;
        // The rule object is read now and not again: a function runs below, and objects move.
        const camel::runtime::RuleSource source = sourceOf(*rule, 1, call);
        // The seed follows the rule's own name (the ruling of 2026-10-24 on
        // `accuracy-check-sampling`, 1).
        checkOneRule(
            ctx, source, samples, sampleStreamOf(seed, std::string_view(source.name)), "", rows);
    }
    return toSlot(CheckReportObject::create(std::move(rows), mm::autoSpace()));
}

/// The report's fixed rendering, as a string. `print(report)` writes the same text through the
/// value's `print`: this is for a caller that wants the text as a value (to compare it, to store
/// it, or to hand it to something that reads strings).
slot_t checkTextKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *report = norm.get<const CheckReportObject *>(0);
    return toSlot(String::from(report->text(), mm::autoSpace()));
}

/// How many rows the table has.
slot_t checkRowCountKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *report = norm.get<const CheckReportObject *>(0);
    return toSlot<Int64>(static_cast<int64_t>(report->rows().size()));
}

/// One column of one row of the table. The row index is checked: a reader that asks for a row the
/// report does not have is answered with an error value naming the count, rather than with an empty
/// string that would read like a row whose reason is empty.
const CheckRow *rowAt(const CheckReportObject *report, int64_t at, const char *who) {
    if (at < 0 || static_cast<size_t>(at) >= report->rows().size()) {
        return nullptr;
    }
    return &report->rows()[static_cast<size_t>(at)];
}

slot_t rowColumnKernel(ArgsView &norm, const char *who, std::string CheckRow::*field) {
    const auto *report  = norm.get<const CheckReportObject *>(0);
    const int64_t at    = norm.get<Int64>(1);
    const CheckRow *row = rowAt(report, at, who);
    if (row == nullptr) {
        return makeError(
            "IndexError",
            std::format(
                "the report has {} row(s), and row {} was asked for", report->rows().size(), at),
            who);
    }
    return makeOk(
        toSlot(String::from(row->*field, mm::autoSpace())),
        TupleType::fallible(Type::String()));
}

slot_t checkRuleKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_rule>", &CheckRow::rule);
}

slot_t checkInstanceKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_instance>", &CheckRow::instance);
}

slot_t checkVerdictKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_verdict>", &CheckRow::verdict);
}

slot_t checkPropositionKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_proposition>", &CheckRow::proposition);
}

slot_t checkEvidenceKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_evidence>", &CheckRow::evidence);
}

slot_t checkReasonKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_reason>", &CheckRow::reason);
}

/// What the translation layer made of one row's rule (H247, slice S1): the proposition it read out
/// of the two sides, or `beyond translation coverage` with the reason it refused. A rule outside
/// the coverage is `sampled` and this is where the reason is written (the ruling of 2026-10-24 on
/// `verification-verdict-surface`, 3).
slot_t checkCoverageKernel(ArgsView &, ArgsView &norm, Context &) {
    return rowColumnKernel(norm, "<rewrite::check_coverage>", &CheckRow::coverage);
}

/// The declared coverage of the translation layer: which widths and operators it translates, what
/// it refuses, and that a rule's condition is not a premise yet. The boundary is a value and not a
/// paragraph, so a reader reads it and a test pins it.
slot_t coverageTableKernel(ArgsView &, ArgsView &, Context &) {
    return toSlot(String::from(camel::verify::coverageTable(), mm::autoSpace()));
}

class RewriteExecutor final : public Executor {
  public:
    explicit RewriteExecutor(context_ptr_t ctx) : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        return {
            {"rule", ruleKernel},
            {"rule_when", ruleWhenKernel},
            {"rule_generic", genericRuleKernel},
            {"name", nameKernel},
            {"two_way", twoWayKernel},
            {"needs", needsKernel},
            {"consts", constsKernel},
            {"rewrite", rewriteKernel},
            {"run", runKernel},
            {"explain", explainKernel},
            {"explain_all", explainAllKernel},
            {"why_not", whyNotKernel},
            {"saturate", saturateKernel},
            {"extract", extractKernel},
            {"check", checkKernel},
            {"check_text", checkTextKernel},
            {"check_row_count", checkRowCountKernel},
            {"check_rule", checkRuleKernel},
            {"check_instance", checkInstanceKernel},
            {"check_verdict", checkVerdictKernel},
            {"check_proposition", checkPropositionKernel},
            {"check_evidence", checkEvidenceKernel},
            {"check_reason", checkReasonKernel},
            {"check_coverage", checkCoverageKernel},
            {"check_coverage_table", coverageTableKernel},
            {"analysis", camel::rewritemod::analysisKernel},
        };
    }
};

class RewriteModule final : public BuiltinModule {
  public:
    explicit RewriteModule(context_ptr_t ctx) : BuiltinModule("rewrite", std::move(ctx)) {
        exportType(Reference("Rule"), RuleType::get());
        exportType(Reference("Proof"), ProofType::get());
        exportType(Reference("CheckReport"), CheckReportType::get());
        exportEntity(
            "__rule",
            OperatorGroup::create(
                "__rule",
                {{"rewrite:rule",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {6, {false, false, false, false, false, false}}},
                      "(name: string, two_way: bool, needs: int, consts: int, lhs: F, rhs: F) => "
                      "Rule",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->code() != TypeCode::String ||
                              norm[1]->code() != TypeCode::Bool ||
                              norm[2]->code() != TypeCode::Int64 ||
                              norm[3]->code() != TypeCode::Int64 ||
                              norm[4]->code() != TypeCode::Function ||
                              norm[5]->code() != TypeCode::Function)
                              return std::nullopt;
                          return RuleType::get();
                      })}}));
        exportEntity(
            "__rule_when",
            OperatorGroup::create(
                "__rule_when",
                {{"rewrite:rule_when",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {7, {false, false, false, false, false, false, false}}},
                      "(name: string, two_way: bool, needs: int, consts: int, lhs: F, rhs: F, "
                      "when: F) => Rule",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->code() != TypeCode::String ||
                              norm[1]->code() != TypeCode::Bool ||
                              norm[2]->code() != TypeCode::Int64 ||
                              norm[3]->code() != TypeCode::Int64 ||
                              norm[4]->code() != TypeCode::Function ||
                              norm[5]->code() != TypeCode::Function ||
                              norm[6]->code() != TypeCode::Function)
                              return std::nullopt;
                          const auto *when = static_cast<const FunctionType *>(norm[6]);
                          if (when->exitType() == nullptr ||
                              when->exitType()->code() != TypeCode::Bool)
                              return std::nullopt;
                          return RuleType::get();
                      })}}));
        // `__rule_generic`: the maker of a rule whose head declares type parameters. The front end
        // writes it instead of `__rule` / `__rule_when` when the declaration has a `::<T>` head: the
        // sides are generic functions, which have no value, so the maker is given the id of their
        // templates after `consts` (the ruling of 2026-10-23 on generic-rule-instantiation).
        exportEntity(
            "__rule_generic",
            OperatorGroup::create(
                "__rule_generic",
                {{"rewrite:rule_generic",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {5, {false, false, false, false, false}}},
                      "(name: string, two_way: bool, needs: int, consts: int, id: int) => Rule",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->code() != TypeCode::String ||
                              norm[1]->code() != TypeCode::Bool ||
                              norm[2]->code() != TypeCode::Int64 ||
                              norm[3]->code() != TypeCode::Int64 ||
                              norm[4]->code() != TypeCode::Int64)
                              return std::nullopt;
                          return RuleType::get();
                      })}}));
        // `__analysis` (rewrite design 4): the compiled form of an `analysis` declaration. Its
        // arity is 4 + k + 3n: name, initial, join, widen, the k pattern variables the query was
        // called with (one node, `n`, in the one-value form), then for each `on` clause its
        // pattern function, its fact function and the mask of its `const` parameters.
        exportEntity(
            "__analysis",
            OperatorGroup::create(
                "__analysis",
                {{"rewrite:analysis",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {-1, {}}},
                      "(name: string, initial: F, join: F, widen: F, node: Node, ...) => T",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm.size() < 5 || norm[0]->code() != TypeCode::String) {
                              return std::nullopt;
                          }
                          for (size_t i = 1; i < 4; ++i) {
                              if (norm[i]->code() != TypeCode::Function) {
                                  return std::nullopt;
                              }
                          }
                          // The query's pattern variables come first, one `Node` each; the `on`
                          // descriptions follow as triples that begin with a function.
                          size_t at = 4;
                          while (at < norm.size() && HandleType::is(norm[at], HandleKind::Node)) {
                              ++at;
                          }
                          if (at == 4 || at >= norm.size() || (norm.size() - at) % 3 != 0) {
                              return std::nullopt;
                          }
                          for (size_t i = at; i < norm.size(); i += 3) {
                              if (norm[i]->code() != TypeCode::Function ||
                                  norm[i + 1]->code() != TypeCode::Function ||
                                  norm[i + 2]->code() != TypeCode::Int64) {
                                  return std::nullopt;
                              }
                          }
                          const auto *initial =
                              static_cast<const FunctionType *>(norm[1]);
                          if (initial->exitType() == nullptr) {
                              return std::nullopt;
                          }
                          return initial->exitType();
                      })}}));
        // The callable operators (`rewrite`, `saturate`, `extract`, `check`, `run` and the four
        // rule readers) are declared in `stdlib/rewrite.cml`; the declarations are the one source
        // of their signatures, so the module exports none of them itself (handover H183).
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("rewrite", [ctx = context_]() {
            return std::make_shared<RewriteExecutor>(ctx);
        });
        // The readers are pure. `__rule` is not: constant folding would build the rule at
        // compile time and keep it in a static area, where the collector does not trace the
        // functions it holds. `__analysis` compiles an `analysis` declaration at macro time and
        // installs its facts, so it is effectful as well; it was the one operator of the module
        // whose purity was never declared, which nothing noticed while no program imported the
        // module (handover H173).
        camel::core::OperatorTable::instance().declare(
            "rewrite",
            {{"rule", false},
             {"rule_when", false},
             {"rule_generic", false},
             {"name", true},
             {"two_way", true},
             {"needs", true},
             {"consts", true},
             {"rewrite", false},
             {"run", false},
             {"explain", true},
             {"explain_all", true},
             {"why_not", false},
             {"saturate", false},
             {"extract", false},
             {"check", false},
             {"check_text", true},
             {"check_row_count", true},
             {"check_rule", true},
             {"check_instance", true},
             {"check_verdict", true},
             {"check_proposition", true},
             {"check_evidence", true},
             {"check_reason", true},
             {"check_coverage", true},
             {"check_coverage_table", true},
             {"analysis", false}});
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new RewriteModule(ctx->shared_from_this());
}

