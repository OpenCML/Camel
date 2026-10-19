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

/*
 * Local algebra on binary operators, independent of autodiff. Matching never expands an
 * expression tree. The operators it rewrites are the ones that declare the laws it uses
 * (OperatorSemantics::binary: a commutative, associative sum, a product that distributes over
 * it, a subtraction of it): no operator is named here, so the scalar operators and the
 * elementwise operators of a module (tensors) are one case. The relaxations it applies are a
 * NumericPolicy's (design principles 2.15): the default policy allows the grouping to change
 * and `safe_fp=true` is the strict one.
 */
#include "generic.h"
#include "rewrite.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/runtime/control.h"
#include "camel/runtime/draft_writes.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
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

/// The operators of one family of laws over one element type, as the declarations select them.
struct Arithmetic {
    type::TypeCode element = type::TypeCode::Void;
    /// Aggregate operands (tensors): a coefficient is a scalar constant, the other operand has
    /// the type of the result.
    bool elementwise = false;
    std::function<std::optional<type::TypeCode>(const type::Type *)> elementOf;
    OperId add = kNoOper, sub = kNoOper, mul = kNoOper;
    std::optional<uint64_t> addNeutral, subNeutral, mulNeutral;
};

/// The families the declarations make, rebuilt when a declaration is added (a module loaded).
class Families {
  public:
    static std::shared_ptr<const Families> current() {
        static std::mutex mutex;
        static std::shared_ptr<const Families> cached;
        static uint64_t epoch = ~uint64_t{0};
        std::lock_guard lock(mutex);
        const uint64_t now = OperatorTable::instance().declarationEpoch();
        if (!cached || epoch != now) {
            cached = std::shared_ptr<const Families>(new Families());
            epoch  = now;
        }
        return cached;
    }

    /// The family `op` belongs to for a value of type `ty` (the node's result), or nullptr.
    const Arithmetic *match(OperId op, const type::Type *ty) const {
        const auto it = byOper_.find(op);
        if (it == byOper_.end()) {
            return nullptr;
        }
        for (const size_t index : it->second) {
            const Arithmetic &family = families_[index];
            const auto element =
                family.elementOf ? family.elementOf(ty) : std::optional(ty->code());
            if (element && *element == family.element) {
                return &family;
            }
        }
        return nullptr;
    }

  private:
    Families() {
        const auto &table = OperatorTable::instance();
        const auto lawOf  = [](const sem::BinaryAlgebra &algebra, type::TypeCode element) {
            for (const sem::BinaryLaw &law : algebra.laws) {
                if (law.element == element) {
                    return &law;
                }
            }
            return static_cast<const sem::BinaryLaw *>(nullptr);
        };
        // A product is an operator whose law says it distributes over another (the sum); the
        // subtraction is the one that says it is a subtraction of that sum.
        for (OperId mul = 1; mul < table.size(); ++mul) {
            const sem::OperatorSemantics *declared = table.semantics(mul);
            if (declared == nullptr || !declared->binary) {
                continue;
            }
            for (const sem::BinaryLaw &product : declared->binary->laws) {
                if (product.distributesOver.empty() || !product.commutative) {
                    continue;
                }
                const auto add = table.lookup(product.distributesOver);
                if (!add || *add == mul) {
                    continue;
                }
                const sem::OperatorSemantics *sumDeclared = table.semantics(*add);
                if (sumDeclared == nullptr || !sumDeclared->binary) {
                    continue;
                }
                const sem::BinaryLaw *sum = lawOf(*sumDeclared->binary, product.element);
                if (sum == nullptr || !sum->commutative) {
                    continue;
                }
                Arithmetic family{
                    .element     = product.element,
                    .elementwise = declared->binary->elementwise,
                    .elementOf   = declared->binary->elementOf,
                    .add         = *add,
                    .mul         = mul,
                    .addNeutral  = sum->neutralBits,
                    .mulNeutral  = product.neutralBits,
                };
                for (OperId sub = 1; sub < table.size(); ++sub) {
                    const sem::OperatorSemantics *subDeclared = table.semantics(sub);
                    if (subDeclared == nullptr || !subDeclared->binary) {
                        continue;
                    }
                    if (const sem::BinaryLaw *law = lawOf(*subDeclared->binary, product.element);
                        law && law->subtractionOf == table.uri(*add)) {
                        family.sub        = sub;
                        family.subNeutral = law->neutralBits;
                        break;
                    }
                }
                const size_t index = families_.size();
                families_.push_back(std::move(family));
                for (const OperId op : {*add, mul, families_.back().sub}) {
                    if (op != kNoOper) {
                        byOper_[op].push_back(index);
                    }
                }
            }
        }
    }

