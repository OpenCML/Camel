// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The `tensor.fusion` groups the tensor module runs.
 *
 * Two families. A division's VJP emits a chain of elementwise nodes and marks it (below); the
 * affine transform `x @ w + b` -- with `relu` over it, when that is what the program wrote -- is
 * the other: `tensor::fuse` marks the pair (or the triple, `passes/fuse.cpp#markGroups`) and the
 * kernels at the end of this file run it. Both families keep the program's operators and mark the
 * nodes: no operator is added, renamed or removed (design principles 9.6).
 *
 * The `tensor.fusion` groups a division's VJP emits.
 *
 * `divideVjp` builds `dlhs = divide(dy, rhs)`, `scaled = multiply(dlhs, y)`, `neg(scaled)` and
 * marks them with `tensor.fusion` (design principles 9.6). In a training step the head and the
 * middle are private to the chain -- only the next member reads them -- so a backend may compute
 * the tail's value in one pass and never store the two intermediates: three buffer reads, one
 * write, one launch, instead of three allocations and three passes.
 *
 * When the numerator is differentiated, `dlhs` is also the numerator's gradient and the gradient
 * struct reads it: the three-node chain is no longer private at its head [1]. The multiply and
 * the neg still are, so the same VJP marks them `tensor.fusion = "vjp_mul_neg"` instead, and a
 * backend runs that pair in one pass at the neg, reading the divide's already-stored value: two
 * passes instead of three. The two values never share a node, so each node names the largest
 * group its shape allows [2].
 *
 * The fused kernels do exactly the operators' work in the operators' order, per element:
 * `q = fl(dy/rhs)`, `s = fl(q*y)`, `r = -s`; and `s = fl(lhs*y)`, `r = -s`. The neg is a sign
 * flip, not a rounding, so the result is bit-identical to running the operators one by one, and
 * the groups are therefore licensed under `#strict` and under the safe mode (7.3, 7.4): no
 * roundings disappear and no permission is consumed. When the operands are not tensors of one
 * floating dtype and shape (a scalar operand, a broadcast the fast path does not tile, an integer
 * type, f16/bf16), the kernels call the operators' own elementwise kernels in the same order and
 * write the same tail value: still one group execution, still the operators' bits, no fast path.
 *
 * A backend that cannot keep that promise, or that has no kernel for the group, runs the nodes
 * one by one; the attribute grants nothing by itself (camel/execute/fusion_group.h).
 *
 * [1] A value a rule keeps (the numerator's gradient) is a reader of the divide's result, so the
 *     chain's head is not private and the backend declines the three-node group.
 * [2] One key holds one value per node (camel/core/node_attrs.h): the marking picks the group
 *     that can run. The pair is also marked when the numerator is active but its gradient is
 *     dropped; the backend then declines the bigger chain but still fuses the pair, one pass
 *     less than the operators.
 */

#include "fusion_group.h"

#include "attrs.h"
#include "camel/execute/fusion_group.h"
#include "kernels/elementwise.h"
#include "kernels/gemm.h"
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
    if (isTensorType(t)) {
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
    if (isTensorType(t)) {
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
    if (isTensorType(view.typeOf(headInputs[0])) &&
        isTensorType(view.typeOf(headInputs[1])) &&
        isTensorType(view.typeOf(midInputs[1]))) {
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

/// One pass over `lhs`, `y` and the tail's buffer: `out[i] = -(lhs[i] * y[i])`. The multiply is
/// one rounding, the neg one sign flip, in that order, so the bits are the two operators' bits.
template <typename T> void fusedPair(const T *lhs, const T *y, T *out, int64_t count) {
    k::parallelFor(
        count,
        k::grainForWork(2.0 * k::kNsPerFloat),
        [&](int64_t begin, int64_t end) {
            for (int64_t i = begin; i < end; ++i) {
                out[i] = -(lhs[i] * y[i]);
            }
        });
}

Array *runPair(const FusionGroupView &view, gc_node_ref_t middle) {
    const auto midInputs = view.inputsOf(middle);
    k::ScalarOperand lhsStore, yStore;
    const k::Operand lhs = operandOf(view, midInputs[0], lhsStore);
    const k::Operand y   = operandOf(view, midInputs[1], yStore);
    if (isTensorType(view.typeOf(midInputs[0])) &&
        isTensorType(view.typeOf(midInputs[1]))) {
        const auto *a = static_cast<const Array *>(lhs.data);
        const auto *b = static_cast<const Array *>(y.data);
        const type::TypeCode dtype = a->dtype();
        if ((dtype == type::TypeCode::Float32 || dtype == type::TypeCode::Float64) &&
            b->dtype() == dtype && a->sameShape(b)) {
            const int64_t count = static_cast<int64_t>(a->numel());
            Array *out          = createTensor(dtype, a->shapeSpan(), ops::resultAllocator(), false);
            if (dtype == type::TypeCode::Float32) {
                fusedPair<float>(a->dataAs<float>(), b->dataAs<float>(), out->dataAs<float>(), count);
            } else {
                fusedPair<double>(
                    a->dataAs<double>(), b->dataAs<double>(), out->dataAs<double>(), count);
            }
            return out;
        }
    }

    // The general path: the operators' own kernels in the operators' order, so the result is what
    // running the two nodes gives (no fast path, no different bits).
    Array *product = k::binary(k::BinaryOp::Mul, lhs, y, ops::resultAllocator());
    return k::unary(k::UnaryOp::Neg, product, ops::resultAllocator());
}

/// Recognizes `multiply -> neg` from the multiply: its first input is the divide whose value the
/// group reads as already stored, its only reader is the neg, and both carry the group's
/// attribute. The head is not a member: it has run (or will run) as the program says.
bool pairAtMiddle(const FusionGroupView &view, gc_node_ref_t middle, gc_node_ref_t &tail) {
    const ChainOperators &ops = chainOperators();
    const auto inputs         = view.inputsOf(middle);
    if (inputs.size() != 2) {
        return false;
    }
    const gc_node_ref_t head = inputs[0];
    if (view.operOf(head) != ops.divide || view.memberOf(head)) {
        return false;
    }
    if (!isTensorType(view.typeOf(head)) ||
        !isTensorType(view.typeOf(inputs[1]))) {
        return false;
    }
    const auto users = view.usersOf(middle);
    if (users.size() != 1) {
        return false;
    }
    tail = users.front();
    if (view.operOf(tail) != ops.neg || !view.memberOf(tail)) {
        return false;
    }
    const auto tailInputs = view.inputsOf(tail);
    if (tailInputs.size() != 1 || tailInputs[0] != middle) {
        return false;
    }
    const gc_node_ref_t members[] = {middle, tail};
    return view.isPrivateTo(middle, members);
}

FusionGroupAction multiplyNegKernel(
    const FusionGroupView &view,
    gc_node_ref_t node,
    std::span<const gc_node_ref_t> deferred,
    std::vector<gc_node_ref_t> &members,
    camel::execute::FusionGroupRef &group) {
    const ChainOperators &ops = chainOperators();
    if (view.operOf(node) == ops.multiply && view.memberOf(node)) {
        gc_node_ref_t tail = camel::runtime::kInvalidNodeRef;
        if (!pairAtMiddle(view, node, tail)) {
            return FusionGroupAction::None;
        }
        // The multiply is deferred to the neg, which runs both from the divide's stored value.
        members.push_back(node);
        group.tail = tail;
        return FusionGroupAction::Deferred;
    }
    if (view.operOf(node) != ops.neg || !view.memberOf(node)) {
        return FusionGroupAction::None;
    }
    const auto tailInputs = view.inputsOf(node);
    if (tailInputs.size() != 1) {
        return FusionGroupAction::None;
    }
    const gc_node_ref_t middle = tailInputs.front();
    if (view.operOf(middle) != ops.multiply || !view.memberOf(middle)) {
        return FusionGroupAction::None;
    }
    // The pair runs only when the multiply was deferred to this node. If it ran one by one (no
    // kernel at the middle, another reader there), the neg runs as the program says: that is the
    // fallback.
    if (!among(deferred, middle)) {
        return FusionGroupAction::None;
    }
    Array *result = runPair(view, middle);
    view.setValueOf(node, ops::wrap(view.typeOf(node), result));
    return FusionGroupAction::Ran;
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
    view.setValueOf(node, ops::wrap(view.typeOf(node), result));
    return FusionGroupAction::Ran;
}

/*
 * The affine transform: `x @ w + b`, and `relu(x @ w + b)` when the program wrote the activation
 * over it. `tensor::fuse` marks the pair `tensor.fusion = "linear"` and the triple
 * `tensor.fusion = "linear_relu"` (passes/fuse.cpp#markGroups); the group's nodes are still
 * tensor:matmul, tensor:add and tensor:relu.
 *
 * The promise is the same one the two VJP groups keep, and it is what makes the group licensed
 * under `#strict` and under the safe mode (7.3, 7.4): the fused run gives the bits the operators
 * give, one by one.
 *
 *   * The product half is `k::linear`, not a second hand-written GEMM, and that is the whole
 *     argument. The canonical algorithm is a chain of fused multiply-adds in ascending `k` from
 *     `+0`, and `alpha`/`beta` are applied after it (kernels/gemm.h, gemm_blocked.inl#finish):
 *     with `beta == 1` the last step is `product + c`, one rounding of `acc + c`. `k::linear`'s
 *     `f32` path writes the bias into the output first and runs that same `sgemm` with `beta = 1`
 *     (kernels/gemm.cpp#linear). So `linear(x, w, b)` is `matmul(x, w)`'s bits with `add`'s one
 *     rounding after them -- exactly the two nodes -- and the operand the group saves is the one
 *     buffer the product would have stored.
 *   * The activation half of the triple is the activation operator's own kernel
 *     (`k::unary(k::UnaryOp::Relu, ...)`), not a `> 0 ? x : 0` written here: `relu` is
 *     `maximum(x, +0)` (stdlib/tensor_definitions.cml), a NaN gives NaN and `-0` gives `+0`, and a
 *     second implementation would be a second definition.
 *   * When the product is not one this kernel can promise, it returns `None` and the nodes run one
 *     by one: that is a product the library GEMM actually takes (`tensor.kernel = "library"` and a
 *     size above the library's measured threshold, `kernels/gemm.h#libraryGemmRuns`), because the
 *     library chooses its own order and grouping of the accumulation -- or an operand shape the
 *     promise above does not cover. The attribute grants nothing, so declining is always allowed,
 *     and it is the reason the group never has to guess a bit. The decision is taken at the group's
 *     head, not at its tail: a head that answered `Deferred` has promised that the tail computes
 *     the product.
 */

/// The operators the pair and the triple are made of, interned once.
struct LinearOperators {
    OperId matmul = camel::core::kNoOper;
    OperId add    = camel::core::kNoOper;
    OperId relu   = camel::core::kNoOper;
};

const LinearOperators &linearOperators() {
    static const LinearOperators ops{
        intern("tensor:matmul"), intern("tensor:add"), intern("tensor:relu")};
    return ops;
}

/// One `x @ w + b` (optionally `relu`) group as its head sees it: the product, the add that reads
/// it, the bias the add reads on its other side, the activation when the group is the triple
/// (`kInvalidNodeRef` when it is the pair), and the product's two operands, as values.
///
/// The bias is a node the add reads, not the product: the plan may reach it *after* the product,
/// so its value is not promised at the head. It is read at the tail, where it is the add's own
/// operand and therefore has one.
struct LinearSite {
    gc_node_ref_t matmul = camel::runtime::kInvalidNodeRef;
    gc_node_ref_t add    = camel::runtime::kInvalidNodeRef;
    gc_node_ref_t bias   = camel::runtime::kInvalidNodeRef;
    gc_node_ref_t relu   = camel::runtime::kInvalidNodeRef;
    const Array *x       = nullptr;
    const Array *w       = nullptr;
};

/// Recognizes the group from its product; false means the nodes run one by one. Every condition
/// here is a promise this kernel does not make, not a shape it cannot check:
///
///   * a product the library GEMM actually runs (the node's `tensor.kernel` says "library" **and**
///     `kernels/gemm.h#libraryGemmRuns` says the size is one the library takes,
///     passes/fuse.cpp#kernelChoice): that GEMM chooses its own order and grouping of the
///     accumulation, so its result is not the canonical chain's and no kernel of ours reproduces
///     it. When the attribute says "library" but the size is below the library's threshold,
///     `sgemmLibrary` runs the canonical kernel, so `k::linear` still gives exactly the operator's
///     bits and the group runs;
///   * a product whose only reader is not a two-operand `add` reading it as its **first** operand
///     (the kernel adds the bias after the product's chain, and the graph's order is what it
///     copies);
///   * a weight that is not rank two, an input of rank below two (matmul promotes a vector operand,
///     and the promotion is a shape rule of its own), or two operands of different element types;
///   * a product or (in the triple) an add that something outside the group observes
///     (`isPrivateTo`): the group computes it at the tail and never stores it.
///
/// The decision is taken here, at the head, and not at the tail: a head that answers `Deferred` has
/// promised its tail will compute the product, so a tail that then declined would leave the
/// product's value unwritten. What the head cannot see -- the bias's value -- the tail may still
/// find unusable, and it then runs the product with the operator's own kernel rather than
/// declining (`runLinear`).
bool linearSite(const FusionGroupView &view, gc_node_ref_t matmul, LinearSite &site) {
    const LinearOperators &ops = linearOperators();
    if (view.operOf(matmul) != ops.matmul || !view.memberOf(matmul)) {
        return false;
    }
    const auto matmulInputs = view.inputsOf(matmul);
    if (matmulInputs.size() != 2) {
        return false;
    }
    const auto productUsers = view.usersOf(matmul);
    if (productUsers.size() != 1) {
        return false;
    }
    const gc_node_ref_t add = productUsers.front();
    if (view.operOf(add) != ops.add || !view.memberOf(add)) {
        return false;
    }
    const auto addInputs = view.inputsOf(add);
    if (addInputs.size() != 2 || addInputs[0] != matmul) {
        return false;
    }
    gc_node_ref_t relu = camel::runtime::kInvalidNodeRef;
    if (const auto addUsers = view.usersOf(add);
        addUsers.size() == 1 && view.memberOf(addUsers.front()) &&
        view.operOf(addUsers.front()) == ops.relu) {
        const auto reluInputs = view.inputsOf(addUsers.front());
        if (reluInputs.size() != 1 || reluInputs[0] != add) {
            return false;
        }
        relu = addUsers.front();
    }
    const gc_node_ref_t members[] = {matmul, add, relu};
    if (!view.isPrivateTo(matmul, members)) {
        return false;
    }
    if (relu != camel::runtime::kInvalidNodeRef && !view.isPrivateTo(add, members)) {
        return false;
    }
    // The product's operands: they are this node's inputs, so both have values here.
    if (!isTensorType(view.typeOf(matmulInputs[0])) ||
        !isTensorType(view.typeOf(matmulInputs[1])) || !isTensorType(view.typeOf(addInputs[1]))) {
        return false;
    }
    const auto *x = camel::core::rtdata::fromSlot<Array *>(view.valueOf(matmulInputs[0]));
    const auto *w = camel::core::rtdata::fromSlot<Array *>(view.valueOf(matmulInputs[1]));
    if (x == nullptr || w == nullptr) {
        return false;
    }
    if (x->rank() < 2 || w->rank() != 2 || w->dim(0) == 0) {
        return false;
    }
    if (x->dtype() != w->dtype() || x->dim(x->rank() - 1) != w->dim(0)) {
        return false;
    }
    // The product's licence: only a product the library GEMM actually runs is one whose bits this
    // kernel cannot promise. Below the library's threshold the operator runs the canonical kernel,
    // which is the one `k::linear` runs, so the group may keep the promise.
    if (usesLibraryGemm(view.graph().nodeAttrs(matmul))) {
        const int64_t K = w->dim(0);
        const int64_t N = w->dim(1);
        const int64_t M = static_cast<int64_t>(x->numel()) / K;
        if (k::libraryGemmRuns(M, N, K)) {
            return false;
        }
    }
    site = LinearSite{matmul, add, addInputs[1], relu, x, w};
    return true;
}

/// The group's value at its tail: `x @ w + b`, with the activation's own kernel over it when the
/// group is the triple.
///
/// The bias's value is read here, and when it is not the rank-1 tensor of the product's last extent
/// this kernel promised -- the head could not see it -- the three operators' own kernels run in
/// their own order instead (`k::matmul` with the node's licence, then the add's, then the
/// activation's). The bits are their bits either way, and the product's value is never left
/// unwritten.
Array *runLinear(const FusionGroupView &view, const LinearSite &site) {
    const auto *bias = camel::core::rtdata::fromSlot<Array *>(view.valueOf(site.bias));
    if (bias != nullptr && bias->rank() == 1 && bias->dtype() == site.w->dtype() &&
        bias->dim(0) == site.w->dim(1)) {
        Array *result = k::linear(site.x, site.w, bias, ops::resultAllocator());
        if (site.relu != camel::runtime::kInvalidNodeRef) {
            result = k::unary(k::UnaryOp::Relu, result, ops::resultAllocator());
        }
        return result;
    }
    Array *product = k::matmul(
        site.x, site.w, ops::resultAllocator(),
        usesLibraryGemm(view.graph().nodeAttrs(site.matmul)));
    if (bias == nullptr) {
        return site.relu != camel::runtime::kInvalidNodeRef
                   ? k::unary(k::UnaryOp::Relu, product, ops::resultAllocator())
                   : product;
    }
    Array *sum = k::binary(
        k::BinaryOp::Add, k::Operand::of(product), k::Operand::of(bias), ops::resultAllocator());
    return site.relu != camel::runtime::kInvalidNodeRef
               ? k::unary(k::UnaryOp::Relu, sum, ops::resultAllocator())
               : sum;
}

FusionGroupAction linearKernel(
    const FusionGroupView &view, gc_node_ref_t node, std::span<const gc_node_ref_t> deferred,
    std::vector<gc_node_ref_t> &members, camel::execute::FusionGroupRef &group) {
    const LinearOperators &ops = linearOperators();
    if (!view.memberOf(node)) {
        return FusionGroupAction::None;
    }
    // The product this node belongs to, read from whichever member the plan reached. The three
    // spellings are the three the marking writes; anything else is not this group.
    gc_node_ref_t matmul = camel::runtime::kInvalidNodeRef;
    if (view.operOf(node) == ops.matmul) {
        matmul = node;
    } else if (view.operOf(node) == ops.add) {
        const auto inputs = view.inputsOf(node);
        if (inputs.size() != 2) {
            return FusionGroupAction::None;
        }
        matmul = inputs[0];
    } else if (view.operOf(node) == ops.relu) {
        const auto reluInputs = view.inputsOf(node);
        if (reluInputs.size() != 1 || view.operOf(reluInputs[0]) != ops.add) {
            return FusionGroupAction::None;
        }
        const auto addInputs = view.inputsOf(reluInputs[0]);
        if (addInputs.size() != 2) {
            return FusionGroupAction::None;
        }
        matmul = addInputs[0];
    } else {
        return FusionGroupAction::None;
    }
    LinearSite site;
    if (matmul == camel::runtime::kInvalidNodeRef || !linearSite(view, matmul, site)) {
        return FusionGroupAction::None;
    }
    if (node != site.matmul && node != site.add && node != site.relu) {
        return FusionGroupAction::None;
    }
    const gc_node_ref_t tail =
        site.relu != camel::runtime::kInvalidNodeRef ? site.relu : site.add;
    if (node == site.matmul) {
        // The head: it is deferred to the tail, and so is the add when the group is the triple --
        // the tail is where every operand of the group has a value.
        members.push_back(node);
        if (site.relu != camel::runtime::kInvalidNodeRef) {
            members.push_back(site.add);
        }
        group.tail = tail;
        return FusionGroupAction::Deferred;
    }
    if (node != tail) {
        // The add of a triple is not a place the group runs or starts: it was deferred by the
        // head, so the plan never asks about it (the back end skips it).
        return FusionGroupAction::None;
    }
    // The group runs only when its earlier members were deferred to it. If the product ran one by
    // one (the library kernel, another reader, an operand this kernel declines), the tail runs as
    // the program says: that is the fallback.
    if (!among(deferred, site.matmul)) {
        return FusionGroupAction::None;
    }
    if (site.relu != camel::runtime::kInvalidNodeRef && !among(deferred, site.add)) {
        return FusionGroupAction::None;
    }
    Array *result = runLinear(view, site);
    // The nodes computed here, for the reports that say what the group stood for (profile.h).
    members.push_back(site.matmul);
    if (site.add != node) {
        members.push_back(site.add);
    }
    group.tail = node;
    view.setValueOf(node, ops::wrap(view.typeOf(node), result));
    return FusionGroupAction::Ran;
}

} // namespace

void registerDivideMulNegFusion() {
    camel::execute::registerFusionGroup(
        fusionAttrKey(), kDivideMulNegFusion, &divideMulNegKernel);
}

void registerMulNegFusion() {
    camel::execute::registerFusionGroup(fusionAttrKey(), kMulNegFusion, &multiplyNegKernel);
}

void registerLinearFusion() {
    camel::execute::registerFusionGroup(fusionAttrKey(), kLinearFusion, &linearKernel);
}

void registerLinearReluFusion() {
    camel::execute::registerFusionGroup(fusionAttrKey(), kLinearReluFusion, &linearKernel);
}

} // namespace camel::tensor