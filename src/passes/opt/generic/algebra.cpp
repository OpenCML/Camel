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
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* Local scalar algebra, independent of autodiff. Matching never expands an expression tree. */
#include "generic.h"
#include "rewrite.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/runtime/control.h"
#include "camel/runtime/draft_writes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <type_traits>

namespace camel::passes::generic {
namespace {
using namespace camel::core;
using namespace camel::core::rtdata;
using namespace camel::runtime;

struct Arithmetic {
    type::TypeCode code;
    OperId add, sub, mul;
};

const auto &arithmetic() {
    static const auto ops = [] {
        auto &table = OperatorTable::instance();
        const auto row =
            [&](type::TypeCode code, const char *add, const char *sub, const char *mul) {
                return Arithmetic{code, table.intern(add), table.intern(sub), table.intern(mul)};
            };
        return std::array{
            row(type::TypeCode::Int32, ":op/add_i", ":op/sub_i", ":op/mul_i"),
            row(type::TypeCode::Int64, ":op/add_l", ":op/sub_l", ":op/mul_l"),
            row(type::TypeCode::Float32, ":op/add_f", ":op/sub_f", ":op/mul_f"),
            row(type::TypeCode::Float64, ":op/add_d", ":op/sub_d", ":op/mul_d")};
    }();
    return ops;
}

/// Do not peel GATEs: their control dependencies are part of evaluating the constant.
template <typename T>
std::optional<T>
constant(const GraphDraft &draft, gc_node_ref_t id, type::Type *ty, const WrittenSlots &written) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Data || h->dataType != ty || h->dataIndex >= 0 ||
        !draft.normInputsOf(id).empty() || !draft.withInputsOf(id).empty() ||
        !draft.ctrlInputsOf(id).empty() || written(id)) {
        return std::nullopt;
    }
    const auto index = static_cast<size_t>(-h->dataIndex);
    if (index >= draft.staticSlots().size()) {
        return std::nullopt;
    }
    return fromSlot<T>(draft.staticSlots()[index]);
}

/// Restrict elimination to a producer used only by this consumer. Shared products stay shared;
/// arm anchors keep their identity. This also bounds ordering transfer to immediate inputs.
bool exclusive(const GraphDraft &draft, gc_node_ref_t node, gc_node_ref_t consumer) {
    if (!isReplaceable(draft, node)) {
        return false;
    }
    for (auto users : {draft.normUsersOf(node), draft.withUsersOf(node), draft.ctrlUsersOf(node)}) {
        if (std::ranges::any_of(users, [&](auto user) { return user != consumer; })) {
            return false;
        }
    }
    return true;
}

template <typename T> struct Term {
    gc_node_ref_t base;
    T coefficient;
    gc_node_ref_t product = kInvalidNodeRef;
};

template <typename T>
Term<T> term(
    const GraphDraft &draft, gc_node_ref_t node, gc_node_ref_t consumer, const Arithmetic &ops,
    type::Type *ty, const WrittenSlots &written) {
    if (operIdOf(draft, node) == ops.mul && isPureOper(draft, node) && !written(node) &&
        exclusive(draft, node, consumer) && draft.withInputsOf(node).empty() &&
        draft.normInputsOf(node).size() == 2 &&
        draft.nodePermissions(node) == draft.nodePermissions(consumer)) {
        const auto inputs = draft.normInputsOf(node);
        for (size_t i = 0; i < 2; ++i) {
            if (auto c = constant<T>(draft, inputs[i], ty, written); c && !written(inputs[1 - i])) {
                return {inputs[1 - i], *c, node};
            }
        }
    }
    return {node, T{1}};
}

bool permitted(const GraphDraft &draft, gc_node_ref_t node, uint8_t required) {
    return (draft.nodePermissions(node) & required) == required;
}

