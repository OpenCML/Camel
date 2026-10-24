// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The ONNX lowering table for the tensor and nn operators (see lowering.h).
 *
 * Conventions shared by the entries:
 *   - Arithmetic operands are converted to the inferred result dtype, which
 *     reproduces Camel's promotion rules (ONNX requires equal input types).
 *   - Comparisons convert operands to their promoted dtype and yield bool.
 *   - Hyper-parameters (axes, strides, shapes) must be export-time constants;
 *     they become node attributes or constant int64 initializers.
 */

#include "lowering.h"

#include "../tensor/dtype.h"
#include "../tensor/ops/support.h"
#include "../tensor/type.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>
#include <format>
#include <limits>
#include <numeric>

namespace camel::onnx {

using tensor::ops::ConstArg;
using type::TypeCode;

// ---------------------------------------------------------------- LowerContext

size_t LowerContext::rank(size_t index) const {
    const auto facts = factsOf(args_[index]);
    if (!facts.shape) {
        throw ExportError(
            std::format("'{}': the rank of argument {} must be known statically", uri_, index));
    }
    return facts.shape->size();
}

std::string LowerContext::input(size_t index, std::optional<TypeCode> dtype) const {
    return emitter_.operand(args_[index], dtype);
}

std::optional<TypeCode> LowerContext::elementDtype() const {
    if (result_.dtype) {
        return result_.dtype;
    }
    for (size_t i = 0; i < args_.size(); ++i) {
        if (auto dtype = factsOf(args_[i]).dtype) {
            return dtype;
        }
    }
    return TypeCode::Float32;
}

std::optional<ConstArg> LowerContext::constArg(size_t index) const {
    return has(index) ? constArgOf(args_[index]) : std::nullopt;
}

void LowerContext::fail(size_t index, std::string_view expected) const {
    throw ExportError(
        std::format(
            "'{}': argument {} must be an export-time constant {} (it depends on the model input)",
            uri_,
            index,
            expected));
}

int64_t LowerContext::constInt(size_t index) const {
    if (auto c = constArg(index)) {
        if (auto *v = std::get_if<int64_t>(&*c)) {
            return *v;
        }
    }
    fail(index, "integer");
}

int64_t LowerContext::constInt(size_t index, int64_t fallback) const {
    return has(index) ? constInt(index) : fallback;
}

double LowerContext::constNumber(size_t index, double fallback) const {
    if (!has(index)) {
        return fallback;
    }
    if (auto c = constArg(index)) {
        if (auto *v = std::get_if<double>(&*c)) {
            return *v;
        }
        if (auto *v = std::get_if<int64_t>(&*c)) {
            return static_cast<double>(*v);
        }
    }
    fail(index, "number");
}

bool LowerContext::constBool(size_t index, bool fallback) const {
    if (!has(index)) {
        return fallback;
    }
    if (auto c = constArg(index)) {
        if (auto *v = std::get_if<bool>(&*c)) {
            return *v;
        }
    }
    fail(index, "bool");
}

std::string LowerContext::constString(size_t index) const {
    if (auto c = constArg(index)) {
        if (auto *v = std::get_if<std::string>(&*c)) {
            return *v;
        }
    }
    fail(index, "string");
}

std::vector<int64_t> LowerContext::constInts(size_t index) const {
    if (auto c = constArg(index)) {
        if (auto *v = std::get_if<std::vector<int64_t>>(&*c)) {
            return *v;
        }
    }
    fail(index, "int array");
}

Value LowerContext::emit(
    std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes) const {
    return result(emitter_.node(std::move(opType), std::move(inputs), std::move(attributes)));
}

Value LowerContext::result(std::string name) const {
    return Value::symbolic(std::move(name), result_.dtype, result_.shape);
}

Value LowerContext::emitScalar(
    std::string opType, std::vector<std::string> inputs, std::vector<Attribute> attributes) const {
    if (!resultType_) {
        throw ExportError(std::format("'{}': the result type is not known", uri_));
    }
    return Value::symbolicScalar(
        emitter_.node(std::move(opType), std::move(inputs), std::move(attributes)),
        resultType_);
}

// ---------------------------------------------------------------- lowerings

namespace {

Attribute ints(std::string name, std::vector<int64_t> values) {
    return Attribute::makeInts(std::move(name), std::move(values));
}

Attribute intAttr(std::string name, int64_t value) {
    return Attribute::makeInt(std::move(name), value);
}

/// ReduceMax (and friends) of `x` over `axes`, in the form the opset takes (defined below).
std::string reduceAxes(
    Emitter &e, const std::string &op, const std::string &x, std::vector<int64_t> axes,
    bool keepDims);

/// The element type an elementwise operator computes in, mirroring the kernels' `operandType`:
/// one scalar takes the tensor's element type (a float scalar meeting an integer tensor computes
/// in f32), and two tensors promote by widening. A bare `Tensor` operand has no facts and takes
/// the graph's element type. The whole node is emitted in this one type, so ONNX never sees a
/// double constant beside a float tensor.
std::optional<TypeCode> operandDtype(const LowerContext &ctx);

/// Every argument converted to the dtype the operator computes in, then one node.
LowerFn sameTypeOp(std::string opType, size_t operands) {
    return [opType = std::move(opType), operands](const LowerContext &ctx) {
        std::optional<TypeCode> dtype = operands == 1 ? ctx.facts(0).dtype : operandDtype(ctx);
        if (!dtype) {
            dtype = ctx.elementDtype();
        }
        std::vector<std::string> inputs;
        for (size_t i = 0; i < operands; ++i) {
            inputs.push_back(ctx.input(i, dtype));
        }
        return ctx.emit(opType, std::move(inputs));
    };
}

/// Whether an argument is a scalar Camel number rather than tensor data: a constant scalar has a
/// numeric Camel type, a symbolic one says Form::Scalar.
bool isScalarOperand(const Value &value) {
    if (value.form == Value::Form::Scalar) {
        return true;
    }
    if (value.ty == nullptr) {
        return false;
    }
    const TypeCode code = value.ty->code();
    return code == TypeCode::Int32 || code == TypeCode::Int64 || code == TypeCode::Float32 ||
           code == TypeCode::Float64 || code == TypeCode::Bool;
}

std::optional<TypeCode> operandDtype(const LowerContext &ctx) {
    const auto left  = ctx.facts(0);
    const auto right = ctx.facts(1);
    const bool leftData  = !isScalarOperand(ctx.arg(0));
    const bool rightData = !isScalarOperand(ctx.arg(1));
    if (leftData != rightData) {
        const tensor::ops::TensorFacts &data = leftData ? left : right;
        const std::optional<TypeCode> scalar = leftData ? right.dtype : left.dtype;
        if (data.dtype) {
            if (tensor::isFloatingTensorType(*data.dtype)) {
                return data.dtype;
            }
            return scalar && tensor::isFloatingTensorType(*scalar) ? std::optional(TypeCode::Float32)
                                                           : data.dtype;
        }
        return TypeCode::Float32;
    }
    if (auto promoted = tensor::ops::promote(left.dtype, right.dtype)) {
        return promoted;
    }
    return ctx.elementDtype();
}

LowerFn compareOp(std::string opType, bool negate) {
    return [opType = std::move(opType), negate](const LowerContext &ctx) {
        auto dtype = operandDtype(ctx);
        std::vector<std::string> inputs{ctx.input(0, dtype), ctx.input(1, dtype)};
        if (!negate) {
            return ctx.emit(opType, std::move(inputs));
        }
        const std::string eq = ctx.emitter().node(opType, std::move(inputs));
        return ctx.emit("Not", {eq});
    };
}

Value lowerRsqrt(const LowerContext &ctx) {
    const std::string sqrt = ctx.emitter().node("Sqrt", {ctx.input(0, ctx.elementDtype())});
    return ctx.emit("Reciprocal", {sqrt});
}

/// GELU with the tanh approximation (the Camel kernel's definition).
Value lowerGelu(const LowerContext &ctx) {
    const std::string x = ctx.input(0, ctx.elementDtype());
    if (ctx.opset() >= 20) {
        return ctx.emit("Gelu", {x}, {Attribute::makeString("approximate", "tanh")});
    }
    Emitter &e        = ctx.emitter();
    const auto scalar = [&](double v) {
        return e.operand(
            Value::constant(rtdata::toSlot(static_cast<rtdata::Float64>(v)), type::Type::Float64()),
            TypeCode::Float32);
    };
    const std::string x3    = e.node("Mul", {e.node("Mul", {x, x}), x});
    const std::string inner = e.node("Add", {x, e.node("Mul", {x3, scalar(0.044715)})});
    const std::string t     = e.node("Tanh", {e.node("Mul", {inner, scalar(0.7978845608028654)})});
    const std::string half  = e.node("Mul", {x, scalar(0.5)});
    return ctx.emit("Mul", {half, e.node("Add", {t, scalar(1.0)})});
}

Value lowerWhere(const LowerContext &ctx) {
    return ctx.emit(
        "Where",
        {ctx.input(0, TypeCode::Bool),
         ctx.input(1, ctx.elementDtype()),
         ctx.input(2, ctx.elementDtype())});
}

Value lowerCast(const LowerContext &ctx) {
    if (!ctx.result().dtype) {
        throw ExportError("'tensor:cast': the target dtype must be an export-time constant");
    }
    return ctx.emit(
        "Cast",
        {ctx.input(0)},
        {intAttr("to", static_cast<int64_t>(elemTypeOf(*ctx.result().dtype)))});
}

Value lowerLinear(const LowerContext &ctx) {
    const auto dtype   = ctx.elementDtype();
    const bool hasBias = ctx.has(2);
    std::string x      = ctx.input(0, dtype);
    std::string w      = ctx.input(1, dtype);
    if (ctx.rank(0) == 2) {
        std::vector<std::string> inputs{x, w};
        if (hasBias) {
            inputs.push_back(ctx.input(2, dtype));
        }
        return ctx.emit("Gemm", std::move(inputs));
    }
    if (!hasBias) {
        return ctx.emit("MatMul", {x, w});
    }
    const std::string product = ctx.emitter().node("MatMul", {x, w});
    return ctx.emit("Add", {product, ctx.input(2, dtype)});
}

Value lowerTranspose(const LowerContext &ctx) {
    const size_t rank = ctx.rank(0);
    if (rank < 2) {
        throw ExportError("'tensor:transpose' needs a tensor of rank >= 2");
    }
    std::vector<int64_t> perm(rank);
    std::iota(perm.begin(), perm.end(), 0);
    std::swap(perm[rank - 1], perm[rank - 2]);
    return ctx.emit("Transpose", {ctx.input(0)}, {ints("perm", perm)});
}

Value lowerPermute(const LowerContext &ctx) {
    return ctx.emit("Transpose", {ctx.input(0)}, {ints("perm", ctx.constInts(1))});
}

/// Operand name of an int[] argument (a shape) as a 1-D int64 tensor, and its known entries.
std::pair<std::string, std::vector<int64_t>> shapeOperand(const LowerContext &ctx, size_t index) {
    const Value &arg = ctx.arg(index);
    if (arg.isSymbolic() && arg.form == Value::Form::IntArray) {
        return {ctx.input(index, TypeCode::Int64), arg.elements};
    }
    const auto values = ctx.constInts(index);
    return {ctx.emitter().int64s(values), values};
}

Value lowerReshape(const LowerContext &ctx) {
    auto [shape, known] = shapeOperand(ctx, 1);
    std::vector<Attribute> attrs;
    if (ctx.opset() >= 14) {
        attrs.push_back(intAttr("allowzero", 1)); // a zero extent means zero, as in Camel
    }
    Value out = ctx.emit("Reshape", {ctx.input(0), shape}, std::move(attrs));
    if (!out.shape) {
        // Inference could not use a dynamic shape; its statically known entries still hold.
        for (int64_t &d : known) {
            d = d < 0 ? tensor::kUnknownDim : d;
        }
        out.shape = known;
    }
    return out;
}

/// dim(t, axis): the extent of one axis, a Camel int. A constant when that extent is static
/// (the gradient rules of reductions and concat ask for it on values that depend on the model
/// input, whose shapes are nevertheless known); otherwise read from the Shape of the tensor.
Value lowerDim(const LowerContext &ctx) {
    const auto facts = ctx.facts(0);
    if (!facts.shape) {
        throw ExportError("'tensor:dim' of a tensor of unknown rank is not supported");
    }
    const int64_t rank = static_cast<int64_t>(facts.shape->size());
    int64_t axis       = ctx.constInt(1);
    axis               = axis < 0 ? axis + rank : axis;
    if (axis < 0 || axis >= rank) {
        throw ExportError(
            std::format("'tensor:dim': axis {} is out of range for rank {}", ctx.constInt(1), rank));
    }
    const int64_t extent = (*facts.shape)[static_cast<size_t>(axis)];
    if (extent != tensor::kUnknownDim) {
        return Value::constant(rtdata::toSlot<rtdata::Int64>(extent), type::Type::Int64());
    }
    Emitter &e = ctx.emitter();
    const std::string index =
        e.operand(Value::constant(rtdata::toSlot<rtdata::Int64>(axis), type::Type::Int64()), TypeCode::Int64);
    return Value::symbolicScalar(
        e.node("Gather", {e.node("Shape", {ctx.input(0)}), index}, {intAttr("axis", 0)}),
        type::Type::Int64());
}

/// numel(t): the number of elements, a Camel int. A constant when the shape is static, otherwise
/// the ONNX Size of the tensor.
Value lowerNumel(const LowerContext &ctx) {
    const auto facts = ctx.facts(0);
    if (facts.shape &&
        std::ranges::none_of(*facts.shape, [](int64_t d) { return d == tensor::kUnknownDim; })) {
        int64_t count = 1;
        for (int64_t d : *facts.shape) {
            count *= d;
        }
        return Value::constant(rtdata::toSlot<rtdata::Int64>(count), type::Type::Int64());
    }
    return Value::symbolicScalar(ctx.emitter().node("Size", {ctx.input(0)}), type::Type::Int64());
}

Value lowerFlatten(const LowerContext &ctx) {
    return ctx.emit("Flatten", {ctx.input(0)}, {intAttr("axis", ctx.constInt(1, 1))});
}

Value lowerUnsqueeze(const LowerContext &ctx) {
    const int64_t axis[] = {ctx.constInt(1)};
    return ctx.emit("Unsqueeze", {ctx.input(0), ctx.emitter().int64s(axis)});
}

Value lowerConcat(const LowerContext &ctx) {
    const auto dtype = ctx.elementDtype();
    return ctx.emit(
        "Concat",
        {ctx.input(0, dtype), ctx.input(1, dtype)},
        {intAttr("axis", ctx.constInt(2))});
}

/**
 * slice(t, axis, start, end[, step]). The axis and the step are constants of the model. A
 * start or an end outside the axis (after a negative one is counted from the end) is a fault in
 * Camel (design principles 3.9), while the ONNX Slice clamps it, so the exporter never relies
 * on Slice for the bounds:
 *
 *   - constant bounds on an axis of known extent are checked here (proved in range, or the
 *     export fails with a located error) and give a plain Slice;
 *   - otherwise (a bound that depends on the model input, such as the counter of a loop over
 *     the time axis, or an extent known only when the model runs) each bound goes through a
 *     guard that faults in ONNX exactly when Camel faults: a Gather into a vector of n + 1
 *     zeros at b (b >= 0) or b - 1 (b < 0), which is in range exactly for -n <= b <= n, and
 *     whose zero is added to the bound so the guard stays on the data path;
 *   - such a guard cannot be emitted where the model computes both arms of a branch and
 *     selects one (Emitter::speculative): it would fault on the arm Camel does not run. That
 *     slice is refused.
 */
namespace {

/// The parameter of `source` for `operand` (and `axis`, or 0xFF for any), or nullptr.
const core::sem::Param *findParam(
    const core::sem::IndexedInstance &inst, core::sem::ParamSource source, size_t operand,
    uint8_t axis = 0xFF) {
    for (const core::sem::Param &p : inst.domain.params) {
        if (p.source == source && p.operand == operand && (axis == 0xFF || p.axis == axis)) {
            return &p;
        }
    }
    return nullptr;
}

/// The check the declaration makes on operand `operand`, or nullptr.
const core::sem::OperandCheck *findCheck(const core::sem::IndexedInstance &inst, size_t operand) {
    for (const core::sem::OperandCheck &c : inst.checks) {
        if (c.operand == operand) {
            return &c;
        }
    }
    return nullptr;
}

/// True when `value` is -1 * `other` (the form a negative bound's lower limit takes).
bool isNegationOf(
    const core::sem::ExprPool &pool, core::sem::ExprId value, core::sem::ExprId other) {
    const core::sem::Expr &e = pool[value];
    if (e.op != core::sem::ExprOp::Mul) {
        return false;
    }
    const auto isMinusOne = [&](core::sem::ExprId id) {
        const core::sem::Expr &f = pool[id];
        return f.op == core::sem::ExprOp::Const && f.value == -1;
    };
    return (isMinusOne(e.lhs) && e.rhs == other) || (isMinusOne(e.rhs) && e.lhs == other);
}

} // namespace

/**
 * `slice`: the operator's index semantics declaration (ops/indexing.cpp) states the axis, the
 * step and, for a bound that depends on the model, the interval the bound is accepted in. This
 * lowering follows that declaration instead of restating the rules:
 *
 *   - Constant bounds that the declaration proves in range (it gives them no check) go to a
 *     plain ONNX Slice, which normalizes a negative bound the same way. A constant bound the
 *     declaration only checks (the axis extent is not known at export time) is guarded like a
 *     model-dependent one.
 *   - A bound that depends on the model is guarded from the declared interval: the declaration
 *     states [-n, n] around the axis extent n, and the guard Gathers into zeros of length n + 1
 *     at b - (b < 0), which is a valid index exactly for -n <= b <= n (ONNX Gather accepts
 *     negative indices from -n - 1); anything else makes the run fail where Camel faults.
 *   - A declaration this lowering cannot follow (no instance, no declared check, an interval
 *     that is not the axis extent around zero) is a diagnostic, not a guess.
 */
Value lowerSlice(const LowerContext &ctx) {
    Emitter &e = ctx.emitter();
    if (!ctx.instance()) {
        throw ExportError(
            "'tensor:slice': the operator's index semantics declaration gives no instance for "
            "this call (an unknown rank, a non-constant axis or step, or a constant bound "
            "outside the axis), so the export cannot state the rule it follows");
    }
    const core::sem::IndexedInstance &inst = *ctx.instance();
    const core::sem::Param *axisParam      = findParam(inst, core::sem::ParamSource::Static, 1);
    if (axisParam == nullptr || !axisParam->value) {
        throw ExportError(
            "'tensor:slice': the index semantics declaration does not state the axis of this "
            "call");
    }
    const auto facts = ctx.facts(0);
    if (!facts.shape) {
        throw ExportError("'tensor:slice' of a tensor of unknown rank is not supported");
    }
    const int64_t rank = static_cast<int64_t>(facts.shape->size());
    const int64_t at   = *axisParam->value;
    if (at < 0 || at >= rank) {
        throw ExportError(
            std::format(
                "'tensor:slice': the index semantics declaration normalized the axis to {}, which "
                "is out of range for rank {}",
                at,
                rank));
    }
    const int64_t extent              = (*facts.shape)[static_cast<size_t>(at)];
    const core::sem::Param *stepParam = findParam(inst, core::sem::ParamSource::Static, 4);
    const int64_t step = stepParam != nullptr && stepParam->value ? *stepParam->value : 1;
    const core::sem::OperandCheck *startCheck = findCheck(inst, 2);
    const core::sem::OperandCheck *stopCheck  = findCheck(inst, 3);

    // Proved in range from the static extent and the constants: a plain Slice.
    if (startCheck == nullptr && stopCheck == nullptr && ctx.arg(2).isConstant() &&
        ctx.arg(3).isConstant()) {
        if (extent == tensor::kUnknownDim) {
            throw ExportError(
                "'tensor:slice': the index semantics declaration gives no check for constant "
                "bounds whose axis extent is unknown, so the export cannot prove them in range");
        }
        const int64_t start[] = {ctx.constInt(2)}, end[] = {ctx.constInt(3)};
        const int64_t axis[] = {at}, steps[] = {step};
        auto shape = facts.shape.value_or(tensor::StaticShape{});
        if (ctx.result().shape) {
            shape = *ctx.result().shape;
        }
        return Value::symbolic(
            e.node(
                "Slice",
                {ctx.input(0), e.int64s(start), e.int64s(end), e.int64s(axis), e.int64s(steps)}),
            facts.dtype,
            shape);
    }

    if (e.speculative()) {
        throw ExportError(
            std::format(
                "'tensor:slice': its bounds on axis {} cannot be proved within the axis at export "
                "time, and it is on an arm of a branch on a model-dependent condition, which the "
                "exported model computes whether or not it is taken; an ONNX Slice there would "
                "clamp bounds that Camel faults on (design principles 3.9). Prove the bounds with "
                "constants, or move the slice out of the branch",
                at));
    }

    // The guards: zeros of length n + 1, n the extent (known now, or read from the shape).
    const int64_t zero[] = {0}, one[] = {1};
    std::string extentVec;
    if (extent != tensor::kUnknownDim) {
        const int64_t n[] = {extent};
        extentVec         = e.int64s(n);
    } else {
        const int64_t axisIndex[] = {at};
        extentVec                 = e.node(
            "Gather",
            {e.node("Shape", {ctx.input(0)}), e.int64s(axisIndex)},
            {intAttr("axis", 0)});
    }
    const auto int64Cast    = intAttr("to", static_cast<int64_t>(elemTypeOf(TypeCode::Int64)));
    const std::string zeros = e.node(
        "Cast",
        {e.node("ConstantOfShape", {e.node("Add", {extentVec, e.int64s(one)})})},
        {int64Cast});
    const auto guarded = [&](size_t index) {
        std::string b;
        if (ctx.arg(index).isConstant()) {
            const int64_t value[] = {ctx.constInt(index)};
            b                     = e.int64s(value);
        } else {
            b = e.node("Unsqueeze", {ctx.input(index, TypeCode::Int64), e.int64s(zero)});
        }
        const core::sem::OperandCheck *check = findCheck(inst, index);
        if (check == nullptr) {
            if (ctx.arg(index).isConstant()) {
                return b; // the declaration proved the constant in range
            }
            throw ExportError(
                std::format(
                    "'tensor:slice': the index semantics declaration states no fault for argument "
                    "{} when it depends on the model, so the exported model would clamp a bound "
                    "that Camel faults on",
                    index));
        }
        if (check->onInvalid != core::sem::OnInvalid::Fault) {
            throw ExportError(
                std::format(
                    "'tensor:slice': the index semantics declaration does not make a bound outside "
                    "the axis of argument {} a fault, which the ONNX guard would have to reproduce",
                    index));
        }
        const core::sem::Expr &upper = inst.exprs[check->upper];
        const bool isExtent =
            upper.op == core::sem::ExprOp::Param &&
            inst.domain.params[upper.value].source == core::sem::ParamSource::Extent &&
            inst.domain.params[upper.value].operand == 0 &&
            inst.domain.params[upper.value].axis == at;
        if (!isExtent || !isNegationOf(inst.exprs, check->lower, check->upper)) {
            throw ExportError(
                std::format(
                    "'tensor:slice': the declared legal range of argument {} is not [-n, n] around "
                    "the extent n of axis {}, which the ONNX fault guard cannot express",
                    index,
                    at));
        }
        const std::string negative =
            e.node("Cast", {e.node("Less", {b, e.int64s(zero)})}, {int64Cast});
        const std::string inRange =
            e.node("Gather", {zeros, e.node("Sub", {b, negative})}, {intAttr("axis", 0)});
        return std::string(e.node("Add", {b, inRange}));
    };
    const std::string starts = guarded(2);
    const std::string ends   = guarded(3);
    const int64_t axis[] = {at}, steps[] = {step};
    auto shape = ctx.result().shape.value_or(facts.shape.value_or(tensor::StaticShape{}));
    shape[static_cast<size_t>(at)] = tensor::kUnknownDim;
    return Value::symbolic(
        e.node("Slice", {ctx.input(0), starts, ends, e.int64s(axis), e.int64s(steps)}),
        facts.dtype,
        shape);
}

/// Reduction over one axis. Opset 18 moved `axes` from an attribute to an input for every
/// reduction except ReduceSum (which moved at opset 13).
LowerFn reduceAxisOp(std::string opType) {
    return [opType = std::move(opType)](const LowerContext &ctx) {
        const int64_t axis     = ctx.constInt(1);
        const int64_t keepDims = ctx.constBool(2, false) ? 1 : 0;
        const bool axesInput   = opType == "ReduceSum" ? ctx.opset() >= 13 : ctx.opset() >= 18;
        const std::string x    = ctx.input(0, ctx.elementDtype());
        if (axesInput) {
            const int64_t axes[] = {axis};
            return ctx.emit(
                opType,
                {x, ctx.emitter().int64s(axes)},
                {intAttr("keepdims", keepDims)});
        }
        return ctx.emit(opType, {x}, {ints("axes", {axis}), intAttr("keepdims", keepDims)});
    };
}

/**
 * `argmax`: the position of the first NaN when the operand has one, else the first position of
 * the largest value by IEEE 754-2019 maximum (-0 < +0), the first such position on ties
 * (design principles 7.7). ONNX ArgMax takes the first index on ties with select_last_index=0,
 * but it orders -0 and +0 the same and its NaN behavior is not specified, so the float cases
 * compose the rule:
 *
 *   isNaN      = Not(Equal(x, x))
 *   hasNaN     = ReduceMax(Cast(isNaN, int64)) != 0
 *   nanFirst   = ArgMax(Cast(isNaN, int64))           first NaN
 *   y          = Where(isNaN, -inf, x)                NaN cannot win the value race
 *   maxValue   = ReduceMax(y)
 *   valueFirst = ArgMax(y)                            first maximum, -0 == +0
 *   plusZero   = And(Equal(y, 0), Greater(Reciprocal(y), 0))   +0, not -0
 *   maxIsZero  = Equal(maxValue, 0)
 *   usePlus    = And(maxIsZero, ReduceMax(Cast(plusZero, int64)) != 0)
 *   result     = Where(hasNaN, nanFirst,
 *                      Where(usePlus, ArgMax(Cast(plusZero, int64)), valueFirst))
 *
 * The last steps replace the first zero by the first +0 when the maximum is a zero (the only
 * case where -0 and +0 compete; IEEE maximum says +0 wins, and the first such position). An
 * int64 operand has neither NaN nor -0, and for bool false < true: both go straight to ArgMax
 * (bool converted to int64, whose order ArgMax supports).
 */
Value lowerArgMax(const LowerContext &ctx) {
    Emitter &e             = ctx.emitter();
    const int64_t axis     = ctx.constInt(1);
    const int64_t keepDims = ctx.constBool(2, false) ? 1 : 0;
    const auto argmax      = [&](const std::string &data) {
        return e.node(
            "ArgMax",
            {data},
            {intAttr("axis", axis),
             intAttr("keepdims", keepDims),
             intAttr("select_last_index", 0)});
    };
    const auto dtypeOpt = ctx.facts(0).dtype;
    if (!dtypeOpt) {
        throw ExportError(
            "'tensor:argmax_axis': the element type of the operand is not known, so the export "
            "cannot tell whether NaN and -0 can occur");
    }
    const TypeCode dtype = *dtypeOpt;
    const auto int64Cast = intAttr("to", static_cast<int64_t>(elemTypeOf(TypeCode::Int64)));
    const auto boolCast  = intAttr("to", static_cast<int64_t>(elemTypeOf(TypeCode::Bool)));
    if (dtype == TypeCode::Int64) {
        return ctx.emit(
            "ArgMax",
            {ctx.input(0)},
            {intAttr("axis", axis),
             intAttr("keepdims", keepDims),
             intAttr("select_last_index", 0)});
    }
    if (dtype == TypeCode::Bool) {
        return ctx.emit(
            "ArgMax",
            {e.node("Cast", {ctx.input(0)}, {int64Cast})},
            {intAttr("axis", axis),
             intAttr("keepdims", keepDims),
             intAttr("select_last_index", 0)});
    }
    if (dtype != TypeCode::Float32 && dtype != TypeCode::Float64) {
        throw ExportError(
            std::format(
                "'tensor:argmax_axis': the element type {} has no ONNX lowering",
                static_cast<int>(dtype)));
    }
    const std::string x = ctx.input(0);
    const bool keep     = keepDims != 0;
    const auto scalar   = [&](double v) {
        return e.operand(
            Value::constant(rtdata::toSlot(static_cast<rtdata::Float64>(v)), type::Type::Float64()),
            dtype);
    };
    const std::string zero    = scalar(0.0);
    const std::string isNan   = e.node("Not", {e.node("Equal", {x, x})});
    const std::string isNan64 = e.node("Cast", {isNan}, {int64Cast});
    const std::string hasNan =
        e.node("Cast", {reduceAxes(e, "ReduceMax", isNan64, {axis}, keep)}, {boolCast});
    const std::string nanFirst = argmax(isNan64);
    const std::string y =
        e.node("Where", {isNan, scalar(-std::numeric_limits<double>::infinity()), x});
    const std::string maxValue   = reduceAxes(e, "ReduceMax", y, {axis}, keep);
    const std::string valueFirst = argmax(y);
    const std::string plusZero   = e.node(
        "And",
        {e.node("Equal", {y, zero}), e.node("Greater", {e.node("Reciprocal", {y}), zero})});
    const std::string anyPlus = e.node(
        "Cast",
        {reduceAxes(e, "ReduceMax", e.node("Cast", {plusZero}, {int64Cast}), {axis}, keep)},
        {boolCast});
    const std::string plusFirst = argmax(e.node("Cast", {plusZero}, {int64Cast}));
    const std::string usePlus   = e.node("And", {e.node("Equal", {maxValue, zero}), anyPlus});
    const std::string value     = e.node("Where", {usePlus, plusFirst, valueFirst});
    return ctx.result(e.node("Where", {hasNan, nanFirst, value}));
}

LowerFn softmaxOp(std::string opType) {
    return [opType = std::move(opType)](const LowerContext &ctx) {
        return ctx.emit(
            opType,
            {ctx.input(0, TypeCode::Float32)},
            {intAttr("axis", ctx.constInt(1, -1))});
    };
}

Value lowerLayerNorm(const LowerContext &ctx) {
    return ctx.emit(
        "LayerNormalization",
        {ctx.input(0, TypeCode::Float32),
         ctx.input(1, TypeCode::Float32),
         ctx.input(2, TypeCode::Float32)},
        {intAttr("axis", -1),
         Attribute::makeFloat("epsilon", static_cast<float>(ctx.constNumber(3, 1e-5)))});
}

/// shape(t): a constant when the shape is static (the common case, which lets shape arithmetic
/// fold at export time), otherwise a Shape node whose statically known entries are tracked, so
/// indexing a static dimension still folds.
Value lowerShape(const LowerContext &ctx) {
    const auto facts = ctx.facts(0);
    if (!facts.shape) {
        throw ExportError("'tensor:shape' of a tensor of unknown rank is not supported");
    }
    const bool known =
        std::ranges::none_of(*facts.shape, [](int64_t d) { return d == tensor::kUnknownDim; });
    if (!known) {
        return Value::symbolicIntArray(ctx.emitter().node("Shape", {ctx.input(0)}), *facts.shape);
    }
    static type::ArrayType *intArray = type::ArrayType::create(type::Type::Int64());
    auto *array = ::Array::create(core::mm::autoSpace(), intArray, facts.shape->size());
    for (size_t i = 0; i < facts.shape->size(); ++i) {
        array->set(i, static_cast<rtdata::Int64>((*facts.shape)[i]));
    }
    ctx.emitter().retain(array, intArray);
    return Value::constant(rtdata::toSlot(array), intArray);
}

/// zeros / ones / full with a shape that depends on the input: ConstantOfShape.
LowerFn constantOfShapeOp(std::optional<double> fill) {
    return [fill](const LowerContext &ctx) {
        auto [shape, known]   = shapeOperand(ctx, 0);
        const TypeCode dtype  = ctx.result().dtype.value_or(TypeCode::Float32);
        const double value    = fill ? *fill : ctx.constNumber(1, 0.0);
        const int64_t asInt[] = {static_cast<int64_t>(value)};
        const float asFloat[] = {static_cast<float>(value)};
        TensorValue scalar    = dtype == TypeCode::Int64
                                    ? makeTensor("value", {1}, std::span<const int64_t>(asInt))
                                    : makeTensor("value", {1}, std::span<const float>(asFloat));
        Value out             = ctx.emit(
            "ConstantOfShape",
            {shape},
            {Attribute::makeTensor("value", std::move(scalar))});
        out.dtype = dtype;
        out.shape = known;
        return out;
    };
}

/// arr[i] on an int[] that depends on the input (e.g. shape(x)[0] with a dynamic batch), and
/// arr[i] reading one number out of a tensor that depends on the input: since the tensor flip a
/// `Tensor` parameter is an array, so `t[0]` is this operator, and the read lowers the way the
/// tensor index does (Gather along axis 0, one per index).
Value lowerArrayIndex(const LowerContext &ctx) {
    const Value &array = ctx.arg(0);
    if (array.isSymbolic() && array.form == Value::Form::IntArray && ctx.arg(1).isConstant()) {
        const int64_t i = ctx.constInt(1);
        const auto n    = static_cast<int64_t>(array.elements.size());
        const int64_t k = i < 0 ? i + n : i;
        if (k >= 0 && k < n && array.elements[k] != tensor::kUnknownDim) {
            // A statically known dimension folds even though the whole shape is dynamic.
            return Value::constant(
                rtdata::toSlot(static_cast<rtdata::Int64>(array.elements[k])),
                type::Type::Int64());
        }
    }
    if (!array.isSymbolic() || array.form != Value::Form::IntArray) {
        // A constant index reads one element of any element type: `Gather` takes them all, so
        // `t[0]` on a tensor of the model is the same node the tensor index emitted before the
        // flip (this operator is what the read became). An index the model input decides is
        // lowered only for the shape numbers themselves — the read below, which casts them to
        // int64 — because the element a symbolic index picks out of a numeric array has no type
        // the export can state, and a Loop or a branch that reads it would be built on a guess.
        if (!ctx.arg(1).isConstant()) {
            throw ExportError(
                std::format("'{}': reading an element of '{}' needs a constant index; an index "
                            "the model input decides is only supported for an array of shapes "
                            "(an int array)",
                            ctx.uri(),
                            ctx.arg(0).ty == nullptr ? std::string("a numeric array")
                                                     : ctx.arg(0).ty->toString()));
        }
        std::string v = ctx.input(0, TypeCode::Float64);
        for (size_t i = 1; i < ctx.size(); ++i) {
            v = ctx.emitter().node(
                "Gather", {v, ctx.input(i, TypeCode::Int64)}, {intAttr("axis", 0)});
        }
        return Value::symbolicScalar(v, type::Type::Float64());
    }
    return ctx.emitScalar(
        "Gather",
        {ctx.input(0, TypeCode::Int64), ctx.input(1, TypeCode::Int64)},
        {intAttr("axis", 0)});
}

/// sum(t) / mean(t) over all elements: a Camel float.
LowerFn reduceAllOp(std::string opType) {
    return [opType = std::move(opType)](const LowerContext &ctx) {
        // sum/mean return a Camel float and the kernel accumulates in double (kernels/reduce.cpp,
        // sumAll), so the ONNX reduction is the same computation: the input is widened to double
        // and reduced there. A float32 reduction would round in the graph where the program does
        // not, and declaring a float32 node as a float64 value exports a graph ONNX rejects
        // (the value_info and the node would disagree).
        return Value::symbolicScalar(
            ctx.emitter().node(opType, {ctx.input(0, TypeCode::Float64)}, {intAttr("keepdims", 0)}),
            type::Type::Float64());
    };
}

/// t[i] / t[i, j]: element reads producing a Camel float.
Value lowerTensorIndex(const LowerContext &ctx) {
    Emitter &e    = ctx.emitter();
    std::string v = ctx.input(0, TypeCode::Float64);
    for (size_t i = 1; i < ctx.size(); ++i) {
        v = e.node("Gather", {v, ctx.input(i, TypeCode::Int64)}, {intAttr("axis", 0)});
    }
    return Value::symbolicScalar(v, type::Type::Float64());
}

// ---------------------------------------------------------------- builtin scalars

/// Camel int / float / bool arithmetic on scalars: operands converted to the result's dtype.
LowerFn scalarOp(std::string opType, size_t operands) {
    return [opType = std::move(opType), operands](const LowerContext &ctx) {
        const TypeCode dtype = ctx.resultType()->code();
        std::vector<std::string> inputs;
        for (size_t i = 0; i < operands; ++i) {
            inputs.push_back(ctx.input(i, dtype));
        }
        return ctx.emitScalar(opType, std::move(inputs));
    };
}

/// Scalar comparisons: operands converted to their promoted dtype, bool result.
LowerFn scalarCompare(std::string opType, bool negate) {
    return [opType = std::move(opType), negate](const LowerContext &ctx) {
        auto dtype = tensor::ops::promote(ctx.facts(0).dtype, ctx.facts(1).dtype);
        std::vector<std::string> inputs{ctx.input(0, dtype), ctx.input(1, dtype)};
        if (!negate) {
            return ctx.emitScalar(opType, std::move(inputs));
        }
        return ctx.emitScalar("Not", {ctx.emitter().node(opType, std::move(inputs))});
    };
}

/// Conversions between Camel scalar types (itol, ltod, dtol, ...).
Value lowerScalarCast(const LowerContext &ctx) {
    const TypeCode target = ctx.resultType()->code();
    return ctx.emitScalar(
        "Cast",
        {ctx.input(0)},
        {intAttr("to", static_cast<int64_t>(elemTypeOf(target)))});
}

Value lowerConv2d(const LowerContext &ctx) {
    const int64_t stride  = ctx.constInt(3, 1);
    const int64_t padding = ctx.constInt(4, 0);
    return ctx.emit(
        "Conv",
        {ctx.input(0, TypeCode::Float32),
         ctx.input(1, TypeCode::Float32),
         ctx.input(2, TypeCode::Float32)},
        {ints("strides", {stride, stride}), ints("pads", {padding, padding, padding, padding})});
}

LowerFn poolOp(std::string opType) {
    return [opType = std::move(opType)](const LowerContext &ctx) {
        const int64_t kernel  = ctx.constInt(1);
        const int64_t stride  = ctx.constInt(2, kernel);
        const int64_t padding = ctx.constInt(3, 0);
        std::vector<Attribute> attrs{
            ints("kernel_shape", {kernel, kernel}),
            ints("strides", {stride, stride}),
            ints("pads", {padding, padding, padding, padding})};
        if (opType == "AveragePool") {
            attrs.push_back(intAttr("count_include_pad", 1)); // the kernel divides by k*k
        }
        return ctx.emit(opType, {ctx.input(0, TypeCode::Float32)}, std::move(attrs));
    };
}

Value lowerBatchNorm(const LowerContext &ctx) {
    // Camel order: (input, mean, var, gamma, beta); ONNX order: (X, scale, B, mean, var).
    const auto f = TypeCode::Float32;
    return ctx.emit(
        "BatchNormalization",
        {ctx.input(0, f), ctx.input(3, f), ctx.input(4, f), ctx.input(1, f), ctx.input(2, f)},
        {Attribute::makeFloat("epsilon", static_cast<float>(ctx.constNumber(5, 1e-5)))});
}

/// A float32 scalar constant operand.
std::string f32Const(Emitter &e, double v) {
    return e.operand(
        Value::constant(rtdata::toSlot(static_cast<rtdata::Float64>(v)), type::Type::Float64()),
        TypeCode::Float32);
}

/// ReduceSum/ReduceMean of `x` over `axes`, in the form the opset takes.
std::string reduceAxes(
    Emitter &e, const std::string &op, const std::string &x, std::vector<int64_t> axes,
    bool keepDims) {
    const bool axesInput = op == "ReduceSum" ? e.opset() >= 13 : e.opset() >= 18;
    const Attribute keep = Attribute::makeInt("keepdims", keepDims ? 1 : 0);
    if (axesInput) {
        return e.node(op, {x, e.int64s(axes)}, {keep});
    }
    return e.node(op, {x}, {Attribute::makeInts("axes", std::move(axes)), keep});
}

/// The axes batch-norm statistics reduce: every axis but the channel axis 1.
std::vector<int64_t> bnStatAxes(size_t rank) {
    std::vector<int64_t> axes{0};
    for (size_t i = 2; i < rank; ++i) {
        axes.push_back(static_cast<int64_t>(i));
    }
    return axes;
}

/// [1, C, 1, ...]: the channel count spread over `rank` axes for broadcasting.
std::vector<int64_t> bnChannelShape(size_t rank, int64_t channels) {
    std::vector<int64_t> shape(rank, 1);
    shape[1] = channels;
    return shape;
}

/// The channel count (gamma's extent); batch_norm parameters are rank-1 [C].
int64_t bnChannelCount(const LowerContext &ctx, size_t index) {
    const auto facts = ctx.facts(index);
    if (!facts.shape || facts.shape->size() != 1 || (*facts.shape)[0] == tensor::kUnknownDim) {
        throw ExportError(
            std::format(
                "'{}': the channel count (parameter {}'s extent) must be known statically",
                ctx.uri(),
                index));
    }
    return (*facts.shape)[0];
}

/// momentum * M/(M-1), the running variance's batch-variance factor (the UNBIASED correction):
/// a constant when the input shape is static, a Shape -> Gather -> Cast -> ReduceProd chain
/// otherwise. A static M < 2 is rejected like PyTorch's "more than 1 value per channel".
std::string bnUnbiasedVarFactor(
    Emitter &e, const LowerContext &ctx, const std::string &x, size_t rank, double momentum) {
    const auto facts = ctx.facts(0);
    double count     = 1.0;
    bool known       = facts.shape.has_value();
    if (known) {
        for (size_t i = 0; i < rank; ++i) {
            if (i == 1) {
                continue;
            }
            if ((*facts.shape)[i] == tensor::kUnknownDim) {
                known = false;
                break;
            }
            count *= static_cast<double>((*facts.shape)[i]);
        }
    }
    if (known) {
        if (count < 2.0) {
            throw ExportError(
                std::format(
                    "'{}': expects more than 1 value per channel when training",
                    ctx.uri()));
        }
        return f32Const(e, momentum * count / (count - 1.0));
    }
    const std::string dims =
        e.node("Gather", {e.node("Shape", {x}), e.int64s(bnStatAxes(rank))}, {intAttr("axis", 0)});
    const bool axesInput = e.opset() >= 18;
    const std::string total =
        axesInput ? e.node(
                        "ReduceProd",
                        {dims, e.int64s(std::vector<int64_t>{0})},
                        {intAttr("keepdims", 0)})
                  : e.node("ReduceProd", {dims}, {ints("axes", {0}), intAttr("keepdims", 0)});
    const std::string m =
        e.node("Cast", {total}, {intAttr("to", static_cast<int64_t>(ElemType::Float))});
    const std::string ratio = e.node("Div", {m, e.node("Sub", {m, f32Const(e, 1.0)})});
    return e.node("Mul", {f32Const(e, momentum), ratio});
}

// batch_norm_train(x, gamma, beta, running_mean, running_var, momentum = 0.1, eps = 1e-5)
//   -> (y, new_running_mean, new_running_var)
// PyTorch's training-mode batch_norm, decomposed to standard inference operators: per-channel
// batch statistics over every axis but 1, the BIASED variance for the normalize, the UNBIASED
// (M/(M-1)) variance blended into the running variance.
Value lowerBatchNormTrain(const LowerContext &ctx) {
    Emitter &e        = ctx.emitter();
    const auto f      = TypeCode::Float32;
    const size_t rank = ctx.rank(0);
    if (rank < 2) {
        throw ExportError(std::format("'{}': expects input of rank >= 2", ctx.uri()));
    }
    const std::vector<int64_t> axes = bnStatAxes(rank);
    const int64_t C                 = bnChannelCount(ctx, 1);
    const double momentum           = ctx.constNumber(5, 0.1);
    const double eps                = ctx.constNumber(6, 1e-5);
    const std::string x             = ctx.input(0, f);

    // The normalize path keeps the reduced axes (broadcast against x).
    const std::string mu1  = reduceAxes(e, "ReduceMean", x, axes, true);
    const std::string xc   = e.node("Sub", {x, mu1});
    const std::string xc2  = e.node("Mul", {xc, xc});
    const std::string var1 = reduceAxes(e, "ReduceMean", xc2, axes, true);
    const std::string inv1 =
        e.node("Reciprocal", {e.node("Sqrt", {e.node("Add", {var1, f32Const(e, eps)})})});
    const std::string xhat            = e.node("Mul", {xc, inv1});
    const std::vector<int64_t> cShape = bnChannelShape(rank, C);
    const std::string g4              = e.node("Reshape", {ctx.input(1, f), e.int64s(cShape)});
    const std::string b4              = e.node("Reshape", {ctx.input(2, f), e.int64s(cShape)});
    const std::string y               = e.node("Add", {e.node("Mul", {xhat, g4}), b4});

    // The running statistics are [C] vectors: biased mean, UNBIASED variance, momentum blend.
    const std::string muC  = reduceAxes(e, "ReduceMean", x, axes, false);
    const std::string varC = reduceAxes(e, "ReduceMean", xc2, axes, false);
    const std::string keep = f32Const(e, 1.0 - momentum), mix = f32Const(e, momentum);
    const std::string newRm =
        e.node("Add", {e.node("Mul", {ctx.input(3, f), keep}), e.node("Mul", {muC, mix})});
    const std::string varFactor = bnUnbiasedVarFactor(e, ctx, x, rank, momentum);
    const std::string newRv =
        e.node("Add", {e.node("Mul", {ctx.input(4, f), keep}), e.node("Mul", {varC, varFactor})});

    const tensor::StaticShape statShape{C};
    Value yV               = Value::symbolic(y, f, ctx.facts(0).shape);
    Value rmV              = Value::symbolic(newRm, f, statShape);
    Value rvV              = Value::symbolic(newRv, f, std::move(statShape));
    type::Type *resultType = ctx.resultType();
    if (!resultType || resultType->code() != TypeCode::Tuple) {
        resultType = type::TupleType::create(
            {tensor::ops::tensorOf(f, std::nullopt), tensor::ops::tensorOf(f, std::nullopt), tensor::ops::tensorOf(f, std::nullopt)});
    }
    return Value::aggregate(resultType, {std::move(yV), std::move(rmV), std::move(rvV)});
}

/// take(t, indices) is ONNX Gather along axis 0: the result has the indices' shape followed by
/// t's remaining axes, as the Camel operator's inference computes. A constant index list outside
/// [0, extent) is rejected here (design principles 3.9: the Camel call would fault); a computed
/// index list is exported as it is.
Value lowerTake(const LowerContext &ctx) {
    std::string indices;
    if (ctx.arg(1).kind == Value::Kind::Constant) {
        const auto constantIndices = ctx.constInts(1);
        const auto input           = ctx.facts(0).shape;
        if (input && !input->empty() && (*input)[0] != tensor::kUnknownDim) {
            const int64_t extent = (*input)[0];
            for (const int64_t index : constantIndices) {
                if (index < 0 || index >= extent) {
                    throw ExportError(std::format(
                        "'tensor:take': index {} is out of range for the first dimension of "
                        "extent {}",
                        index,
                        extent));
                }
            }
        }
        // The operator accepts an int[] as well as an int64 tensor (TensorLike); an array has
        // no operand name of its own, so it becomes an i64 initializer directly.
        if (ctx.arg(1).ty && ctx.arg(1).ty->code() == type::TypeCode::Array) {
            indices = ctx.emitter().int64s(constantIndices);
        }
    }
    if (indices.empty()) {
        indices = ctx.input(1, TypeCode::Int64);
    }
    return ctx.emit("Gather", {ctx.input(0), indices}, {intAttr("axis", 0)});
}

Value lowerEmbedding(const LowerContext &ctx) {
    return ctx.emit("Gather", {ctx.input(0), ctx.input(1, TypeCode::Int64)}, {intAttr("axis", 0)});
}

} // namespace

// ---------------------------------------------------------------- registry

LoweringRegistry::LoweringRegistry() {
    auto add = [this](std::string uri, LowerFn fn, int64_t minOpset = 1) {
        entries_.emplace_back(std::move(uri), Lowering{std::move(fn), minOpset});
    };

    // Elementwise arithmetic and comparisons.
    add("tensor:add", sameTypeOp("Add", 2));
    add("tensor:subtract", sameTypeOp("Sub", 2));
    add("tensor:multiply", sameTypeOp("Mul", 2));
    add("tensor:divide", sameTypeOp("Div", 2));
    add("tensor:pow", sameTypeOp("Pow", 2));
    add("tensor:maximum", sameTypeOp("Max", 2));
    add("tensor:minimum", sameTypeOp("Min", 2));
    add("tensor:lt", compareOp("Less", false));
    add("tensor:le", compareOp("LessOrEqual", false));
    add("tensor:gt", compareOp("Greater", false));
    add("tensor:ge", compareOp("GreaterOrEqual", false));
    add("tensor:eq", compareOp("Equal", false));
    add("tensor:ne", compareOp("Equal", true));

    // Elementwise unary.
    add("tensor:neg", sameTypeOp("Neg", 1));
    add("tensor:abs", sameTypeOp("Abs", 1));
    add("tensor:exp", sameTypeOp("Exp", 1));
    add("tensor:log", sameTypeOp("Log", 1));
    add("tensor:sqrt", sameTypeOp("Sqrt", 1));
    add("tensor:rsqrt", lowerRsqrt);
    add("tensor:sigmoid", sameTypeOp("Sigmoid", 1));
    add("tensor:tanh", sameTypeOp("Tanh", 1));
    add("tensor:relu", sameTypeOp("Relu", 1));
    add("tensor:gelu", lowerGelu);
    add("tensor:erf", sameTypeOp("Erf", 1));
    add("tensor:where", lowerWhere);
    add("tensor:cast", lowerCast);

    // Linear algebra and layout.
    add("tensor:matmul", sameTypeOp("MatMul", 2));
    add("tensor:linear", lowerLinear);
    add("tensor:transpose", lowerTranspose);
    add("tensor:permute", lowerPermute);
    add("tensor:reshape", lowerReshape, 5);
    add("tensor:flatten", lowerFlatten);
    add("tensor:unsqueeze", lowerUnsqueeze, 13);
    add("tensor:concat", lowerConcat);
    add("tensor:slice", lowerSlice, 10);
    add("tensor:shape", lowerShape);
    add("tensor:dim", lowerDim);
    add("tensor:numel", lowerNumel);
    add("tensor:zeros", constantOfShapeOp(0.0), 9);
    add("tensor:ones", constantOfShapeOp(1.0), 9);
    add("tensor:full", constantOfShapeOp(std::nullopt), 9);
    add("tensor:idx", lowerTensorIndex);
    add("tensor:idx2d", lowerTensorIndex);

    // Reductions and normalization.
    add("tensor:sum", reduceAllOp("ReduceSum"));
    add("tensor:mean", reduceAllOp("ReduceMean"));
    add("tensor:sum_axis", reduceAxisOp("ReduceSum"));
    add("tensor:mean_axis", reduceAxisOp("ReduceMean"));
    add("tensor:max_axis", reduceAxisOp("ReduceMax"));
    add("tensor:min_axis", reduceAxisOp("ReduceMin"));
    // select_last_index (opset 12) makes the tie rule explicit; the composition follows it.
    add("tensor:argmax_axis", lowerArgMax, 12);
    add("tensor:softmax", softmaxOp("Softmax"), 13);
    add("tensor:log_softmax", softmaxOp("LogSoftmax"), 13);
    add("tensor:layer_norm", lowerLayerNorm, 17);

    // Neural-network layers.
    add("nn:conv2d", lowerConv2d);
    add("nn:max_pool2d", poolOp("MaxPool"));
    add("nn:avg_pool2d", poolOp("AveragePool"));
    add("nn:batch_norm", lowerBatchNorm, 15);
    add("nn:batch_norm_train", lowerBatchNormTrain, 13);
    add("nn:embedding", lowerEmbedding);
    add("tensor:take", lowerTake);

    // Builtin scalar operators (only reached when an operand depends on the model input).
    for (const char *suffix : {"i", "l", "f", "d"}) {
        const std::string t(suffix);
        add(":op/add_" + t, scalarOp("Add", 2));
        add(":op/sub_" + t, scalarOp("Sub", 2));
        add(":op/mul_" + t, scalarOp("Mul", 2));
        add(":op/div_" + t, scalarOp("Div", 2));
        add(":op/neg_" + t, scalarOp("Neg", 1));
        add(":op/lt_" + t, scalarCompare("Less", false));
        add(":op/le_" + t, scalarCompare("LessOrEqual", false));
        add(":op/gt_" + t, scalarCompare("Greater", false));
        add(":op/ge_" + t, scalarCompare("GreaterOrEqual", false));
        add(":op/eq_" + t, scalarCompare("Equal", false));
        add(":op/ne_" + t, scalarCompare("Equal", true));
    }
    add(":op/and", scalarOp("And", 2));
    add(":op/or", scalarOp("Or", 2));
    add(":op/not", scalarOp("Not", 1));
    for (const char *from : {"i", "l", "f", "d"}) {
        for (const char *to : {"i", "l", "f", "d"}) {
            add(std::format(":op/{}to{}", from, to), lowerScalarCast);
        }
    }
    add(":op/idx_arr", lowerArrayIndex);
    addGradientLowerings(add);
    add("math:sqrt", scalarOp("Sqrt", 1));

    std::ranges::sort(entries_, {}, &std::pair<std::string, Lowering>::first);
}

const LoweringRegistry &LoweringRegistry::instance() {
    static const LoweringRegistry registry;
    return registry;
}

const Lowering *LoweringRegistry::find(std::string_view uri, int64_t opset) const {
    auto it = std::ranges::lower_bound(entries_, uri, {}, [](const auto &entry) {
        return std::string_view(entry.first);
    });
    if (it == entries_.end() || it->first != uri || opset < it->second.minOpset) {
        return nullptr;
    }
    return &it->second;
}

std::vector<std::string> LoweringRegistry::supported(int64_t opset) const {
    std::vector<std::string> uris;
    for (const auto &[uri, lowering] : entries_) {
        if (opset >= lowering.minOpset) {
            uris.push_back(uri);
        }
    }
    return uris;
}

} // namespace camel::onnx
