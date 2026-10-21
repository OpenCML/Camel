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
 * Test-only: the local algebra of std::opt::algebra written as rules, saturated and extracted.
 * test::saturate runs under the default numeric policy (the order and grouping of a sum or
 * product may change), test::saturate_safe under the strict one. For each function of the
 * corpus it prints "SATURATE <name> add=<n> mul=<n>": the operators left after extraction.
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

const Program *saturateProgram(
    const camel::core::context::context_ptr_t &ctx, const Program *program, std::ostream &os,
    sem::NumericPolicy policy, const char *extraTarget = nullptr, size_t rounds = Budget{}.rounds) {
    static const std::set<std::string> kTargets{
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
    std::set<std::string> targets = kTargets;
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
        os << "SATURATE " << name << " add=" << adds << " mul=" << muls << " waits=" << waits << "\n";
    }
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
