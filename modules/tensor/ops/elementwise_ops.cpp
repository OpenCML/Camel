// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Elementwise operators: broadcasting arithmetic and comparison (tensor with
 * tensor or scalar), unary maps, `where`, and the element-wise type conversions.
 *
 * Operators are generated from small tables so that one kernel wrapper and
 * one inference function serve each family. Binary operators require at
 * least one tensor operand; scalar-with-scalar arithmetic stays with the
 * builtin scalar operators.
 */

#include "../attrs.h"
#include "../kernels/elementwise.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

constexpr std::string_view binaryName(k::BinaryOp op) {
    constexpr std::string_view names[] =
        {"add", "subtract", "multiply", "divide", "pow", "maximum", "minimum", "mod"};
    return names[static_cast<size_t>(op)];
}

constexpr std::string_view compareName(k::CompareOp op) {
    constexpr std::string_view names[] = {"lt", "le", "gt", "ge", "strict_eq", "strict_neq"};
    return names[static_cast<size_t>(op)];
}

constexpr std::string_view unaryName(k::UnaryOp op) {
    constexpr std::string_view names[] =
        {"neg", "abs", "exp", "log", "sqrt", "rsqrt", "sigmoid", "tanh", "relu", "gelu", "erf"};
    return names[static_cast<size_t>(op)];
}

template <k::BinaryOp Op> slot_t binaryKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(binaryName(Op), [&] {
        k::ScalarOperand lhsScalar{}, rhsScalar{};
        const k::Operand lhs = operandArg(norm, 0, lhsScalar);
        const k::Operand rhs = operandArg(norm, 1, rhsScalar);
        return wrap(norm.resultType(), k::binary(Op, lhs, rhs, resultAllocator()));
    });
}

template <k::CompareOp Op> slot_t compareKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(compareName(Op), [&] {
        k::ScalarOperand lhsScalar{}, rhsScalar{};
        const k::Operand lhs = operandArg(norm, 0, lhsScalar);
        const k::Operand rhs = operandArg(norm, 1, rhsScalar);
        return wrap(norm.resultType(), k::compare(Op, lhs, rhs, resultAllocator()));
    });
}

template <k::UnaryOp Op> slot_t unaryKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(unaryName(Op), [&] {
        return wrap(norm.resultType(), k::unary(Op, tensorArg(norm, 0), resultAllocator()));
    });
}

bool isTensorArg(const InferContext &ctx, size_t index) {
    return isTensorType(ctx.type(index));
}

/// Whether a scalar argument is a constant whose value inference can see: a literal written in
/// place, or a local variable the compiler folded to one. It is not a scalar whose type is its
/// own - a parameter (`a: float`), a conversion (`float(i)`), the result of an arithmetic on
/// them - which is the scalar design principles 5.1 refuses beside a tensor.
bool isConstantScalar(const InferContext &ctx, size_t index) {
    return ctx.constNumber(index).has_value() || ctx.constInt(index).has_value() ||
           ctx.constBool(index).has_value();
}

/// The type an operator with a tensor and a scalar computes in when the scalar does not have the
/// element type: the element type itself for a float tensor, and f32 for an integer or bool one
/// (there is no bool arithmetic, and a float meeting an integer tensor has always computed in
/// f32). Reported as the conversion to write.
TypeCode computedElementType(TypeCode element, TypeCode scalar) {
    if (isFloatingTensorType(element)) {
        return element;
    }
    return isFloatingTensorType(scalar) ? TypeCode::Float32 : element;
}