    std::vector<Arithmetic> families_;
    std::unordered_map<OperId, std::vector<size_t>> byOper_;
};

type::Type *scalarType(type::TypeCode code) {
    switch (code) {
    case type::TypeCode::Int32:
        return type::Type::Int32();
    case type::TypeCode::Int64:
        return type::Type::Int64();
    case type::TypeCode::Float32:
        return type::Type::Float32();
    case type::TypeCode::Float64:
        return type::Type::Float64();
    default:
        return nullptr;
    }
}

/// `value` in T when T holds it exactly (floating T only: an integer coefficient is written in
/// the element type already).
template <typename T, typename V> std::optional<T> exactIn(V value) {
    if constexpr (std::is_floating_point_v<T>) {
        const T converted = static_cast<T>(value);
        if (static_cast<V>(converted) == value) {
            return converted;
        }
    }
    return std::nullopt;
}

/// The constant of a scalar node: of the element type, or (for aggregates, whose coefficients
/// are written as plain numbers) a number of another type that the element type holds exactly.
/// Do not peel GATEs: their control dependencies are part of evaluating the constant.
template <typename T>
std::optional<T> constant(
    const GraphDraft &draft, gc_node_ref_t id, const Arithmetic &ops, const WrittenSlots &written) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Data || h->dataType == nullptr || h->dataIndex >= 0 ||
        !draft.normInputsOf(id).empty() || !draft.withInputsOf(id).empty() ||
        !draft.ctrlInputsOf(id).empty() || written(id)) {
        return std::nullopt;
    }
    const auto index = static_cast<size_t>(-h->dataIndex);
    if (index >= draft.staticSlots().size()) {
        return std::nullopt;
    }
    const slot_t slot = draft.staticSlots()[index];
    const auto code   = h->dataType->code();
    if (code == ops.element) {
        return fromSlot<T>(slot);
    }
    if (!ops.elementwise) {
        return std::nullopt;
    }
    switch (code) {
    case type::TypeCode::Int32:
        return exactIn<T>(fromSlot<int32_t>(slot));
    case type::TypeCode::Int64:
        return exactIn<T>(fromSlot<int64_t>(slot));
    case type::TypeCode::Float32:
        return exactIn<T>(fromSlot<float>(slot));
    case type::TypeCode::Float64:
        return exactIn<T>(fromSlot<double>(slot));
    default:
        return std::nullopt;
    }
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
        draft.header(node)->dataType == ty && exclusive(draft, node, consumer) &&
        draft.withInputsOf(node).empty() && draft.normInputsOf(node).size() == 2 &&
        draft.nodePermissions(node) == draft.nodePermissions(consumer)) {
        const auto inputs = draft.normInputsOf(node);
        for (size_t i = 0; i < 2; ++i) {
            const auto *other = draft.header(inputs[1 - i]);
            // The factor that is not the coefficient has the type of the product: an aggregate
            // is not broadcast against a scalar here, only scaled by one.
            if (auto c = constant<T>(draft, inputs[i], ops, written);
                c && !written(inputs[1 - i]) && other && other->dataType == ty) {
                return {inputs[1 - i], *c, node};
            }
        }
    }
    return {node, T{1}};
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
    GraphDraft &draft, gc_node_ref_t node, const Arithmetic &ops, gc_node_ref_t base, T c,
    std::array<gc_node_ref_t, 2> products) {
    const auto value = draft.materializeStaticValue(toSlot<T>(c), scalarType(ops.element));
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
    GraphDraft &draft, gc_node_ref_t node, const Arithmetic &ops, const sem::NumericPolicy &policy,
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
            const auto ca = constant<T>(draft, a, ops, written),
                       cb = constant<T>(draft, b, ops, written);
            // The neutral elements are the declarations': x + 0, x - 0, x * 1 (the operand
            // that stays is the other one; a subtraction is neutral on its right only).
            const auto isNeutral = [](const std::optional<T> &c, const std::optional<uint64_t> &n) {
                return c && n && *c == static_cast<T>(*n);
            };
            gc_node_ref_t replacement = kInvalidNodeRef;
            if ((op == ops.add && isNeutral(cb, ops.addNeutral)) ||
                (op == ops.sub && isNeutral(cb, ops.subNeutral)) ||
                (op == ops.mul && isNeutral(cb, ops.mulNeutral))) {
                replacement = a;
            } else if (
                (op == ops.add && isNeutral(ca, ops.addNeutral)) ||
                (op == ops.mul && isNeutral(ca, ops.mulNeutral))) {
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
            const uint8_t required =
                sem::kReassociate | (sameSign ? 0 : sem::kNoInfs | sem::kNoSignedZeros);
            if (!exact && !policy.permits(draft.nodePermissions(node), required)) {
                return false;
            }
        }
        multiply(draft, node, ops, left.base, c, {left.product, right.product});
        return true;
    }
    if (op == ops.mul) {
        if constexpr (std::is_floating_point_v<T>) {
            // Reassociation may remove an overflowing intermediate product. The default
            // policy changes rounding, but does not also assume infinities away.
            if (!policy.permits(draft.nodePermissions(node), sem::kReassociate | sem::kNoInfs)) {
                return false;
            }
        }
        for (auto [inner, outer] : {std::pair{a, b}, std::pair{b, a}}) {
            const auto c = constant<T>(draft, outer, ops, written);
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
            multiply(draft, node, ops, t.base, combined, {t.product, kInvalidNodeRef});
            return true;
        }
    }
    return false;
}
} // namespace

