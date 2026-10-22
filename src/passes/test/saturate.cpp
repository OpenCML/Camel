// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only: the local algebra of std::opt::algebra written as rules, saturated and extracted.
 * test::saturate runs under the default numeric policy (the order and grouping of a sum or
 * product may change), test::saturate_safe under the strict one. For each function of the
 * corpus it prints "SATURATE <name> add=<n> mul=<n>": the operators left after extraction.
 * test::saturate_props repeats the run to report the properties the counters stand for: every
 * function reaches a fixed point within the budget, a second saturation of the same writings
 * adds nothing, two sessions over the same program agree, and the same operators remain.
 */

#include "camel/passes/test/saturate.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/writings.h"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <type_traits>

using namespace camel::runtime;
using camel::core::OperatorTable;
namespace sem = camel::core::sem;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::toSlot;

namespace {

/// The rules of one element type: `suffix` names its operators (:op/add_d), `type` its type.
template <typename T>
std::vector<Rule> rulesFor(const char *suffix, camel::core::type::Type *type, uint16_t &nextId) {
    const auto find = [&](const char *name) {
        const auto id = OperatorTable::instance().lookup(std::string(":op/") + name + "_" + suffix);
        return id ? *id : camel::core::kNoOper;
    };
    const auto add = find("add"), mul = find("mul");
    std::vector<Rule> rules;
    const auto rule = [&](std::string name, sem::PermissionMask needs, Pattern lhs, Pattern rhs,
                          std::function<bool(const Match &)> when = {}) {
        rules.push_back(
            Rule{name + "_" + suffix, nextId++, needs, std::move(lhs), std::move(rhs), std::move(when)});
    };
    const auto val = [](const Match &m, uint32_t var) { return fromSlot<T>(m.constant(var)->first); };
    constexpr bool floating = std::is_floating_point_v<T>;
    using U = typename std::conditional_t<floating, std::type_identity<T>, std::make_unsigned<T>>::type;
    const auto sum = [](std::span<const slot_t> v) {
        return floating ? toSlot<T>(fromSlot<T>(v[0]) + fromSlot<T>(v[1]))
                        : toSlot<T>(static_cast<T>(static_cast<U>(fromSlot<T>(v[0])) +
                                                   static_cast<U>(fromSlot<T>(v[1]))));
    };
    const auto product = [](std::span<const slot_t> v) {
        return floating ? toSlot<T>(fromSlot<T>(v[0]) * fromSlot<T>(v[1]))
                        : toSlot<T>(static_cast<T>(static_cast<U>(fromSlot<T>(v[0])) *
                                                   static_cast<U>(fromSlot<T>(v[1]))));
    };

    // Exact for every type: the operands of + and * can be swapped.
    rule("add_comm", 0, patOp(add, {patVar(0), patVar(1)}), patOp(add, {patVar(1), patVar(0)}));
    rule("mul_comm", 0, patOp(mul, {patVar(0), patVar(1)}), patOp(mul, {patVar(1), patVar(0)}));

    if constexpr (!floating) {
        // Neutral elements of wrapping integers.
        const Pattern zero = patLiteral(toSlot<T>(T{0}), type);
        const Pattern one  = patLiteral(toSlot<T>(T{1}), type);
        rule("add_zero_right", 0, patOp(add, {patVar(0), zero}), patVar(0));
        rule("add_zero_left", 0, patOp(add, {zero, patVar(0)}), patVar(0));
        rule("mul_one_right", 0, patOp(mul, {patVar(0), one}), patVar(0));
        rule("mul_one_left", 0, patOp(mul, {one, patVar(0)}), patVar(0));
    }

    // x*c + x*c = x*(2c): exact for an integer, and for a float c that is a whole number
    // (doubling a rounded product commutes with rounding).
    const auto two = [](std::span<const slot_t> v) {
        return floating ? toSlot<T>(fromSlot<T>(v[0]) * T{2})
                        : toSlot<T>(static_cast<T>(static_cast<U>(fromSlot<T>(v[0])) * U{2}));
    };
    rule(
        "double_product",
        0,
        patOp(
            add,
            {patOp(mul, {patVar(0), patConst(1)}), patOp(mul, {patVar(0), patConst(1)})}),
        patOp(mul, {patVar(0), patCompute(type, two, {patVar(1)})}),
        [=](const Match &m) {
            if constexpr (floating) {
                const T c = val(m, 1);
                return std::isfinite(c) && std::abs(c) >= T{1} && std::trunc(c) == c &&
                       std::abs(c) <= std::ldexp(T{1}, std::numeric_limits<T>::digits - 1);
            }
            return true;
        });

    // x*c1 + x*c2 = x*(c1+c2): exact for wrapping integers; a float changes the rounding path.
    rule(
        "combine_coefficients",
        floating ? sem::kReassociate : sem::kNoPermissions,
        patOp(add, {patOp(mul, {patVar(0), patConst(1)}), patOp(mul, {patVar(0), patConst(2)})}),
        patOp(mul, {patVar(0), patCompute(type, sum, {patVar(1), patVar(2)})}),
        [=](const Match &m) {
            if constexpr (floating) {
                const T a = val(m, 1), b = val(m, 2), c = a + b;
                return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && c != T{0} &&
                       a != T{0} && b != T{0} && std::signbit(a) == std::signbit(b);
            }
            return true;
        });

    // (x*c1)*c2 = x*(c1*c2): exact for wrapping integers; for a float it can remove an
    // overflowing intermediate, so it needs the assumption that there is no infinity.
    rule(
        "fold_coefficients",
        floating ? static_cast<sem::PermissionMask>(sem::kReassociate | sem::kNoInfs)
                 : sem::kNoPermissions,
        patOp(mul, {patOp(mul, {patVar(0), patConst(1)}), patConst(2)}),
        patOp(mul, {patVar(0), patCompute(type, product, {patVar(1), patVar(2)})}),
        [=](const Match &m) {
            if constexpr (floating) {
                const T a = val(m, 1), b = val(m, 2), c = a * b;
                return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && a != T{0} &&
                       b != T{0} && c != T{0};
            }
            return true;
        });
    return rules;
}

std::vector<Rule> corpusRules() {
    uint16_t next = 1;
    std::vector<Rule> rules;
    for (auto &part : {
             rulesFor<double>("d", camel::core::type::Type::Float64(), next),
             rulesFor<float>("f", camel::core::type::Type::Float32(), next),
             rulesFor<int64_t>("l", camel::core::type::Type::Int64(), next),
             rulesFor<int32_t>("i", camel::core::type::Type::Int32(), next),
         }) {
        rules.insert(rules.end(), part.begin(), part.end());
    }
    return rules;
}

const std::set<std::string> kCorpusTargets{
    "strict",
    "underflow_sensitive",
    "strict_coefficients",
    "relaxed_coefficients",
    "mixed_permissions",
    "integer_coefficients",
    "integer_chain",
    "shared_product",
    "strict32",
    "integer32"};

/// Prints the operators each saturated function was left with: the observable result the rules
/// work for. A rule that never fires leaves an `add` or a `mul` behind, so this also says that
/// the saturation did not stop early.
void printOperatorCounts(
    const Program *next, const std::vector<std::pair<std::string, GraphId>> &done,
    std::ostream &os) {
    for (const auto &[name, id] : done) {
        size_t adds = 0, muls = 0, waits = 0;
        const GCGraph *graph = next->graph(id);
        for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (graph->node(ref)->kind != GCNodeKind::Oper) {
                continue;
            }
            const auto uri = graph->nodeBodyAs<GCOperBody>(ref)->uri();
            waits += graph->ctrlInputsOf(ref).empty() ? 0 : 1;
            adds += uri.starts_with(":op/add_");
            muls += uri.starts_with(":op/mul_");
        }
        os << "SATURATE " << name << " add=" << adds << " mul=" << muls << " waits=" << waits
           << "\n";
    }
}