/// The element type a binary operator's two operands are computed in: one element type, because a
/// tensor operator computes in its element type and there is no implicit conversion between
/// numeric types (design principles 5.1). A scalar operand has the tensor's element type, and a
/// scalar of another type is refused, not converted: a value whose type is its own (`a: float`) is
/// an error, the same rule as `f32 * f64` between two scalars, and the report names the conversion
/// to write. A literal takes the element type before the operator is resolved - `x * 2.0` and
/// `add(x, 2.0)` alike - so what is left at a call the program wrote is a value. A constant that
/// still arrives with a type of its own is one the literal rule could not reach: a literal the
/// element type holds no form of (a `bool` tensor's arithmetic is f32) or one written inside a
/// generic function, whose element type is open where the literal is typed and fixed only at
/// instantiation. The operator then keeps the answer it always gave, and the kernel reads the
/// constant in the element type - the narrowing is a defect the literal rule does not yet cover,
/// not a value to report. A pass that reads the graph again infers without `sourceLevel` and keeps
/// that tolerance for every operand alike, because the program was compiled under it (a generic
/// body is inferred with open element types).
/// Two tensor operands state the element type they compute in, and the pair is refused when the
/// two differ - `f32[..] * f64[..]` and `f32[..] * bool[..]` alike match no arm whether the
/// extents are known or open, and the report names the conversion to write
/// (refuseMixedElementTypes). An integer or a bool is an element type like any other: a number
/// becoming one is a conversion the program writes, so a mask is written `where(mask, x, 0.0)` or
/// `f32(mask)`.
///
/// The two operands the rule reads are named by index: an operator's operands are not always its
/// first two, and `where` reads its condition first and the two values it chooses between after
/// it.
std::optional<TypeCode>
operandType(const InferContext &ctx, size_t lhsIndex, size_t rhsIndex) {
    const TensorFacts lhs = ctx.facts(lhsIndex), rhs = ctx.facts(rhsIndex);
    if (isTensorArg(ctx, lhsIndex) == isTensorArg(ctx, rhsIndex)) {
        refuseMixedElementTypes(lhs.dtype, rhs.dtype);
        return commonElementType(lhs.dtype, rhs.dtype);
    }
    const bool tensorFirst = isTensorArg(ctx, lhsIndex);
    const size_t scalarIndex = tensorFirst ? rhsIndex : lhsIndex;
    const TensorFacts &tensor = tensorFirst ? lhs : rhs;
    Type *scalar = ctx.type(scalarIndex);
    if (!tensor.dtype || scalar == nullptr) {
        return std::nullopt;
    }
    if (scalar->code() == *tensor.dtype) {
        return tensor.dtype;
    }
    const TypeCode computed = computedElementType(*tensor.dtype, scalar->code());
    if (!ctx.sourceLevel() || isConstantScalar(ctx, scalarIndex)) {
        return computed;
    }
    // A refusal names the writing that fits either intent: the operand is converted to the
    // element type, and a scalar the program meant to read out of the array is reduced to one. The
    // second spelling is what a `for` round of an open-rank array needs -- the round is the array
    // itself and not its element, so the conversion of the scalar operand alone would leave the
    // program the author meant to write without a way to say it.
    throw std::invalid_argument(
        "the scalar operand has type '" + scalar->toString() + "', but the tensor's element "
        "type is '" +
        typeCodeToString(*tensor.dtype) +
        "': a tensor operator computes in its element type, and there "
        "is no implicit conversion between numeric types (write the conversion, " +
        typeCodeToString(computed) +
        "(value); a scalar read out of the array is written sum(v) or an explicit conversion)");
}

InferFn binaryInfer(bool producesFloat) {
    return [producesFloat](const InferContext &ctx) -> std::optional<Type *> {
        if (!anyTensor(ctx, 2)) {
            return std::nullopt;
        }
        const TensorFacts lhs = ctx.facts(0), rhs = ctx.facts(1);
        std::optional<TypeCode> dtype = operandType(ctx, 0, 1);
        if (producesFloat && dtype && !isReducedFloatTensorType(*dtype)) {
            // Div and Pow produce a float: f64 when a tensor operand is f64, f32 otherwise. A
            // bf16 or f16 operand keeps its element type: the quotient is computed in f32 and
            // rounded once when it is stored (the one arithmetic path of dtype.h).
            dtype = *dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                                : std::optional(TypeCode::Float32);
        }
        return tensorOf(dtype, broadcast(lhs.shape, rhs.shape));
    };
}

