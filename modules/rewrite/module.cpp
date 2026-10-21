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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The rewrite module: rules as values, and the engines that apply them to a program.
 *
 * `rule name(params) { left => right }` is read as two functions of the same parameters and
 * `let name = __rule('name', ...)` (graph store 5.2: a rule is the shorthand of a pair of
 * functions); the front end writes the import of this module itself. The engines, `rewrite`,
 * `saturate` and `extract`, take the rules as an array.
 */

#include "rule.h"

#include "analysis.h"

#include "../graph/handle.h"

#include "camel/core/context/context.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator_table.h"
#include "camel/core/operator_traits.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "camel/runtime/rules.h"
#include "camel/runtime/writings.h"
#include "src/passes/sched/nodevm/nodevm.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <random>

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::rtdata;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::graphmod::Handle;
using camel::graphmod::HandleKind;
using camel::graphmod::HandleType;
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

/// The rule a rule function makes: the function is called (a rule is the function of a
/// declaration, with no parameter).
RuleObject *callRule(Context &ctx, ::Function *maker) {
    NodeVMSchedPass vm(ctx.shared_from_this());
    const slot_t made = vm.callGraph(maker->program(), maker->graph(), {});
    return static_cast<RuleObject *>(fromSlot<Object *>(made));
}

/// The state a condition closure needs: the rule object, rooted so that the closure can read the
/// condition function from it after every safepoint, and the node VM the conditions of one engine
/// call share.
struct WhenCall {
    std::shared_ptr<mm::RootHandle> rule;
    std::shared_ptr<NodeVMSchedPass> vm;
};

/// Whether a rule's condition holds at this match. Its parameters are Node handles of the draft
/// being rewritten, one per parameter of the rule; a parameter the left side does not use names
/// no node and faults when it is read.
bool callWhen(const WhenCall &call, const camel::runtime::Match &match) {
    const size_t arity = static_cast<size_t>(
        call.rule->getAs<RuleObject>()->when()->graph()->funcType()->normTypesCount());
    camel::runtime::GraphDraft &draft = match.writings.draft();
    std::vector<std::unique_ptr<mm::RootHandle>> roots;
    std::vector<slot_t> args;
    roots.reserve(arity);
    args.reserve(arity);
    for (size_t i = 0; i < arity; ++i) {
        const camel::runtime::gc_node_ref_t ref =
            i < match.vars.size() ? match.vars[i] : camel::runtime::kInvalidNodeRef;
        Handle *handle = Handle::createInDraft(draft, ref, mm::autoSpace());
        roots.push_back(
            std::make_unique<mm::RootHandle>(
                mm::autoSpace(),
                handle,
                HandleType::get(HandleKind::Node),
                "rewrite.whenArg"));
    }
    // Making a handle can collect: the arguments are read from the roots after the last one, so
    // each slot names the handle where the collector put it.
    for (const auto &root : roots) {
        args.push_back(toSlot(root->getAs<Handle>()));
    }
    // A call can move the objects: the condition is read from the rooted rule after the last
    // allocation and immediately before the call.
    ::Function *when    = call.rule->getAs<RuleObject>()->when();
    const slot_t result = call.vm->callGraph(when->program(), when->graph(), args);
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
        source.when = [call](const camel::runtime::Match &match) { return callWhen(*call, match); };
    }
    return source;
}

/// The engine's rules for the rule functions of an array.
///
/// A function is called for each rule, which can move the objects (a safepoint): the array is read
/// again from the argument, which the collector keeps up to date, each time.
std::vector<camel::runtime::Rule> engineRules(Context &ctx, ArgsView &norm, size_t argument) {
    std::vector<camel::runtime::Rule> out;
    auto vm            = std::make_shared<NodeVMSchedPass>(ctx.shared_from_this());
    uint16_t next      = 1;
    const size_t count = norm.get<Array *>(argument)->size();
    for (size_t i = 0; i < count; ++i) {
        RuleObject *rule = callRule(ctx, norm.get<Array *>(argument)->get<::Function *>(i));
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

/// Whether a graph is one side of a rule: those are the patterns, not part of the program the
/// rules are applied to.
bool isRuleSide(const camel::runtime::GCGraph &graph) {
    const std::string &name = graph.name();
    return name.ends_with("__lhs") || name.ends_with("__rhs") || name.ends_with("__when");
}

/// `rewrite(p, rules)`: the program with the rules applied one way, in place, to a fixpoint
/// (rewrite design 3). An error value when a rule cannot be a rule, holds both ways, or the
/// result is not a program.
slot_t rewriteKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    try {
        const auto compiled = engineRules(ctx, norm, 1);
        camel::runtime::RuntimeGraphDraftSession session(ctx.shared_from_this(), base);
        // A condition is Camel code, and so is a safepoint: every draft this session has opened
        // must keep its static values relocatable until the commit reads them again, not only the
        // one the engine is rewriting now.
        camel::runtime::RuntimeGraphDraftSession::RootScope draftRoots(session);
        for (camel::runtime::GCGraph *graph : base->graphs()) {
            if (isRuleSide(*graph)) {
                continue;
            }
            camel::runtime::GraphDraft &draft = session.edit(graph);
            camel::runtime::DraftWritings writings(
                draft,
                camel::core::sem::NumericPolicy::relaxed());
            writings.rewrite(compiled);
        }
        const camel::runtime::Program *committed = session.commitChecked();
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(committed)), resultType);
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::rewrite>");
    } catch (const std::invalid_argument &e) {
        return makeError("RuleTwoWay", e.what(), "<rewrite::rewrite>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::rewrite>");
    }
}