/// Whether two runs of the same saturation found and changed the same things. `saturated` is
/// the termination property: a run whose budget ended first has not reached a fixed point.
bool sameCounters(const SaturateStats &a, const SaturateStats &b) {
    return a.rounds == b.rounds && a.matches == b.matches && a.newWritings == b.newWritings &&
           a.unions == b.unions && a.saturated == b.saturated;
}

const Program *saturateProgram(
    const camel::core::context::context_ptr_t &ctx, const Program *program, std::ostream &os,
    sem::NumericPolicy policy, const char *extraTarget = nullptr, size_t rounds = Budget{}.rounds) {
    std::set<std::string> targets = kCorpusTargets;
    if (extraTarget != nullptr) {
        targets.insert(extraTarget);
    }
    const std::vector<Rule> rules = corpusRules();
    RuntimeGraphDraftSession session(ctx, program);
    std::vector<std::pair<std::string, GraphId>> done;
    for (GCGraph *graph : program->graphs()) {
        if (!targets.contains(graph->name())) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        const auto t0 = std::chrono::steady_clock::now();
        DraftWritings writings(draft, policy);
        writings.build();
        os << "SATURATE-TIME " << graph->name() << " build_ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count() << "\n";
        Budget budget;
        budget.rounds = rounds;
        const auto began = std::chrono::steady_clock::now();
        const SaturateStats stats = writings.saturate(rules, budget);
        os << "SATURATE-TIME " << graph->name() << " ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - began)
                  .count()
           << "\n";
        const auto t1 = std::chrono::steady_clock::now();
        const ExtractStats extracted = writings.extract(*findCostModel("nodes"), ctx.get());
        os << "SATURATE-TIME " << graph->name() << " extract_ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t1).count() << "\n";
        os << "SATURATE-TIME " << graph->name() << " find_ms=" << static_cast<int>(stats.findSeconds * 1000)
           << " apply_ms=" << static_cast<int>(stats.applySeconds * 1000) << "\n";
        os << "SATURATE-INFO " << graph->name() << " rounds=" << stats.rounds
           << " matches=" << stats.matches << " writings=" << stats.newWritings
           << " unions=" << stats.unions << " saturated=" << stats.saturated
           << " erased=" << extracted.erased << "\n";
        done.emplace_back(graph->name(), graph->id());
    }
    const Program *next = session.commit();
    printOperatorCounts(next, done, os);
    return next;
}