/// A comparison of two numeric arrays compares elements of one element type: the two operands are
/// read by the same rule as the arithmetic around them (`operandType`), so an f64 array beside an
/// f32 one is refused (refuseMixedElementTypes) rather than compared element by element at a width
/// the program did not write, and a scalar operand - a literal above all - is read at the tensor's
/// element type. The element type that rule answers with is not the comparison's: the result is a
/// `bool` array of the broadcast shape.
std::optional<Type *> compareInfer(const InferContext &ctx) {
    if (!anyTensorLike(ctx, 2)) {
        return std::nullopt;
    }
    const TensorFacts lhs = ctx.facts(0), rhs = ctx.facts(1);
    operandType(ctx, 0, 1);
    return tensorOf(TypeCode::Bool, broadcast(lhs.shape, rhs.shape));
}

InferFn unaryInfer(bool producesFloat) {
    return [producesFloat](const InferContext &ctx) -> std::optional<Type *> {
        const TensorFacts in = ctx.facts(0);
        std::optional<TypeCode> dtype = in.dtype;
        if (producesFloat && in.dtype && !isReducedFloatTensorType(*in.dtype)) {
            // A math function keeps f64: f64 in, f64 out. An f32 or integer input gives f32,
            // the type these operators have always produced. A bf16 or f16 input keeps its
            // element type and is rounded once when it is stored (dtype.h).
            dtype = *in.dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                                   : std::optional(TypeCode::Float32);
        }
        return tensorOf(dtype, in.shape);
    };
}

const std::vector<ParamSpec> kBinaryParams = {
    {"lhs", ParamKind::TensorOrScalar},
    {"rhs", ParamKind::TensorOrScalar},
};
const std::vector<ParamSpec> kCompareParams = {
    {"lhs", ParamKind::TensorLikeOrScalar},
    {"rhs", ParamKind::TensorLikeOrScalar},
};
const std::vector<ParamSpec> kUnaryParams = {{"t", ParamKind::Tensor}};

OpTraits elementwiseTraits() { return OpTraits{.pure = true, .elementwise = true}; }

template <k::BinaryOp Op>
OpDef binaryDef(std::string_view name, std::vector<std::string_view> exports) {
    return OpDef{
        .name      = name,
        .exports   = std::move(exports),
        .params    = kBinaryParams,
        .resultDoc = "float[..]",
        .infer     = binaryInfer(k::binaryProducesFloat(Op)),
        .kernel    = &binaryKernel<Op>,
        .traits    = elementwiseTraits(),
    };
}

template <k::CompareOp Op>
OpDef compareDef(std::string_view name, std::vector<std::string_view> exports) {
    return OpDef{
        .name      = name,
        .exports   = std::move(exports),
        .params    = kCompareParams,
        .resultDoc = "bool[..]",
        .infer     = compareInfer,
        .kernel    = &compareKernel<Op>,
        .traits    = elementwiseTraits(),
    };
}

template <k::UnaryOp Op>
OpDef unaryDef(std::string_view name, std::vector<std::string_view> exports) {
    return OpDef{
        .name      = name,
        .exports   = std::move(exports),
        .params    = kUnaryParams,
        .resultDoc = "float[..]",
        .infer     = unaryInfer(k::unaryProducesFloat(Op)),
        .kernel    = &unaryKernel<Op>,
        .traits    = elementwiseTraits(),
    };
}

slot_t whereKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("where", [&] {
        k::ScalarOperand lhsScalar{}, rhsScalar{};
        const k::Operand cond = k::Operand::of(tensorArg(norm, 0));
        const k::Operand lhs  = operandArg(norm, 1, lhsScalar);
        const k::Operand rhs  = operandArg(norm, 2, rhsScalar);
        return wrap(norm.resultType(), k::where(cond, lhs, rhs, resultAllocator()));
    });
}

/// The condition selects between the two values and is not one of them: the element type is the
/// one the two values state (argument 1 and 2), and the mask is bool whatever that type is.
std::optional<Type *> whereInfer(const InferContext &ctx) {
    const TensorFacts c = ctx.facts(0), a = ctx.facts(1), b = ctx.facts(2);
    return tensorOf(operandType(ctx, 1, 2), broadcast(c.shape, broadcast(a.shape, b.shape)));
}

// ---------------------------------------------------------------- VJP rules

// Rules of broadcasting binary operators reduce each operand's gradient to its shape; an operand
// that is a float receives the gradient's sum.

void addVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        accumulateOperand(b, call.inputs[0], *dy);
        accumulateOperand(b, call.inputs[1], *dy);
    }
}

void subtractVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        accumulateOperand(b, call.inputs[0], *dy);
        accumulateOperand(b, call.inputs[1], addTensorOper(b, "tensor:neg", {*dy}));
    }
}

void multiplyVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
        accumulateOperand(b, lhs, addTensorOper(b, "tensor:multiply", {*dy, rhs}));
        accumulateOperand(b, rhs, addTensorOper(b, "tensor:multiply", {*dy, lhs}));
    }
}

// y = lhs / rhs: dlhs = dy / rhs, drhs = -dy * y / rhs.
void divideVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
        const vjp_node_t dlhs = addTensorOper(b, "tensor:divide", {*dy, rhs});
        accumulateOperand(b, lhs, dlhs);
        const vjp_node_t scaled = addTensorOper(b, "tensor:multiply", {dlhs, call.output});
        const vjp_node_t neg    = addTensorOper(b, "tensor:neg", {scaled});
        accumulateOperand(b, rhs, neg);
        // The three operators are one chain a backend may run in one pass over the buffers: each
        // element is divided, multiplied and negated in that order, so a backend that keeps the
        // roundings of the operators (7.3) produces the same bits (7.4). The group is recorded
        // where the chain is built; a node another node reads makes the chain unobservable to a
        // backend, which then runs the operators one by one (camel/execute/fusion_group.h).
        //
        // Only when the denominator is differentiated is the chain reachable at all (the neg is
        // what carries the gradient back to `rhs`); otherwise the three nodes are dropped as
        // dead and the attribute would name nodes that are not a group.
        if (b.isActive(rhs)) {
            // The largest group that can run. When the numerator is not differentiated the
            // divide's result has one reader (the multiply), so the three nodes are private and
            // the chain runs as one pass. When it is, the divide's result is also the numerator's
            // gradient: the gradient struct reads it, the chain's head is not private, and the
            // backend declines the three-node group. The multiply and the neg are still private,
            // so the pair is marked instead and runs at the neg off the stored divide's value.
            // One key holds one value per node (camel/core/node_attrs.h): the two groups never
            // share a node, so a node names the group its shape allows.
            if (b.isActive(lhs)) {
                const vjp_node_t pair[] = {scaled, neg};
                b.markFusionGroup(fusionAttrKey(), kMulNegFusion, pair);
            } else {
                const vjp_node_t chain[] = {dlhs, scaled, neg};
                b.markFusionGroup(fusionAttrKey(), kDivideMulNegFusion, chain);
            }
        }
    }
}

// y = lhs ^ rhs: dlhs = dy * rhs * lhs ^ (rhs - 1), drhs = dy * y * log(lhs).
void powVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
        const vjp_node_t lowered =
            addTensorOper(b, "tensor:subtract", {rhs, floatGradientAtWidth(b, rhs, b.addStaticFloat(1.0))});
        const vjp_node_t slope = addTensorOper(
            b,
            "tensor:multiply",
            {rhs, addTensorOper(b, "tensor:pow", {lhs, lowered})});
        accumulateOperand(b, lhs, addTensorOper(b, "tensor:multiply", {*dy, slope}));
        // The base may be a float: broadcast it to the result before taking its logarithm.
        const vjp_node_t base   = addTensorOper(b, "tensor:broadcast_like", {lhs, call.output});
        const vjp_node_t growth = addTensorOper(
            b,
            "tensor:multiply",
            {call.output, addTensorOper(b, "tensor:log", {base})});
        accumulateOperand(b, rhs, addTensorOper(b, "tensor:multiply", {*dy, growth}));
    }
}

// maximum / minimum: the gradient goes to the selected operand (the left one on ties).
template <bool Max> void extremumVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
        const vjp_node_t left = addTensorOper(b, Max ? "tensor:ge" : "tensor:le", {lhs, rhs});
        const vjp_node_t zero = b.addStaticFloat(0.0);
        accumulateOperand(b, lhs, addTensorOper(b, "tensor:where", {left, *dy, zero}));
        accumulateOperand(b, rhs, addTensorOper(b, "tensor:where", {left, zero, *dy}));
    }
}