/// Under round-to-nearest-even, doubling a rounded normal product commutes with rounding.
/// With an integer |c| >= 1, x*c cannot round in the subnormal range: every such exact product
/// is already an integer multiple of the smallest subnormal. Thus (x*c)+(x*c) == x*(2*c),
/// including overflow, infinities and signed zero. c and 2*c must be exactly representable.
/// NaN arithmetic remains NaN (ordinary arithmetic does not specify NaN payload selection).
template <typename T> bool exactDoubling(T c) {
    if constexpr (std::is_integral_v<T>) {
        return true;
    } else {
        return std::isfinite(c) && std::abs(c) >= T{1} && std::trunc(c) == c &&
               std::abs(c) <= std::ldexp(T{1}, std::numeric_limits<T>::digits - 1);
    }
}

/// Keep the consumer's identity, origin, permissions and CTRL edges, including output/arm
/// anchors. Every removed product's data input survives, and its immediate CTRL predecessors
/// move to the consumer. Never move a computation across a GATE or conditional boundary.
template <typename T>
void multiply(
    GraphDraft &draft, gc_node_ref_t node, const Arithmetic &ops, type::Type *ty,
    gc_node_ref_t base, T c, std::array<gc_node_ref_t, 2> products) {
    const auto value = draft.materializeStaticValue(toSlot<T>(c), ty);
    const std::array inputs{base, value};
    draft.setNormInputs(node, inputs);
    auto *body = reinterpret_cast<GCOperBody *>(draft.mutablePayloadOf(node).data());
    body->oper = ops.mul;
    body->op   = nullptr;
    if (products[1] == products[0]) {
        products[1] = kInvalidNodeRef;
    }
    for (auto product : products) {
        if (product == kInvalidNodeRef) {
            continue;
        }
        const std::vector<gc_node_ref_t> predecessors(
            draft.ctrlInputsOf(product).begin(),
            draft.ctrlInputsOf(product).end());
        draft.unlinkInput(DraftEdgeKind::Ctrl, node, product);
        for (auto pred : predecessors) {
            appendControlDependency(draft, pred, node);
        }
        draft.eraseNode(product);
    }
}

template <typename T>
bool simplify(
    GraphDraft &draft, gc_node_ref_t node, const Arithmetic &ops, bool safeFp,
    const WrittenSlots &written) {
    const auto inputs = draft.normInputsOf(node);
    if (inputs.size() != 2 || !draft.withInputsOf(node).empty()) {
        return false;
    }
    auto *ty = draft.header(node)->dataType;
    if (draft.header(inputs[0])->dataType != ty || draft.header(inputs[1])->dataType != ty ||
        written(inputs[0]) || written(inputs[1])) {
        return false;
    }
    const auto a = inputs[0], b = inputs[1];
    const auto op = operIdOf(draft, node);
    // Integer neutral elements preserve the evaluated operand. Floating-point identities
    // need separate NaN/zero rules and are deliberately not inferred from real arithmetic.
    if constexpr (std::is_integral_v<T>) {
        if (isReplaceable(draft, node)) {
            const auto ca             = constant<T>(draft, a, ty, written),
                       cb             = constant<T>(draft, b, ty, written);
            gc_node_ref_t replacement = kInvalidNodeRef;
            if ((op == ops.add && cb == T{0}) || (op == ops.sub && cb == T{0}) ||
                (op == ops.mul && cb == T{1})) {
                replacement = a;
            } else if ((op == ops.add && ca == T{0}) || (op == ops.mul && ca == T{1})) {
                replacement = b;
            }
            if (replacement != kInvalidNodeRef) {
                replaceNode(draft, node, replacement);
                return true;
            }
        }
    }
    if (op == ops.add) {
        const auto left  = term<T>(draft, a, node, ops, ty, written);
        const auto right = term<T>(draft, b, node, ops, ty, written);
        if (left.base != right.base ||
            (left.product == kInvalidNodeRef && right.product == kInvalidNodeRef && a != b)) {
            return false;
        }
        const bool exact =
            a == b && left.product != kInvalidNodeRef && exactDoubling(left.coefficient);
        const T c = left.coefficient + right.coefficient;
        if constexpr (std::is_floating_point_v<T>) {
            if (!std::isfinite(left.coefficient) || !std::isfinite(right.coefficient) ||
                !std::isfinite(c) || c == T{0}) {
                return false;
            }
            // Same-sign, nonzero coefficients preserve infinities and signed zero. Only the
            // rounding path changes, which the default algebra policy permits (§2.15).
            // Cancellation can change inf-inf and zero signs and needs those specific grants.
            const bool sameSign = left.coefficient != T{0} && right.coefficient != T{0} &&
                                  std::signbit(left.coefficient) == std::signbit(right.coefficient);
            const uint8_t required = sameSign ? 0 : sem::kNoInfs | sem::kNoSignedZeros;
            if (!exact && (safeFp || !permitted(draft, node, required))) {
                return false;
            }
        }
        multiply(draft, node, ops, ty, left.base, c, {left.product, right.product});
        return true;
    }
    if (op == ops.mul) {
        if constexpr (std::is_floating_point_v<T>) {
            // Reassociation may remove an overflowing intermediate product. The default
            // policy changes rounding, but does not also assume infinities away.
            if (safeFp || !permitted(draft, node, sem::kNoInfs)) {
                return false;
            }
        }
        for (auto [inner, outer] : {std::pair{a, b}, std::pair{b, a}}) {
            const auto c = constant<T>(draft, outer, ty, written);
            const auto t = term<T>(draft, inner, node, ops, ty, written);
            if (!c || t.product == kInvalidNodeRef) {
                continue;
            }
            const T combined = t.coefficient * *c;
            if constexpr (std::is_floating_point_v<T>) {
                if (!std::isfinite(t.coefficient) || !std::isfinite(*c) || t.coefficient == T{0} ||
                    *c == T{0} || !std::isfinite(combined) || combined == T{0}) {
                    continue;
                }
            }
            multiply(draft, node, ops, ty, t.base, combined, {t.product, kInvalidNodeRef});
            return true;
        }
    }
    return false;
}
} // namespace

