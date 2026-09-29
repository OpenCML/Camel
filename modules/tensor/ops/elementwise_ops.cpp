/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Elementwise operators: broadcasting arithmetic and comparison (tensor with
 * tensor or scalar), unary maps, `where`, and `cast`.
 *
 * Operators are generated from small tables so that one kernel wrapper and
 * one inference function serve each family. Binary operators require at
 * least one tensor operand; scalar-with-scalar arithmetic stays with the
 * builtin scalar operators.
 */

#include "../kernels/elementwise.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

constexpr std::string_view binaryName(k::BinaryOp op) {
    constexpr std::string_view names[] =
        {"add", "subtract", "multiply", "divide", "pow", "maximum", "minimum"};
    return names[static_cast<size_t>(op)];
}

constexpr std::string_view compareName(k::CompareOp op) {
    constexpr std::string_view names[] = {"lt", "le", "gt", "ge", "eq", "ne"};
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
        return wrap(k::binary(Op, lhs, rhs, resultAllocator()));
    });
}

template <k::CompareOp Op> slot_t compareKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(compareName(Op), [&] {
        k::ScalarOperand lhsScalar{}, rhsScalar{};
        const k::Operand lhs = operandArg(norm, 0, lhsScalar);
        const k::Operand rhs = operandArg(norm, 1, rhsScalar);
        return wrap(k::compare(Op, lhs, rhs, resultAllocator()));
    });
}

template <k::UnaryOp Op> slot_t unaryKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(unaryName(Op), [&] {
        return wrap(k::unary(Op, tensorArg(norm, 0), resultAllocator()));
    });
}

InferFn binaryInfer(bool producesFloat) {
    return [producesFloat](const InferContext &ctx) -> std::optional<Type *> {
        if (!anyTensor(ctx, 2)) {
            return std::nullopt;
        }
        const TensorFacts lhs = ctx.facts(0), rhs = ctx.facts(1);
        auto dtype =
            producesFloat ? std::optional(TypeCode::Float32) : promote(lhs.dtype, rhs.dtype);
        return tensorOf(dtype, broadcast(lhs.shape, rhs.shape));
    };
}

std::optional<Type *> compareInfer(const InferContext &ctx) {
    if (!anyTensorLike(ctx, 2)) {
        return std::nullopt;
    }
    return tensorOf(TypeCode::Bool, broadcast(ctx.facts(0).shape, ctx.facts(1).shape));
}

InferFn unaryInfer(bool producesFloat) {
    return [producesFloat](const InferContext &ctx) -> std::optional<Type *> {
        const TensorFacts in = ctx.facts(0);
        return tensorOf(producesFloat ? std::optional(TypeCode::Float32) : in.dtype, in.shape);
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
        .resultDoc = "Tensor",
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
        .resultDoc = "Tensor<bool>",
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
        .resultDoc = "Tensor",
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
        return wrap(k::where(cond, lhs, rhs, resultAllocator()));
    });
}

std::optional<Type *> whereInfer(const InferContext &ctx) {
    const TensorFacts c = ctx.facts(0), a = ctx.facts(1), b = ctx.facts(2);
    return tensorOf(promote(a.dtype, b.dtype), broadcast(c.shape, broadcast(a.shape, b.shape)));
}

/// Storage dtype named by a cast target string ("float32", "int64", "bool", or Camel scalar names).
TypeCode dtypeFromName(const std::string &name) {
    if (name == "float32" || name == "float" || name == "double" || name == "float64") {
        return TypeCode::Float32;
    }
    if (name == "int64" || name == "int" || name == "long" || name == "int32") {
        return TypeCode::Int64;
    }
    if (name == "bool") {
        return TypeCode::Bool;
    }
    throw std::invalid_argument("cast: unknown dtype '" + name + "'");
}

slot_t castKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("cast", [&] {
        return wrap(k::cast(
            tensorArg(norm, 0, true),
            dtypeFromName(stringArg(norm, 1)),
            resultAllocator()));
    });
}

std::optional<Type *> castInfer(const InferContext &ctx) {
    std::optional<TypeCode> dtype;
    if (auto name = ctx.constString(1)) {
        dtype = dtypeFromName(*name);
    }
    return tensorOf(dtype, ctx.facts(0).shape);
}

// ---------------------------------------------------------------- VJP rules

void addVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        accumulateAddendGradient(b, call.inputs[0], *dy);
        accumulateAddendGradient(b, call.inputs[1], *dy);
    }
}

void subtractVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
    if (isTensorNode(b, lhs) || isFloatNode(b, lhs)) {
        b.accumulateGradient(lhs, *dy);
    }
    if (isTensorNode(b, rhs)) {
        b.accumulateGradient(
            rhs,
            addTensorOper(b, "tensor:multiply", {b.addStaticFloat(-1.0), *dy}));
    } else if (isFloatNode(b, rhs)) {
        const vjp_node_t in[] = {b.addStaticFloat(0.0), *dy};
        b.accumulateGradient(rhs, b.addOper(type::Type::Float64(), ":op/sub_d", in));
    }
}

void multiplyVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
    if (isTensorNode(b, lhs)) {
        b.accumulateGradient(lhs, addTensorOper(b, "tensor:multiply", {*dy, rhs}));
    }
    if (isTensorNode(b, rhs)) {
        b.accumulateGradient(rhs, addTensorOper(b, "tensor:multiply", {*dy, lhs}));
    }
}

void divideVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1];
    if (isTensorNode(b, lhs)) {
        b.accumulateGradient(lhs, addTensorOper(b, "tensor:divide", {*dy, rhs}));
    }
    if (isTensorNode(b, rhs)) {
        // d(lhs / rhs)/d rhs = -dy * lhs / rhs^2
        const vjp_node_t negDy = addTensorOper(b, "tensor:subtract", {b.addStaticFloat(0.0), *dy});
        const vjp_node_t rhsSquared = addTensorOper(b, "tensor:multiply", {rhs, rhs});
        const vjp_node_t numerator  = addTensorOper(b, "tensor:multiply", {negDy, lhs});
        b.accumulateGradient(rhs, addTensorOper(b, "tensor:divide", {numerator, rhsSquared}));
    }
}

void reluVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], reluGradient(b, call.output, *dy));
    }
}

void expVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:multiply", {*dy, call.output}));
    }
}

void logVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:divide", {*dy, call.inputs[0]}));
    }
}

void sigmoidVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    // dy * y * (1 - y)
    const vjp_node_t oneMinusY =
        addTensorOper(b, "tensor:subtract", {b.addStaticFloat(1.0), call.output});
    const vjp_node_t slope = addTensorOper(b, "tensor:multiply", {call.output, oneMinusY});
    b.accumulateGradient(call.inputs[0], addTensorOper(b, "tensor:multiply", {*dy, slope}));
}

void tanhVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    // dy * (1 - y^2)
    const vjp_node_t ySquared = addTensorOper(b, "tensor:multiply", {call.output, call.output});
    const vjp_node_t slope = addTensorOper(b, "tensor:subtract", {b.addStaticFloat(1.0), ySquared});
    b.accumulateGradient(call.inputs[0], addTensorOper(b, "tensor:multiply", {*dy, slope}));
}

} // namespace

std::vector<OpDef> elementwiseOps() {
    std::vector<OpDef> defs;
    defs.push_back(binaryDef<k::BinaryOp::Add>("add", {"__add__", "add"}));
    defs.push_back(binaryDef<k::BinaryOp::Sub>("subtract", {"__sub__", "subtract"}));
    defs.push_back(binaryDef<k::BinaryOp::Mul>("multiply", {"__mul__", "multiply"}));
    defs.push_back(binaryDef<k::BinaryOp::Div>("divide", {"__div__", "divide"}));
    defs.push_back(binaryDef<k::BinaryOp::Pow>("pow", {"__pow__", "pow"}));
    defs.push_back(binaryDef<k::BinaryOp::Max>("maximum", {"maximum"}));
    defs.push_back(binaryDef<k::BinaryOp::Min>("minimum", {"minimum"}));

    defs.push_back(compareDef<k::CompareOp::Less>("lt", {"__lt__"}));
    defs.push_back(compareDef<k::CompareOp::LessEqual>("le", {"__le__"}));
    defs.push_back(compareDef<k::CompareOp::Greater>("gt", {"__gt__"}));
    defs.push_back(compareDef<k::CompareOp::GreaterEqual>("ge", {"__ge__"}));
    defs.push_back(compareDef<k::CompareOp::Equal>("eq", {"__eq__"}));
    defs.push_back(compareDef<k::CompareOp::NotEqual>("ne", {"__neq__"}));

    defs.push_back(unaryDef<k::UnaryOp::Neg>("neg", {"__neg__", "neg"}));
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
        .resultDoc = "Tensor",
        .infer     = whereInfer,
        .kernel    = &whereKernel,
        .traits    = elementwiseTraits(),
    });
    defs.push_back(OpDef{
        .name      = "cast",
        .exports   = {"cast"},
        .params    = {{"t", ParamKind::TensorLike}, {"dtype", ParamKind::String}},
        .resultDoc = "Tensor",
        .infer     = castInfer,
        .kernel    = &castKernel,
        .traits    = elementwiseTraits(),
    });
    setVjp(defs, "add", &addVjp);
    setVjp(defs, "subtract", &subtractVjp);
    setVjp(defs, "multiply", &multiplyVjp);
    setVjp(defs, "divide", &divideVjp);
    setVjp(defs, "relu", &reluVjp);
    setVjp(defs, "exp", &expVjp);
    setVjp(defs, "log", &logVjp);
    setVjp(defs, "sigmoid", &sigmoidVjp);
    setVjp(defs, "tanh", &tanhVjp);
    return defs;
}

} // namespace camel::tensor::ops