/// The properties of saturation, on the algebra corpus under the default numeric policy.
///
/// One `SaturateStats` snapshot (rounds, matches, writings, unions of a particular rule set and
/// matcher) is not a property: it changes whenever the matcher or the rules change. What the
/// design promises is weaker and still testable — the run terminates, the writings are a fixed
/// point (saturating them again adds no writing and no union), and the same program and rules
/// give the same counters on every run. The result the rules are for (the operators left after
/// extraction) is printed too, so a rule that stopped firing is visible.
const Program *saturateProgramChecked(
    const camel::core::context::context_ptr_t &ctx, const Program *program, std::ostream &os,
    sem::NumericPolicy policy) {
    const std::vector<Rule> rules = corpusRules();
    const Budget budget;

    // A first run, and a second saturation of the same writings. The session is not committed:
    // only the counters and the fixed point are read from it.
    std::vector<std::pair<SaturateStats, SaturateStats>> probe;
    {
        RuntimeGraphDraftSession session(ctx, program);
        for (GCGraph *graph : program->graphs()) {
            if (!kCorpusTargets.contains(graph->name())) {
                continue;
            }
            GraphDraft &draft = session.edit(graph);
            DraftWritings writings(draft, policy);
            writings.build();
            const SaturateStats first = writings.saturate(rules, budget);
            const SaturateStats again = writings.saturate(rules, budget);
            probe.emplace_back(first, again);
        }
    }

    // A second run over the same program: it must find the same counters, and its result is the
    // program the pipeline gets.
    RuntimeGraphDraftSession session(ctx, program);
    std::vector<std::pair<std::string, GraphId>> done;
    size_t saturated = 0, fixpoint = 0, stable = 0;
    size_t run = 0;
    for (GCGraph *graph : program->graphs()) {
        if (!kCorpusTargets.contains(graph->name())) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        DraftWritings writings(draft, policy);
        writings.build();
        const SaturateStats stats = writings.saturate(rules, budget);
        const SaturateStats &first = probe[run].first;
        const SaturateStats &again = probe[run].second;
        ++run;
        const bool same  = sameCounters(first, stats);
        const bool fixed = again.newWritings == 0 && again.unions == 0 && again.saturated;
        saturated += stats.saturated ? 1 : 0;
        fixpoint += fixed ? 1 : 0;
        stable += same ? 1 : 0;
        const ExtractStats extracted = writings.extract(*findCostModel("nodes"), ctx.get());
        os << "SATURATE-INFO " << graph->name() << " rounds=" << stats.rounds
           << " matches=" << stats.matches << " writings=" << stats.newWritings
           << " unions=" << stats.unions << " saturated=" << stats.saturated
           << " erased=" << extracted.erased << "\n";
        os << "SATURATE-AGAIN " << graph->name() << " writings=" << again.newWritings
           << " unions=" << again.unions << " saturated=" << again.saturated
           << " rounds=" << again.rounds << " matches=" << again.matches << "\n";
        os << "SATURATE-STABLE " << graph->name() << " same=" << (same ? 1 : 0) << "\n";
        done.emplace_back(graph->name(), graph->id());
    }
    const Program *next = session.commit();
    printOperatorCounts(next, done, os);
    os << "SATURATE-PROPS targets=" << done.size() << " saturated=" << saturated
       << " fixpoint=" << fixpoint << " stable=" << stable << "\n";
    return next;
}

} // namespace

const camel::runtime::Program *
SaturateTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return saturateProgram(context_, program, os, sem::NumericPolicy::relaxed());
}

const camel::runtime::Program *
SaturateSafeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return saturateProgram(context_, program, os, sem::NumericPolicy::strict());
}

const camel::runtime::Program *
SaturateWideTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return saturateProgram(context_, program, os, sem::NumericPolicy::relaxed(), "wide");
}

const camel::runtime::Program *
SaturateOneRoundTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return saturateProgram(context_, program, os, sem::NumericPolicy::relaxed(), nullptr, 1);
}

const camel::runtime::Program *
SaturatePropsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return saturateProgramChecked(context_, program, os, sem::NumericPolicy::relaxed());
}