size_t algebraDraft(GraphDraft &draft, bool safeFp) {
    // Fixed-depth matching, and at most four times the original node count in visits. The queue
    // revisits affected users without repeatedly scanning the graph or expanding shared DAGs.
    const size_t slots = draft.nodeSlotCount();
    const WrittenSlots written(draft);
    size_t budget = slots * 4, changed = 0;
    std::deque<gc_node_ref_t> work;
    std::vector<bool> queued(slots, true);
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        work.push_back(id);
    }
    while (!work.empty() && budget--) {
        const auto id = work.front();
        work.pop_front();
        queued[id]    = false;
        const auto *h = draft.header(id);
        if (!h || !h->dataType || !isPureOper(draft, id) || written(id)) {
            continue;
        }
        const auto op = operIdOf(draft, id);
        for (const auto &ops : arithmetic()) {
            if (h->dataType->code() != ops.code ||
                (op != ops.add && op != ops.sub && op != ops.mul)) {
                continue;
            }
            const std::vector<gc_node_ref_t> users(
                draft.normUsersOf(id).begin(),
                draft.normUsersOf(id).end());
            bool done = false;
            // Unsigned arithmetic implements the declared wrapping integer contract without
            // invoking host signed-overflow UB while calculating coefficients.
            switch (ops.code) {
            case type::TypeCode::Int32:
                done = simplify<uint32_t>(draft, id, ops, safeFp, written);
                break;
            case type::TypeCode::Int64:
                done = simplify<uint64_t>(draft, id, ops, safeFp, written);
                break;
            case type::TypeCode::Float32:
                done = simplify<float>(draft, id, ops, safeFp, written);
                break;
            case type::TypeCode::Float64:
                done = simplify<double>(draft, id, ops, safeFp, written);
                break;
            default:
                break;
            }
            if (done) {
                ++changed;
                for (auto user : users) {
                    if (user < slots && !queued[user]) {
                        queued[user] = true;
                        work.push_back(user);
                    }
                }
            }
            break;
        }
    }
    return changed;
}
} // namespace camel::passes::generic

camel::runtime::GCGraph *AlgebraPass::apply(camel::runtime::GCGraph *graph, std::ostream &) {
    return camel::passes::generic::rewriteReachableGraphs(
        context_,
        graph,
        "std::opt::algebra",
        [&](camel::runtime::GraphDraft &draft) {
            return camel::passes::generic::algebraDraft(draft, safeFp_);
        });
}