/// `saturate(p, rules, rounds)`: the program with the rules applied both ways to the writings of
/// every value, adding writings until nothing new is found or `rounds` rounds were made
/// (rewrite design 3, graph store 3.5). The result has values with several writings: `extract`
/// (or the pipeline, before a pass that runs or translates it) chooses one.
slot_t saturateKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const TupleType *resultType = TupleType::fallible(norm.type(0));
    const auto *base            = norm.get<const camel::runtime::Program *>(0);
    const int64_t rounds        = norm.get<Int64>(2);
    try {
        const auto compiled = engineRules(ctx, norm, 1);
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
                camel::core::sem::NumericPolicy::relaxed());
            writings.build();
            writings.saturate(compiled, budget);
            writings.save();
        }
        const camel::runtime::Program *committed = session.commitChecked();
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(committed)), resultType);
    } catch (const camel::runtime::RuleCompileError &e) {
        return makeError("RuleInvalid", e.what(), "<rewrite::saturate>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::saturate>");
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
    try {
        const camel::runtime::Program *extracted =
            camel::runtime::extractWritings(ctx.shared_from_this(), base, *model);
        return makeOk(toSlot(const_cast<camel::runtime::Program *>(extracted)), resultType);
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &e) {
        return makeError("RewriteInvalid", e.what(), "<rewrite::extract>");
    }
}

// ---- the check of a rule (graph store decision 5) ----

/// A value of a scalar type, as the operators read it.
struct Sample {
    TypeCode code;
    slot_t slot;
    double asDouble;
};

/// Random values of one scalar type. Floats include the special values the permissions of the
/// rule do not rule out.
class Sampler {
  public:
    Sampler(uint64_t seed, camel::core::sem::PermissionMask needs) : rng_(seed), needs_(needs) {}

    Sample value(TypeCode code, bool constant) {
        switch (code) {
        case TypeCode::Int64: {
            const int64_t v = pickInt(constant);
            return {code, toSlot<Int64>(v), static_cast<double>(v)};
        }
        case TypeCode::Int32: {
            const int32_t v = static_cast<int32_t>(pickInt(constant));
            return {code, toSlot<Int32>(v), static_cast<double>(v)};
        }
        case TypeCode::Float64: {
            const double v = pickFloat(constant, code);
            return {code, toSlot<Float64>(v), v};
        }
        case TypeCode::Float32: {
            const float v = static_cast<float>(pickFloat(constant, code));
            return {code, toSlot<Float32>(v), static_cast<double>(v)};
        }
        case TypeCode::Bool: {
            const bool v = (rng_() & 1) != 0;
            return {code, toSlot<Bool>(v), v ? 1.0 : 0.0};
        }
        default:
            throw std::invalid_argument("a parameter of a rule has a type the check cannot make");
        }
    }

  private:
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
        std::vector<double> specials{0.0, 1.0, -1.0, 2.0, 0.5, 3.0, 0.1};
        // A special has to be a value of the parameter's own type: 1e300 is finite as a float64
        // and an infinity as a float32, which would smuggle an infinity past `no_infs` and make
        // every float32 rule that assumes finiteness fail its own check.
        if (code == TypeCode::Float32) {
            specials.push_back(1e-30);
            specials.push_back(1e30);
            specials.push_back(1e-40); // subnormal
        } else {
            specials.push_back(1e-300);
            specials.push_back(1e300);
            specials.push_back(1e-310); // subnormal
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
        const double range = constant ? 64.0 : 1000.0;
        return (static_cast<double>(rng_() % 2000001) / 1000000.0 - 1.0) * range;
    }