size_t algebraDraft(GraphDraft &draft, const sem::NumericPolicy &policy) {
    // Fixed-depth matching, and at most four times the original node count in visits. The queue
    // revisits affected users without repeatedly scanning the graph or expanding shared DAGs.
    const size_t slots = draft.nodeSlotCount();
    const auto families = Families::current();
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
        const Arithmetic *ops = families->match(operIdOf(draft, id), h->dataType);
        if (ops == nullptr) {
            continue;
        }
        const std::vector<gc_node_ref_t> users(
            draft.normUsersOf(id).begin(),
            draft.normUsersOf(id).end());
        bool done = false;
        // Unsigned arithmetic implements the declared wrapping integer contract without
        // invoking host signed-overflow UB while calculating coefficients.
        switch (ops->element) {
        case type::TypeCode::Int32:
            done = simplify<uint32_t>(draft, id, *ops, policy, written);
            break;
        case type::TypeCode::Int64:
            done = simplify<uint64_t>(draft, id, *ops, policy, written);
            break;
        case type::TypeCode::Float32:
            done = simplify<float>(draft, id, *ops, policy, written);
            break;
        case type::TypeCode::Float64:
            done = simplify<double>(draft, id, *ops, policy, written);
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
    }
    return changed;
}

size_t algebraDraft(GraphDraft &draft, bool safeFp) {
    return algebraDraft(
        draft,
        safeFp ? sem::NumericPolicy::strict() : sem::NumericPolicy::relaxed());
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
