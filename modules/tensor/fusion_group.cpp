// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The `tensor.fusion` group a division's VJP emits.
 *
 * `divideVjp` builds `dlhs = divide(dy, rhs)`, `scaled = multiply(dlhs, y)`, `neg(scaled)` and
 * marks the three nodes with `tensor.fusion = "vjp_div_neg"` (design principles 9.6). In a
 * training step the head and the middle are private to the chain -- only the next member reads
 * them -- so a backend may compute the tail's value in one pass and never store the two
 * intermediates: three buffer reads, one write, one launch, instead of three allocations and
 * three passes.
 *
 * The fused kernel does exactly the operators' work in the operators' order, per element:
 * `q = fl(dy/rhs)`, `s = fl(q*y)`, `r = -s`. The neg is a sign flip, not a rounding, so the
 * result is bit-identical to running the three nodes, and the group is therefore licensed under
 * `#strict` and under the safe mode (7.3, 7.4): no roundings disappear and no permission is
 * consumed. When the operands are not three tensors of one floating dtype and shape (a scalar
 * denominator, a broadcast, an integer type, f16/bf16), the kernel calls the operators' own
 * elementwise kernels in the same order and writes the same tail value: still one group
 * execution, still the operators' bits, no fast path.
 *
 * A backend that cannot keep that promise, or that has no kernel for the group, runs the nodes
 * one by one; the attribute grants nothing by itself (camel/execute/fusion_group.h).
 */

#include "fusion_group.h"

#include "attrs.h"
#include "camel/execute/fusion_group.h"
#include "kernels/elementwise.h"
#include "kernels/parallel.h"
#include "ops/support.h"
#include "tensor.h"
#include "type.h"

#include <algorithm>
#include <span>