    std::mt19937_64 rng_;
    camel::core::sem::PermissionMask needs_;
};

/// Whether two results agree: bit for bit when the rule needs no permission (a NaN is a NaN),
/// otherwise within the numeric contract of the rule's permissions and the operators' declared
/// bounds (camel/core/operator_semantics.h, rewriteErrorBound).
bool agree(
    const Sample &a, const Sample &b, const camel::core::sem::RewriteErrorBound &bound,
    double scale) {
    if (a.code == TypeCode::Float64 || a.code == TypeCode::Float32) {
        const bool single = a.code == TypeCode::Float32;
        const double x = a.asDouble, y = b.asDouble;
        if (std::isnan(x) || std::isnan(y)) {
            return std::isnan(x) && std::isnan(y);
        }
        if (!bound.checkable) {
            return true; // no contract to compare against (reported as unchecked)
        }
        if (bound.ulps == 0.0 && bound.relative == 0.0) {
            return single ? fromSlot<Float32>(a.slot) == fromSlot<Float32>(b.slot) &&
                                std::signbit(x) == std::signbit(y)
                          : a.slot == b.slot;
        }
        if (std::isinf(x) || std::isinf(y)) {
            return x == y;
        }
        const double eps       = single ? 1.2e-7 : 2.3e-16;
        const double magnitude = std::abs(x) + std::abs(y) + scale;
        // The smallest subnormal: a rounding in the subnormal range is absolute, not relative.
        const double smallest  = single ? 1.4e-45 : 4.9e-324;
        const double allowed =
            (bound.ulps * eps + bound.relative) * magnitude + bound.ulps * smallest;
        return std::abs(x - y) <= allowed;
    }
    if (a.code == TypeCode::Int32) {
        return fromSlot<Int32>(a.slot) == fromSlot<Int32>(b.slot);
    }
    return a.slot == b.slot;
}

std::string sampleText(const Sample &s) {
    switch (s.code) {
    case TypeCode::Int64:
        return std::to_string(fromSlot<Int64>(s.slot));
    case TypeCode::Int32:
        return std::to_string(fromSlot<Int32>(s.slot));
    case TypeCode::Bool:
        return fromSlot<Bool>(s.slot) ? "true" : "false";
    default:
        return std::format("{}", s.asDouble);
    }
}