/// Unary rules: the input gradient is dy combined with the input and/or output.
template <typename Fn> void unaryRule(VjpBuilder &b, const VjpCall &call, Fn &&gradient) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], gradient(*dy, call.inputs[0], call.output));
    }
}

void negVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t) {
        return addTensorOper(b, "tensor:neg", {dy});
    });
}

void absVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t x, vjp_node_t) {
        const vjp_node_t positive = addTensorOper(b, "tensor:ge", {x, b.addStaticFloat(0.0)});
        return addTensorOper(
            b,
            "tensor:where",
            {positive, dy, addTensorOper(b, "tensor:neg", {dy})});
    });
}

void reluVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t y) {
        return reluGradient(b, y, dy);
    });
}

void expVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t y) {
        return addTensorOper(b, "tensor:multiply", {dy, y});
    });
}

void logVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t x, vjp_node_t) {
        return addTensorOper(b, "tensor:divide", {dy, x});
    });
}

// y = sqrt(x): dx = dy * 0.5 / y.
void sqrtVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t y) {
        const vjp_node_t half = addTensorOper(b, "tensor:multiply", {dy, b.addStaticFloat(0.5)});
        return addTensorOper(b, "tensor:divide", {half, y});
    });
}

// y = x ^ -1/2: dx = -0.5 * dy * y^3.
void rsqrtVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t y) {
        const vjp_node_t cube =
            addTensorOper(b, "tensor:multiply", {y, addTensorOper(b, "tensor:multiply", {y, y})});
        const vjp_node_t scaled = addTensorOper(b, "tensor:multiply", {dy, b.addStaticFloat(-0.5)});
        return addTensorOper(b, "tensor:multiply", {scaled, cube});
    });
}

// dy * y * (1 - y)
void sigmoidVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t y) {
        const vjp_node_t oneMinusY =
            addTensorOper(b, "tensor:subtract", {b.addStaticFloat(1.0), y});
        const vjp_node_t slope = addTensorOper(b, "tensor:multiply", {y, oneMinusY});
        return addTensorOper(b, "tensor:multiply", {dy, slope});
    });
}

// dy * (1 - y^2)
void tanhVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t, vjp_node_t y) {
        const vjp_node_t ySquared = addTensorOper(b, "tensor:multiply", {y, y});
        const vjp_node_t slope =
            addTensorOper(b, "tensor:subtract", {b.addStaticFloat(1.0), ySquared});
        return addTensorOper(b, "tensor:multiply", {dy, slope});
    });
}

void geluVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t x, vjp_node_t) {
        return addTensorOper(b, "tensor:gelu_grad", {x, dy});
    });
}

void erfVjp(VjpBuilder &b, const VjpCall &call) {
    unaryRule(b, call, [&](vjp_node_t dy, vjp_node_t x, vjp_node_t) {
        return addTensorOper(b, "tensor:erf_grad", {x, dy});
    });
}

// where(cond, lhs, rhs): each branch value receives dy where it was selected.
void whereVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 3);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t cond = call.inputs[0];
        const vjp_node_t zero = b.addStaticFloat(0.0);
        accumulateOperand(b, call.inputs[1], addTensorOper(b, "tensor:where", {cond, *dy, zero}));
        accumulateOperand(b, call.inputs[2], addTensorOper(b, "tensor:where", {cond, zero, *dy}));
    }
}

} // namespace