namespace camel::tensor {

namespace {

namespace k    = camel::tensor::kernels;
namespace attr = camel::core::attr;
using camel::core::OperId;
using camel::execute::FusionGroupAction;
using camel::execute::FusionGroupView;
using camel::runtime::gc_node_ref_t;

OperId intern(std::string_view uri) {
    return camel::core::OperatorTable::instance().intern(uri);
}

struct ChainOperators {
    OperId divide   = camel::core::kNoOper;
    OperId multiply = camel::core::kNoOper;
    OperId neg      = camel::core::kNoOper;
};

/// The three operators of the group, interned once (the VJP rule emits them by this URI).
const ChainOperators &chainOperators() {
    static const ChainOperators ops{intern("tensor:divide"), intern("tensor:multiply"), intern("tensor:neg")};
    return ops;
}

bool numericOperand(const type::Type *t) {
    if (t == nullptr) {
        return false;
    }
    if (asTensorType(t) != nullptr) {
        return true;
    }
    switch (t->code()) {
    case type::TypeCode::Float32:
    case type::TypeCode::Float64:
    case type::TypeCode::Int32:
    case type::TypeCode::Int64:
        return true;
    default:
        return false;
    }
}

bool among(std::span<const gc_node_ref_t> nodes, gc_node_ref_t node) {
    return std::find(nodes.begin(), nodes.end(), node) != nodes.end();
}

/// Recognizes `divide -> multiply -> neg` from its head: each link has exactly one reader, the
/// next member, and nothing outside the group reads the head or the middle.
bool chainAtHead(
    const FusionGroupView &view, gc_node_ref_t head, gc_node_ref_t &middle, gc_node_ref_t &tail) {
    const ChainOperators &ops = chainOperators();
    if (view.operOf(head) != ops.divide) {
        return false;
    }
    const auto headInputs = view.inputsOf(head);
    if (headInputs.size() != 2) {
        return false;
    }
    const auto headUsers = view.usersOf(head);
    if (headUsers.size() != 1) {
        return false;
    }
    middle = headUsers.front();
    if (view.operOf(middle) != ops.multiply || !view.memberOf(middle)) {
        return false;
    }
    const auto midInputs = view.inputsOf(middle);
    if (midInputs.size() != 2 || midInputs[0] != head) {
        return false;
    }
    const auto midUsers = view.usersOf(middle);
    if (midUsers.size() != 1) {
        return false;
    }
    tail = midUsers.front();
    if (view.operOf(tail) != ops.neg || !view.memberOf(tail)) {
        return false;
    }
    const auto tailInputs = view.inputsOf(tail);
    if (tailInputs.size() != 1 || tailInputs[0] != middle) {
        return false;
    }
    const gc_node_ref_t members[] = {head, middle, tail};
    if (!view.isPrivateTo(head, members) || !view.isPrivateTo(middle, members)) {
        return false;
    }
    // Every operand the group reads is a number or a tensor of numbers. Anything else is not a
    // chain this kernel knows; the nodes run one by one (the attribute stays, the backend
    // declines).
    return numericOperand(view.typeOf(headInputs[0])) && numericOperand(view.typeOf(headInputs[1])) &&
           numericOperand(view.typeOf(midInputs[1]));
}

k::Operand operandOf(const FusionGroupView &view, gc_node_ref_t node, k::ScalarOperand &store) {
    type::Type *t = view.typeOf(node);
    if (asTensorType(t) != nullptr) {
        auto *array = camel::core::rtdata::fromSlot<Array *>(view.valueOf(node));
        if (array == nullptr) {
            ops::reportKernelError("tensor.fusion vjp_div_neg", "a tensor operand is null");
        }
        return k::Operand::of(array);
    }
    store = k::ScalarOperand::fromSlot(t->code(), view.valueOf(node));
    return store.view();
}

/// One pass over `dy`, `rhs`, `y` and the tail's buffer. `kBroadcast` says the denominator is one
/// right-aligned block repeated (`rhsCount` elements, out's last dimension), which is how a
/// per-channel scale divides a batch; otherwise it is a tensor of the output's shape.
template <typename T, bool kBroadcast>
void fusedChain(const T *dy, const T *rhs, const T *y, T *out, int64_t count, int64_t rhsCount) {
    k::parallelFor(
        count,
        k::grainForWork(2.0 * k::kNsPerFloat),
        [&](int64_t begin, int64_t end) {
            // The repeated block's position advances with the element: one integer division per
            // chunk instead of one per element.
            int64_t divisorIndex = 0;
            if constexpr (kBroadcast) {
                divisorIndex = begin % rhsCount;
            }
            for (int64_t i = begin; i < end; ++i) {
                T divisor = rhs[i];
                if constexpr (kBroadcast) {
                    divisor = rhs[divisorIndex];
                    if (++divisorIndex == rhsCount) {
                        divisorIndex = 0;
                    }
                }
                const T quotient = dy[i] / divisor;
                const T scaled   = quotient * y[i];
                out[i]           = -scaled;
            }
        });
}

/// How `rhs` maps onto the output's elements, or -1 when it is a broadcast this kernel does not
/// tile (`k % rhsCount`): a full tensor, a one-element tensor, or a vector along the last
/// dimension.
int64_t rhsExtent(const Array &rhs, const Array &out) {
    if (rhs.sameShape(&out)) {
        return static_cast<int64_t>(out.numel());
    }
    if (rhs.numel() == 1) {
        return 1;
    }
    const auto shape = rhs.shapeSpan();
    for (size_t d = 0; d + 1 < shape.size(); ++d) {
        if (shape[d] != 1) {
            return -1;
        }
    }
    const auto outShape = out.shapeSpan();
    if (!shape.empty() && !outShape.empty() && shape.back() == outShape.back()) {
        return shape.back();
    }
    return -1;
}

Array *runChain(
    const FusionGroupView &view, gc_node_ref_t head, gc_node_ref_t middle, gc_node_ref_t tail) {
    const auto headInputs = view.inputsOf(head);
    const auto midInputs  = view.inputsOf(middle);
    k::ScalarOperand dyStore, rhsStore, yStore;
    const k::Operand dy  = operandOf(view, headInputs[0], dyStore);
    const k::Operand rhs = operandOf(view, headInputs[1], rhsStore);
    const k::Operand y   = operandOf(view, midInputs[1], yStore);

    // One pass over the tail's buffer: the operators' three roundings, in the operators' order,
    // with one buffer written and the two intermediates never stored. The denominator may be one
    // right-aligned block repeated (a per-channel scale), which this kernel tiles.
    if (asTensorType(view.typeOf(headInputs[0])) != nullptr &&
        asTensorType(view.typeOf(headInputs[1])) != nullptr &&
        asTensorType(view.typeOf(midInputs[1])) != nullptr) {
        const auto *a = static_cast<const Array *>(dy.data);
        const auto *b = static_cast<const Array *>(rhs.data);
        const auto *c = static_cast<const Array *>(y.data);
        const type::TypeCode dtype = a->dtype();
        if ((dtype == type::TypeCode::Float32 || dtype == type::TypeCode::Float64) &&
            b->dtype() == dtype && c->dtype() == dtype && a->sameShape(c)) {
            const int64_t count    = static_cast<int64_t>(a->numel());
            const int64_t rhsCount = rhsExtent(*b, *a);
            if (rhsCount > 0) {
                Array *out = createTensor(dtype, a->shapeSpan(), ops::resultAllocator(), false);
                const bool broadcast = rhsCount != count;
                if (dtype == type::TypeCode::Float32) {
                    if (broadcast) {
                        fusedChain<float, true>(a->dataAs<float>(), b->dataAs<float>(),
                                                c->dataAs<float>(), out->dataAs<float>(), count,
                                                rhsCount);
                    } else {
                        fusedChain<float, false>(a->dataAs<float>(), b->dataAs<float>(),
                                                 c->dataAs<float>(), out->dataAs<float>(), count,
                                                 count);
                    }
                } else {
                    if (broadcast) {
                        fusedChain<double, true>(a->dataAs<double>(), b->dataAs<double>(),
                                                 c->dataAs<double>(), out->dataAs<double>(), count,
                                                 rhsCount);
                    } else {
                        fusedChain<double, false>(a->dataAs<double>(), b->dataAs<double>(),
                                                  c->dataAs<double>(), out->dataAs<double>(),
                                                  count, count);
                    }
                }
                return out;
            }
        }
    }

    // The general broadcast/scalar path: the operators' own kernels in the operators' order, so
    // the result is what running the three nodes gives (no fast path, no different bits).
    Array *quotient = k::binary(k::BinaryOp::Div, dy, rhs, ops::resultAllocator());
    Array *scaled = k::binary(k::BinaryOp::Mul, k::Operand::of(quotient), y, ops::resultAllocator());
    return k::unary(k::UnaryOp::Neg, scaled, ops::resultAllocator());
}

FusionGroupAction divideMulNegKernel(
    const FusionGroupView &view,
    gc_node_ref_t node,
    std::span<const gc_node_ref_t> deferred,
    std::vector<gc_node_ref_t> &members,
    camel::execute::FusionGroupRef &group) {
    const ChainOperators &ops = chainOperators();
    if (view.operOf(node) == ops.divide && view.memberOf(node)) {
        gc_node_ref_t middle = camel::runtime::kInvalidNodeRef;
        gc_node_ref_t tail   = camel::runtime::kInvalidNodeRef;
        if (!chainAtHead(view, node, middle, tail)) {
            return FusionGroupAction::None;
        }
        // The head and the middle are deferred to the tail, where every operand of the chain has a
        // value; the head is skipped now, the middle when the plan reaches it.
        members.push_back(node);
        members.push_back(middle);
        group.tail = tail;
        return FusionGroupAction::Deferred;
    }
    if (view.operOf(node) != ops.neg || !view.memberOf(node)) {
        return FusionGroupAction::None;
    }
    // The tail runs the chain when the head and the middle were deferred to it. If they ran one
    // by one (another reader, a scalar operand, no kernel at the head), the tail runs as the
    // program says: that is the fallback.
    const auto tailInputs = view.inputsOf(node);
    if (tailInputs.size() != 1) {
        return FusionGroupAction::None;
    }
    const gc_node_ref_t middle = tailInputs.front();
    if (view.operOf(middle) != ops.multiply || !view.memberOf(middle)) {
        return FusionGroupAction::None;
    }
    const auto midInputs = view.inputsOf(middle);
    if (midInputs.size() != 2) {
        return FusionGroupAction::None;
    }
    const gc_node_ref_t head = midInputs[0];
    if (view.operOf(head) != ops.divide || !view.memberOf(head)) {
        return FusionGroupAction::None;
    }
    if (!among(deferred, head) || !among(deferred, middle)) {
        return FusionGroupAction::None;
    }
    // The nodes computed here, for the reports that say what the group stood for (profile.h).
    members.push_back(head);
    members.push_back(middle);
    group.tail = node;
    Array *result = runChain(view, head, middle, node);
    view.setValueOf(node, ops::wrap(result));
    return FusionGroupAction::Ran;
}

} // namespace

void registerDivideMulNegFusion() {
    camel::execute::registerFusionGroup(
        fusionAttrKey(), kDivideMulNegFusion, &divideMulNegKernel);
}

} // namespace camel::tensor