Sample resultOf(slot_t slot, TypeCode code) {
    switch (code) {
    case TypeCode::Int64:
        return {code, slot, static_cast<double>(fromSlot<Int64>(slot))};
    case TypeCode::Int32:
        return {code, slot, static_cast<double>(fromSlot<Int32>(slot))};
    case TypeCode::Float32:
        return {code, slot, static_cast<double>(fromSlot<Float32>(slot))};
    case TypeCode::Float64:
        return {code, slot, fromSlot<Float64>(slot)};
    default:
        return {code, slot, fromSlot<Bool>(slot) ? 1.0 : 0.0};
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

/// `check(rules, samples, seed)`: for every rule, `samples` random instances (random constants
/// for the const parameters, random values for the others) of its two sides, which must agree.
/// Returns the report; a line starting with FAIL is a rule that does not hold.
slot_t checkKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const int64_t samples = std::max<int64_t>(1, norm.get<Int64>(1));
    const uint64_t seed   = static_cast<uint64_t>(norm.get<Int64>(2));
    std::string report;
    const size_t count = norm.get<Array *>(0)->size();
    for (size_t index = 0; index < count; ++index) {
        // The array moves when a function runs: it is read again each time.
        RuleObject *made = callRule(ctx, norm.get<Array *>(0)->get<::Function *>(index));
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
        const bool conditional = rule->when() != nullptr;
        // The rule object is read now and not again: a function runs below, and objects move.
        const camel::runtime::RuleSource source = sourceOf(*rule, 1, call);
        const std::string name(rule->name());
        const camel::core::sem::PermissionMask needs = rule->needs();
        const uint64_t constParams              = rule->constParams();
        const camel::runtime::Program *program  = rule->lhs()->program();
        const camel::runtime::GraphId lhsId     = rule->lhs()->graphId();
        const camel::runtime::GraphId rhsId     = rule->rhs()->graphId();
        const camel::runtime::GCGraph *lhsGraph = program->graph(lhsId);
        const auto *lhsType                     = lhsGraph->funcType();
        const size_t params                     = lhsType->normTypesCount();
        const TypeCode resultCode               = lhsType->exitType()->code();
        const size_t steps = stepsOf(*lhsGraph) + stepsOf(*program->graph(rhsId));
        const double declaredUlps =
            declaredUlpsOf(*lhsGraph) + declaredUlpsOf(*program->graph(rhsId));
        // The bound is the numeric contract of the permissions the rule needs, read together
        // with the operators' declarations (not one uniform rounding bound).
        const camel::core::sem::RewriteErrorBound bound =
            camel::core::sem::rewriteErrorBound(needs, steps, declaredUlps);
        Sampler sampler(seed * 1000003u + index, needs);
        NodeVMSchedPass vm(ctx.shared_from_this());
        std::string failure;
        if (!bound.checkable) {
            // `nondeterministic` has no numeric contract: there is nothing to compare against,
            // and reporting `ok` would say the rule was checked. It was not.
            report += std::format(
                "unchecked {}: the permissions it needs have no numeric bound\n", name);
            continue;
        }
        // A condition is part of the equation: the two sides need not agree on values the
        // condition rejects, and the tool cannot ask the condition about sampled values. Such a
        // rule is checked by the engine path below only.
        for (int64_t sample = 0; !conditional && sample < samples && failure.empty(); ++sample) {
            std::vector<Sample> values;
            std::vector<slot_t> args;
            double scale = 1.0;
            for (size_t i = 0; i < params; ++i) {
                const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
                values.push_back(sampler.value(lhsType->normTypeAt(i)->code(), isConst));
                args.push_back(values.back().slot);
                scale = std::max(scale, std::abs(values.back().asDouble) + 1.0);
            }
            const Sample left =
                resultOf(vm.callGraph(program, program->graph(lhsId), args), resultCode);
            const Sample right =
                resultOf(vm.callGraph(program, program->graph(rhsId), args), resultCode);
            if (!agree(left, right, bound, std::min(scale * scale, std::numeric_limits<double>::max()))) {
                std::string at;
                for (size_t i = 0; i < values.size(); ++i) {
                    at += (i == 0 ? "" : ", ") + sampleText(values[i]);
                }
                failure = std::format(
                    "FAIL {}: the two sides differ at ({}): {} and {}",
                    name,
                    at,
                    sampleText(left),
                    sampleText(right));
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
                std::vector<Sample> values;
                double scale = 1.0;
                for (size_t i = 0; i < params; ++i) {
                    const bool isConst = i < 64 && ((constParams >> i) & 1) != 0;
                    Type *type         = lhsType->normTypeAt(i);
                    Sample value       = sampler.value(type->code(), isConst);
                    args.push_back(value.slot);
                    values.push_back(value);
                    scale = std::max(scale, std::abs(value.asDouble) + 1.0);
                    if (isConst) {
                        const auto constant = draft.materializeStaticValue(value.slot, type);
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
                const Sample before =
                    resultOf(vm.callGraph(program, program->graph(lhsId), args), resultCode);
                const Sample after =
                    resultOf(vm.callGraph(rewritten, rewritten->graph(lhsId), args), resultCode);
                if (!agree(before, after, bound, std::min(scale * scale, std::numeric_limits<double>::max()))) {
                    std::string at;
                    for (const Sample &value : values) {
                        at += (at.empty() ? "" : ", ") + sampleText(value);
                    }
                    failure = std::format(
                        "FAIL {}: the rewritten left side differs at ({}): {} and {} "
                        "(bound {} ulps, {} relative, scale {})",
                        name,
                        at,
                        sampleText(before),
                        sampleText(after),
                        bound.ulps,
                        bound.relative,
                        scale * scale);
                }
            }
            if (failure.empty() && fired == 0) {
                failure = std::format(
                    "FAIL {}: the rule never fired on its own left side in {} tries",
                    name,
                    tried);
            }
        }
        report +=
            failure.empty()
                ? (conditional ? std::format(
                                     "ok {}: the condition selects the instances, fired {} of "
                                     "{}\n",
                                     name,
                                     fired,
                                     tried)
                               : std::format(
                                     "ok {}: {} samples agree, fired {} of {}\n",
                                     name,
                                     samples,
                                     fired,
                                     tried))
                : failure + "\n";
    }
    return toSlot(String::from(report, mm::autoSpace()));
}

class RewriteExecutor final : public Executor {
  public:
    explicit RewriteExecutor(context_ptr_t ctx) : Executor(std::move(ctx), kernels()) {}

  private:
    static std::unordered_map<std::string, operator_t> kernels() {
        return {
            {"rule", ruleKernel},
            {"rule_when", ruleWhenKernel},
            {"name", nameKernel},
            {"two_way", twoWayKernel},
            {"needs", needsKernel},
            {"consts", constsKernel},
            {"rewrite", rewriteKernel},
            {"saturate", saturateKernel},
            {"extract", extractKernel},
            {"check", checkKernel},
            {"analysis", camel::rewritemod::analysisKernel},
        };
    }
};

oper_group_ptr_t ruleReader(const char *name, Type *result) {
    return OperatorGroup::create(
        name,
        {{std::string("rewrite:") + name,
          StaticFuncTypeResolver::create({}, {{RuleType::get(), false}}, result)}});
}

class RewriteModule final : public BuiltinModule {
  public:
    explicit RewriteModule(context_ptr_t ctx) : BuiltinModule("rewrite", std::move(ctx)) {
        exportType(Reference("Rule"), RuleType::get());
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
        const auto rulesOf = [](Type *array) {
            if (array->code() != TypeCode::Array) {
                return false;
            }
            const Type *element = static_cast<ArrayType *>(array)->elemType();
            if (element == nullptr || element->code() != TypeCode::Function) {
                return false;
            }
            const auto *maker = static_cast<const FunctionType *>(element);
            return maker->normTypesCount() == 0 && maker->withTypesCount() == 0 &&
                   RuleType::is(maker->exitType());
        };
        exportEntity(
            "rewrite",
            OperatorGroup::create(
                "rewrite",
                {{"rewrite:rewrite",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {2, {false, false}}},
                      "(p: Program, rules: (() => Rule)[]) => Program!",
                      [rulesOf](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          // The Program type belongs to the module graph; it is told by its name.
                          if (norm[0]->toString() != "Program" || !rulesOf(norm[1]))
                              return std::nullopt;
                          return TupleType::fallible(norm[0]);
                      })}}));
        exportEntity(
            "saturate",
            OperatorGroup::create(
                "saturate",
                {{"rewrite:saturate",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {3, {false, false, false}}},
                      "(p: Program, rules: (() => Rule)[], rounds: int) => Program!",
                      [rulesOf](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->toString() != "Program" || !rulesOf(norm[1]) ||
                              norm[2]->code() != TypeCode::Int64)
                              return std::nullopt;
                          return TupleType::fallible(norm[0]);
                      })}}));
        exportEntity(
            "extract",
            OperatorGroup::create(
                "extract",
                {{"rewrite:extract",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {2, {false, false}}},
                      "(p: Program, cost: string) => Program!",
                      [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (norm[0]->toString() != "Program" ||
                              norm[1]->code() != TypeCode::String)
                              return std::nullopt;
                          return TupleType::fallible(norm[0]);
                      })}}));
        exportEntity(
            "check",
            OperatorGroup::create(
                "check",
                {{"rewrite:check",
                  DynamicFuncTypeResolver::create(
                      {{0, {}}, {3, {false, false, false}}},
                      "(rules: (() => Rule)[], samples: int, seed: int) => string",
                      [rulesOf](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                          -> std::optional<Type *> {
                          if (!rulesOf(norm[0]) || norm[1]->code() != TypeCode::Int64 ||
                              norm[2]->code() != TypeCode::Int64)
                              return std::nullopt;
                          return Type::String();
                      })}}));
        exportEntity("rule_name", ruleReader("name", Type::String()));
        exportEntity("rule_two_way", ruleReader("two_way", Type::Bool()));
        exportEntity("rule_needs", ruleReader("needs", Type::Int64()));
        exportEntity("rule_consts", ruleReader("consts", Type::Int64()));
    }

    bool load() override {
        if (loaded_)
            return true;
        context_->registerExecutorFactory("rewrite", [ctx = context_]() {
            return std::make_shared<RewriteExecutor>(ctx);
        });
        // The readers are pure. `__rule` is not: constant folding would build the rule at
        // compile time and keep it in a static area, where the collector does not trace the
        // functions it holds.
        camel::core::OperatorTable::instance().declare(
            "rewrite",
            {{"rule", false},
             {"rule_when", false},
             {"name", true},
             {"two_way", true},
             {"needs", true},
             {"consts", true},
             {"rewrite", false},
             {"saturate", false},
             {"extract", false},
             {"check", false}});
        loaded_ = true;
        return true;
    }
};

} // namespace

extern "C" Module *camel_module_create(Context *ctx) {
    return new RewriteModule(ctx->shared_from_this());
}

