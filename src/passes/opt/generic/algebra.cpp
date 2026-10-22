// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The residual of the old C++ algebraic pass: its integer neutral elements alone. The algebraic
 * laws (like terms, coefficients, exact doubling, tensor element-wise forms) are the rules
 * library's now (`rules::simplify`, registered as `std::opt::algebra`), so a law is written once,
 * in stdlib/rules.cml. `neutral_only` is a documented reference mode with no library equivalent;
 * it keeps the five neutral rewrites that the Camel-pass tests compare against. Nothing here runs
 * without that parameter.
 */

#include "generic.h"
#include "rewrite.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/base.h"
#include "camel/runtime/draft_writes.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace camel::passes::generic {
namespace {
using namespace camel::core;
using namespace camel::core::rtdata;
using namespace camel::runtime;

/// The operators of one family of laws over one element type, as the declarations select them.
struct Arithmetic {
    type::TypeCode element = type::TypeCode::Void;
    bool elementwise       = false;
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
                    .sub         = kNoOper,
                    .mul         = mul,
                    .addNeutral  = sum->neutralBits,
                    .subNeutral  = std::nullopt,
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

/// The integer constant of a scalar data node, exactly as the element type holds it. Aggregates
/// (tensors) have no neutral-element law here: the library has the tensor laws and this residual
/// is only the documented integer neutral mode.
template <typename T>
std::optional<T> constant(
    const GraphDraft &draft, gc_node_ref_t id, const Arithmetic &ops,
    const WrittenSlots &written) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Data || h->dataType == nullptr || h->dataIndex >= 0 ||
        h->dataType->code() != ops.element || !draft.normInputsOf(id).empty() ||
        !draft.withInputsOf(id).empty() || !draft.ctrlInputsOf(id).empty() || written(id)) {
        return std::nullopt;
    }
    const auto index = static_cast<size_t>(-h->dataIndex);
    if (index >= draft.staticSlots().size()) {
        return std::nullopt;
    }
    return fromSlot<T>(draft.staticSlots()[index]);
}

template <typename T>
bool simplify(
    GraphDraft &draft, gc_node_ref_t node, const Arithmetic &ops, const WrittenSlots &written) {
    if constexpr (!std::is_integral_v<T>) {
        return false;
    } else {
        const auto inputs = draft.normInputsOf(node);
        if (inputs.size() != 2 || !draft.withInputsOf(node).empty()) {
            return false;
        }
        auto *ty = draft.header(node)->dataType;
        if (draft.header(inputs[0])->dataType != ty || draft.header(inputs[1])->dataType != ty ||
            written(inputs[0]) || written(inputs[1]) || !isReplaceable(draft, node)) {
            return false;
        }
        const auto a = inputs[0], b = inputs[1];
        const auto op = operIdOf(draft, node);
        const auto ca = constant<T>(draft, a, ops, written),
                   cb = constant<T>(draft, b, ops, written);
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
        if (replacement == kInvalidNodeRef) {
            return false;
        }
        replaceNode(draft, node, replacement);
        return true;
    }
}

} // namespace

size_t algebraDraft(GraphDraft &draft, bool safeFp, bool neutralOnly) {
    (void)safeFp; // the neutral elements are integer-only; the library owns the float policy
    if (!neutralOnly) {
        return 0;
    }
    const size_t slots  = draft.nodeSlotCount();
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
            draft.normUsersOf(id).begin(), draft.normUsersOf(id).end());
        bool done = false;
        switch (ops->element) {
        case type::TypeCode::Int32:
            done = simplify<uint32_t>(draft, id, *ops, written);
            break;
        case type::TypeCode::Int64:
            done = simplify<uint64_t>(draft, id, *ops, written);
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

bool mayAlgebra(const camel::runtime::GCGraph &) {
    // The neutral elements are cheap to look for and the mode is a reference; the library pass
    // has its own screening. Keep every graph in reach.
    return true;
}
} // namespace camel::passes::generic

const camel::runtime::Program *
AlgebraPass::apply(const camel::runtime::Program *program, std::ostream &) {
    return camel::passes::generic::rewriteReachableGraphs(
        context_,
        program,
        "std::opt::algebra",
        [&](camel::runtime::GraphDraft &draft) {
            return camel::passes::generic::algebraDraft(draft, false, neutralOnly_);
        },
        nullptr,
        nullptr,
        camel::passes::generic::mayAlgebra);
}