std::vector<OpDef> elementwiseOps() {
    std::vector<OpDef> defs;
    defs.push_back(binaryDef<k::BinaryOp::Add>("add", {"__add__"}));
    defs.push_back(binaryDef<k::BinaryOp::Sub>("subtract", {"__sub__"}));
    defs.push_back(binaryDef<k::BinaryOp::Mul>("multiply", {"__mul__"}));
    defs.push_back(binaryDef<k::BinaryOp::Div>("divide", {"__div__"}));
    defs.push_back(binaryDef<k::BinaryOp::Pow>("pow", {"__pow__"}));
    defs.push_back(binaryDef<k::BinaryOp::Max>("maximum", {"maximum"}));
    defs.push_back(binaryDef<k::BinaryOp::Min>("minimum", {"minimum"}));
    // `%` over numeric arrays. It is the element type's own operator, applied elementwise: a
    // bf16 or f16 array computes in f32 and rounds once, the same path as + - * / and pow, so
    // reduced precision is not a reason to refuse it (design principles 7.3). Two scalars never
    // reach this definition (binaryInfer), so the scalar `%` is unchanged.
    defs.push_back(binaryDef<k::BinaryOp::Mod>("mod", {"__mod__"}));

    defs.push_back(compareDef<k::CompareOp::Less>("lt", {"__lt__"}));
    defs.push_back(compareDef<k::CompareOp::LessEqual>("le", {"__le__"}));
    defs.push_back(compareDef<k::CompareOp::Greater>("gt", {"__gt__"}));
    defs.push_back(compareDef<k::CompareOp::GreaterEqual>("ge", {"__ge__"}));
    // Equality and inequality of two arrays are elementwise (`===`, `!==`, design principles
    // 4.2). The whole comparison is `==` and belongs to the core's operator, which reduces this
    // one; there is no library operator named `eq` or `ne` beside them.
    defs.push_back(compareDef<k::CompareOp::Equal>("strict_eq", {"__strict_eq__"}));
    defs.push_back(compareDef<k::CompareOp::NotEqual>("strict_neq", {"__strict_neq__"}));

    defs.push_back(unaryDef<k::UnaryOp::Neg>("neg", {"__neg__"}));
    defs.push_back(unaryDef<k::UnaryOp::Abs>("abs", {"abs"}));
    defs.push_back(unaryDef<k::UnaryOp::Exp>("exp", {"exp"}));
    defs.push_back(unaryDef<k::UnaryOp::Log>("log", {"log"}));
    defs.push_back(unaryDef<k::UnaryOp::Sqrt>("sqrt", {"sqrt"}));
    defs.push_back(unaryDef<k::UnaryOp::Rsqrt>("rsqrt", {"rsqrt"}));
    defs.push_back(unaryDef<k::UnaryOp::Sigmoid>("sigmoid", {"sigmoid"}));
    defs.push_back(unaryDef<k::UnaryOp::Tanh>("tanh", {"tanh"}));
    defs.push_back(unaryDef<k::UnaryOp::Relu>("relu", {"relu"}));
    defs.push_back(unaryDef<k::UnaryOp::Gelu>("gelu", {"gelu"}));
    defs.push_back(unaryDef<k::UnaryOp::Erf>("erf", {"erf"}));

    defs.push_back(OpDef{
        .name    = "where",
        .exports = {"where"},
        .params =
            {{"cond", ParamKind::Tensor},
             {"lhs", ParamKind::TensorOrScalar},
             {"rhs", ParamKind::TensorOrScalar}},
        .resultDoc = "float[..]",
        .infer     = whereInfer,
        .kernel    = &whereKernel,
        .traits    = elementwiseTraits(),
    });
    setVjp(defs, "add", &addVjp);
    setVjp(defs, "subtract", &subtractVjp);
    setVjp(defs, "multiply", &multiplyVjp);
    setVjp(defs, "divide", &divideVjp);
    setVjp(defs, "pow", &powVjp);
    setVjp(defs, "maximum", &extremumVjp<true>);
    setVjp(defs, "minimum", &extremumVjp<false>);
    setVjp(defs, "neg", &negVjp);
    setVjp(defs, "abs", &absVjp);
    setVjp(defs, "exp", &expVjp);
    setVjp(defs, "log", &logVjp);
    setVjp(defs, "sqrt", &sqrtVjp);
    setVjp(defs, "rsqrt", &rsqrtVjp);
    setVjp(defs, "sigmoid", &sigmoidVjp);
    setVjp(defs, "tanh", &tanhVjp);
    setVjp(defs, "relu", &reluVjp);
    setVjp(defs, "gelu", &geluVjp);
    setVjp(defs, "erf", &erfVjp);
    setVjp(defs, "where", &whereVjp);
    // Comparisons are locally constant.
    for (std::string_view name : {"lt", "le", "gt", "ge", "strict_eq", "strict_neq"}) {
        setVjp(defs, name, &camel::core::noGradient);
    }
    return defs;
}

} // namespace camel::tensor::ops
