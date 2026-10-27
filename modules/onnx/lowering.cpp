// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The ONNX lowering table for the tensor and nn operators (see lowering.h).
 *
 * Conventions shared by the entries:
 *   - Arithmetic operands are emitted in the one element type the node computes in, the same
 *     answer the operator's own inference gives (ONNX requires equal input types and Camel has
 *     no implicit conversion between numeric types).
 *   - Comparisons read their operands by that same rule and yield bool.
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

const LoweringTarget &loweringTarget(std::string_view target) {
    static const LoweringTarget nothingSpecial;
    // onnxruntime-cpu at its defaults: MatmulTransposeFusion (level 2, i.e. the EXTENDED and ALL
    // levels a plain InferenceSession asks for) turns a MatMul beside a Transpose into
    // com.microsoft.FusedMatMul, and the CPU provider's kernel for that operator is registered
    // for float alone (measured on onnxruntime 1.23.2: the session fails to load an f64 model).
    static const LoweringTarget onnxruntimeCpu{
        .transposedProductNeedsGemmOrEinsumForDouble = true};
    if (target == "onnxruntime-cpu") {
        return onnxruntimeCpu;
    }
    return nothingSpecial;
}

bool transposedProductNeedsGemmOrEinsum(
    std::string_view target, std::optional<TypeCode> dtype) {
    return dtype == TypeCode::Float64 &&
           loweringTarget(target).transposedProductNeedsGemmOrEinsumForDouble;
}

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

/// The element type an elementwise operator computes in, the same answer the operator's own
/// inference gives (elementwise_ops.cpp): one scalar operand is read at the tensor's element type
/// (a float scalar beside an integer tensor computes in f32), and two data operands state the one
/// element type the node computes in. Two of them that state different element types are the pair
/// the arithmetic refuses, and the export refuses it here rather than emitting the conversion
/// execution would not make. The whole node is emitted in this one type, so ONNX never sees a
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
    // Two data operands: the same refusal the operator's inference makes, so a node the arithmetic
    // would refuse is refused here too instead of being exported as a conversion.
    tensor::ops::refuseMixedElementTypes(left.dtype, right.dtype);
    if (auto common = tensor::ops::commonElementType(left.dtype, right.dtype)) {
        return common;
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
        throw ExportError("'tensor:to_*': the target dtype must be an export-time constant");
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

/// Operand name of an int[] argument (a shape) as a 1-D int64 tensor, and its known entries (an
/// entry nobody knows is absent: plan section 5.3's migration table).
std::pair<std::string, std::vector<std::optional<int64_t>>>
shapeOperand(const LowerContext &ctx, size_t index) {
    const Value &arg = ctx.arg(index);
    if (arg.isSymbolic() && arg.form == Value::Form::IntArray) {
        return {ctx.input(index, TypeCode::Int64), arg.elements};
    }
    const auto values = ctx.constInts(index);
    std::vector<std::optional<int64_t>> known;
    known.reserve(values.size());
    for (int64_t value : values) {
        known.push_back(value);
    }
    return {ctx.emitter().int64s(values), std::move(known)};
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
        for (std::optional<int64_t> &d : known) {
            if (d && *d < 0) {
                d = std::nullopt;
            }
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
    const std::optional<int64_t> extent = (*facts.shape)[static_cast<size_t>(axis)];
    if (extent.has_value()) {
        return Value::constant(rtdata::toSlot<rtdata::Int64>(*extent), type::Type::Int64());
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
        std::ranges::none_of(*facts.shape, [](const std::optional<int64_t> &d) { return !d; })) {
        int64_t count = 1;
        for (const std::optional<int64_t> &d : *facts.shape) {
            count *= *d;
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
 * `:op/slice_sub`: the core's one slice -- the node both `t[a:b:s]` and `slice(t, axis, a, b, s)`
 * build, for any rank and any axis (design principles 5.4).
 * Its words are the value, the axis, the two bounds, the step and the mask that says which
 * bounds the source wrote (`src/compile/gct/builder.cpp` fills it; the subscript's lowering
 * always writes 3, the function form leaves it out when the caller did). The mask is the core's
 * bookkeeping about the source text rather than part of the slice: it is read here for the
 * defaults -- step 1, both bounds written -- and never exported.
 *
 * ONNX Slice clamps a bound that leaves the axis, where Camel faults (design principles 3.9),
 * and that difference is the whole difficulty of this lowering. So a written bound that is not
 * a proved constant travels through `Add(b, Gather(zeros(extent + 1), b))`: the gathered zero
 * leaves the value alone where the bound is legal, and where it is not, the gather reads outside
 * its zeros and ONNX Runtime fails where the run faults -- and the `Add` is what keeps the guard
 * in the graph at all. The guard's index domain is the axis' own legal range, so a backward
 * slice's start (which must be a position the axis has) is guarded by `extent` zeros and its end
 * (which may be one past the first element) by `extent + 1`. A constant bound needs no guard:
 * the write site refused a negative or out-of-range one already.
 *
 * A bound is a position from the end only in ONNX's own reading of a negative index, which is
 * nothing Camel has. A backward slice's end that the source did not write is the position before
 * the first element, which the kernel reads as -1 (`src/builtin/operators/struct.cpp`,
 * `sliceRange`); in ONNX that same position is named by an end below every extent, because ONNX
 * adds the extent to a negative end and then clamps a negative step's end to -1. So the unwritten
 * end of a backward slice is that constant and not -1: ONNX reads a -1 as the last position of the
 * axis and the slice comes out empty (`x[3::-2]` of a four-element axis was `[]` instead of
 * `[4, 2]`). The constant names the position without the extent, which is what the ONNX Slice
 * documentation recommends for slicing backwards to the end of an axis whose size is not known,
 * so a backward slice of a dynamic extent exports as it stands.
 */
Value lowerSliceSub(const LowerContext &ctx) {
    Emitter &e = ctx.emitter();
    if (!ctx.has(3)) {
        throw ExportError(
            std::format(
                "':op/slice_sub': a slice node has at least 4 words (value, axis, start, end), "
                "this one has {}",
                ctx.size()));
    }
    const auto facts = ctx.facts(0);
    if (!facts.shape) {
        throw ExportError("':op/slice_sub' of a tensor of unknown rank is not supported");
    }
    const int64_t rank   = static_cast<int64_t>(facts.shape->size());
    const int64_t rawAt  = ctx.constInt(1);
    const int64_t at     = rawAt < 0 ? rawAt + rank : rawAt;
    if (at < 0 || at >= rank) {
        throw ExportError(
            std::format(
                "':op/slice_sub': the axis {} is out of range for rank {}", rawAt, rank));
    }
    // A step that is not a constant would change the result's rank at run time, which no ONNX
    // Slice can do; a zero step is refused where the program is written, so it cannot be here.
    const int64_t step = ctx.has(4) ? ctx.constInt(4) : 1;
    if (step == 0) {
        throw ExportError("':op/slice_sub': a slice step must not be 0");
    }
    const int64_t written    = ctx.has(5) ? ctx.constInt(5) : 3;
    const bool wroteStart    = (written & 2) != 0;
    const bool wroteEnd      = (written & 1) != 0;
    const std::optional<int64_t> extent = (*facts.shape)[static_cast<size_t>(at)];
    const int64_t zero[] = {0}, one[] = {1}, two[] = {2};
    // The position before the first element of an axis of any extent, as ONNX names it: below
    // every extent, so adding the extent and clamping a negative step's end to -1 leaves -1.
    const int64_t beforeFirst[] = {std::numeric_limits<int64_t>::min()};
    const auto int64Cast = intAttr("to", static_cast<int64_t>(elemTypeOf(TypeCode::Int64)));

    // The extent as a one-element int64 tensor: the number now, or read from the value's shape
    // when the axis' extent is not known at export time.
    std::string extentVec;
    if (extent.has_value()) {
        const int64_t n[] = {*extent};
        extentVec         = e.int64s(n);
    } else {
        const int64_t axisIndex[] = {at};
        extentVec                 = e.node(
            "Gather",
            {e.node("Shape", {ctx.input(0)}), e.int64s(axisIndex)},
            {intAttr("axis", 0)});
    }
    // The guard's index domain is the axis' own legal range of positions, held as zeros of that
    // length: an index inside the domain gathers a zero, an index outside it reads past the end
    // of the zeros and ONNX Runtime fails where the run faults. A forward slice's bounds are
    // positions 0..extent (the end may be one past the last position, which is the axis' own
    // extent), a backward slice's start is a position the axis has (0..extent - 1) while its end
    // may be one before the first. ONNX reads a *negative* index from the end of the tensor,
    // which no Camel bound ever is, so a negative index is pushed further out of range on that
    // side instead of landing on a legal one.
    const auto boundOf = [&](size_t index, bool isStart) {
        const bool known    = ctx.arg(index).isConstant();
        const int64_t value = known ? ctx.constInt(index) : 0;
        const bool legal    = known && extent.has_value() && value >= 0 &&
                           (isStart && step < 0 ? value < *extent : value <= *extent);
        if (legal) {
            const int64_t proved[] = {value};
            return e.int64s(proved); // the write site proved it a position of the axis
        }
        const std::string length = isStart && step < 0
                                       ? extentVec
                                       : e.node("Add", {extentVec, e.int64s(one)});
        const std::string zeros = e.node("Cast", {e.node("ConstantOfShape", {length})}, {int64Cast});
        const std::string bound =
            e.node("Unsqueeze", {ctx.input(index, TypeCode::Int64), e.int64s(zero)});
        const std::string negative =
            e.node("Cast", {e.node("Less", {bound, e.int64s(zero)})}, {int64Cast});
        const std::string pushed = e.node(
            "Sub",
            {bound,
             e.node(
                 "Mul", {negative, e.node("Add", {length, e.int64s(two)})})});
        return std::string(
            e.node("Add", {bound, e.node("Gather", {zeros, pushed}, {intAttr("axis", 0)})}));
    };
    const std::string start =
        wroteStart ? boundOf(2, true)
                   : (step > 0 ? e.int64s(zero)
                               : (extent.has_value()
                                      ? e.int64s(std::vector<int64_t>{*extent - 1})
                                      : e.node("Sub", {extentVec, e.int64s(one)})));
    const std::string end = wroteEnd ? boundOf(3, false)
                                     : (step > 0 ? extentVec : e.int64s(beforeFirst));

    // The length of the slice, when the extent and both bounds are known now.
    std::optional<int64_t> length;
    if (extent.has_value() && (wroteStart ? ctx.arg(2).isConstant() : true) &&
        (wroteEnd ? ctx.arg(3).isConstant() : true)) {
        const int64_t s = wroteStart ? ctx.constInt(2) : (step > 0 ? 0 : *extent - 1);
        const int64_t n = wroteEnd ? ctx.constInt(3) : (step > 0 ? *extent : -1);
        length          = step > 0 ? (s < n ? (n - s + step - 1) / step : 0)
                                   : (s > n ? (s - n - step - 1) / (-step) : 0);
    }
    const int64_t axis[] = {at}, steps[] = {step};
    auto shape = facts.shape.value_or(tensor::StaticShape{});
    shape[static_cast<size_t>(at)] = length;
    return Value::symbolic(
        e.node("Slice", {ctx.input(0), start, end, e.int64s(axis), e.int64s(steps)}),
        facts.dtype,
        shape);
}

/// A rank-0 int64 constant: the shape `Range` reads its bounds in, and the scalar index with
/// which `Gather` reads one entry of a shape.
std::string int64Scalar(Emitter &e, int64_t value) {
    return e.operand(
        Value::constant(rtdata::toSlot<rtdata::Int64>(value), type::Type::Int64()),
        TypeCode::Int64);
}

/**
 * The reverse of the core's slice: `tangent/scatter_slice_arr(dx, like, axis, start, end, step,
 * written)` is an array of `like`'s shape whose sliced positions hold `dx` and whose other
 * positions are zero. A slice takes the positions its step names along one axis -- `start`,
 * `start + step`, ... -- so its adjoint writes each element of `dx` at the position the step
 * names for it, and ONNX says exactly that with `ScatterND`: the indices are coordinate tuples,
 * and the tuple of a gradient element is its own coordinates with the axis one replaced by the
 * position the range takes there, which is the arithmetic the core's own `parentIndexOfSlice`
 * does. `Pad` can only write a contiguous range, so it writes the positions of a step of 1 and no
 * other step's; computing the positions from the start and the step is the one reading that
 * writes every step (design principles 5.4).
 *
 * The count and every coordinate but the axis one come from `dx`'s own shape: a slice's gradient
 * is an array of the slice's shape, so its extents are the range's count and the parent's other
 * extents. That is why no bound and no parent extent has to be an export-time constant here, and
 * why a slice whose end is not written exports like any other. The start of a slice whose source
 * did not write one is the axis' first position, or its last one for a backward slice, which is
 * the position that walk begins at (5.4).
 */
Value lowerScatterSliceArr(const LowerContext &ctx) {
    Emitter &e = ctx.emitter();
    if (ctx.size() < 7) {
        throw ExportError(
            std::format(
                "':tangent/scatter_slice_arr': a scatter node has seven words (the gradient, the "
                "value the slice came from, the axis, the two bounds, the step and the mask), this "
                "one has {}",
                ctx.size()));
    }
    const auto facts = ctx.facts(1);
    if (!facts.shape) {
        throw ExportError(
            "':tangent/scatter_slice_arr' of a value of unknown rank is not supported");
    }
    const int64_t rank  = static_cast<int64_t>(facts.shape->size());
    const int64_t rawAt = ctx.constInt(2);
    const int64_t at    = rawAt < 0 ? rawAt + rank : rawAt;
    if (at < 0 || at >= rank) {
        throw ExportError(
            std::format(
                "':tangent/scatter_slice_arr': the axis {} is out of range for rank {}",
                rawAt,
                rank));
    }
    const int64_t step = ctx.constInt(5);
    if (step == 0) {
        throw ExportError("':tangent/scatter_slice_arr': a slice step must not be 0");
    }
    const bool wroteStart = (ctx.constInt(6) & 2) != 0;
    // The element type of the gradient is the element type of the value the slice came from: the
    // tangent of an array is the array of its elements' tangents (design principles 9.9).
    const std::optional<TypeCode> dtype = facts.dtype ? facts.dtype : ctx.elementDtype();

    // The array the scatter writes into: the zeros of the value's shape, in that element type.
    // One zero expanded, rather than an array of zeros written out: the shape is what the run
    // gives the value, not an export-time constant.
    const std::string shape = e.node("Shape", {ctx.input(1)});
    const std::string zeros = e.node(
        "Expand",
        {e.operand(
             Value::constant(
                 rtdata::toSlot(static_cast<rtdata::Float64>(0.0)), type::Type::Float64()),
             dtype),
         shape});

    // The first position the range takes: the one the source wrote, or, when it wrote none, the
    // axis' own first position -- its last one for a backward slice.
    std::string start = int64Scalar(e, 0);
    if (wroteStart) {
        start = ctx.input(3, TypeCode::Int64);
    } else if (step < 0) {
        start = e.node(
            "Sub",
            {e.node("Gather", {shape, int64Scalar(e, at)}, {intAttr("axis", 0)}),
             int64Scalar(e, 1)});
    }

    // The index of every element of the gradient: one column per coordinate, each column that
    // coordinate's own range broadcast along the other dimensions, with the axis' column holding
    // the position the range takes there. The last dimension of the indices picks the coordinate.
    const std::string gradientShape = e.node("Shape", {ctx.input(0)});
    std::string indices;
    for (int64_t d = 0; d < rank; ++d) {
        const std::string extent =
            e.node("Gather", {gradientShape, int64Scalar(e, d)}, {intAttr("axis", 0)});
        std::string positions = e.node("Range", {int64Scalar(e, 0), extent, int64Scalar(e, 1)});
        if (d == at) {
            positions =
                e.node("Add", {e.node("Mul", {int64Scalar(e, step), positions}), start});
        }
        std::vector<int64_t> axes;
        for (int64_t k = 0; k <= rank; ++k) {
            if (k != d) {
                axes.push_back(k);
            }
        }
        std::vector<int64_t> column(static_cast<size_t>(rank), 0);
        column[static_cast<size_t>(d)] = 1;
        const std::string term = e.node(
            "Mul", {e.node("Unsqueeze", {positions, e.int64s(axes)}), e.int64s(column)});
        indices = indices.empty() ? term : e.node("Add", {indices, term});
    }
    return Value::symbolic(
        e.node("ScatterND", {zeros, indices, ctx.input(0)}), facts.dtype, facts.shape);
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
        std::ranges::none_of(*facts.shape, [](const std::optional<int64_t> &d) { return !d; });
    if (!known) {
        return Value::symbolicIntArray(ctx.emitter().node("Shape", {ctx.input(0)}), *facts.shape);
    }
    static type::ArrayType *intArray = type::ArrayType::create(type::Type::Int64());
    auto *array = ::Array::create(core::mm::autoSpace(), intArray, facts.shape->size());
    for (size_t i = 0; i < facts.shape->size(); ++i) {
        array->set(i, static_cast<rtdata::Int64>(*(*facts.shape)[i]));
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
        if (k >= 0 && k < n && array.elements[k].has_value()) {
            // A statically known dimension folds even though the whole shape is dynamic.
            return Value::constant(
                rtdata::toSlot(static_cast<rtdata::Int64>(*array.elements[k])),
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

/// The element type a scalar comparison reads its two operands in. A literal has no element type
/// of its own: it takes the one the value it is compared with states (design principles 5.1, the
/// literal rule), so `t[0] > 0.5` is a comparison in the tensor's element type. Two values that
/// each state their own element type meet in no operator, and the export refuses that pair the
/// way the operator's own inference does.
std::optional<TypeCode> comparisonDtype(const LowerContext &ctx) {
    const std::optional<TypeCode> left = ctx.facts(0).dtype, right = ctx.facts(1).dtype;
    const bool leftLiteral  = ctx.arg(0).isConstant();
    const bool rightLiteral = ctx.arg(1).isConstant();
    if (leftLiteral != rightLiteral) {
        return leftLiteral ? right : left;
    }
    tensor::ops::refuseMixedElementTypes(left, right);
    if (auto common = tensor::ops::commonElementType(left, right)) {
        return common;
    }
    return left ? left : right;
}

/// Scalar comparisons: the two operands are read in that one element type and the result is bool.
LowerFn scalarCompare(std::string opType, bool negate) {
    return [opType = std::move(opType), negate](const LowerContext &ctx) {
        const std::optional<TypeCode> dtype = comparisonDtype(ctx);
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
    if (!facts.shape || facts.shape->size() != 1 || !(*facts.shape)[0].has_value()) {
        throw ExportError(
            std::format(
                "'{}': the channel count (parameter {}'s extent) must be known statically",
                ctx.uri(),
                index));
    }
    return *(*facts.shape)[0];
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
            if (!(*facts.shape)[i].has_value()) {
                known = false;
                break;
            }
            count *= static_cast<double>(*(*facts.shape)[i]);
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
        if (input && !input->empty() && (*input)[0].has_value()) {
            const int64_t extent = *(*input)[0];
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
    add("tensor:strict_eq", compareOp("Equal", false));
    add("tensor:strict_neq", compareOp("Equal", true));

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
    for (const char *uri : {"tensor:to_i32", "tensor:to_i64", "tensor:to_f32", "tensor:to_f64",
                            "tensor:to_bf16", "tensor:to_f16"}) {
        add(uri, lowerCast);
    }

    // Linear algebra and layout.
    add("tensor:matmul", sameTypeOp("MatMul", 2));
    add("tensor:linear", lowerLinear);
    add("tensor:transpose", lowerTranspose);
    add("tensor:permute", lowerPermute);
    add("tensor:reshape", lowerReshape, 5);
    add("tensor:flatten", lowerFlatten);
    add("tensor:unsqueeze", lowerUnsqueeze, 13);
    add("tensor:concat", lowerConcat);
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
    // The core's own slice: the node `t[a:b:s]` and `slice(t, axis, a, b, s)` both build, for
    // any rank, any axis and any element type. It is the only slice the language has, so every
    // slice in a program reaches this lowering.
    add(":op/slice_sub", lowerSliceSub, 10);
    // The same operator's adjoint, which a training step's backward pass builds (the scatter is
    // the reverse of the forward slice, not a gradient operator of ONNX). It writes the positions
    // with `ScatterND` and computes them with `Range`, opset 11 operators, whose columns are
    // broadcast with an `Unsqueeze` that takes its axes as an input (opset 13).
    add(":tangent/scatter_slice_arr", lowerScatterSliceArr, 13);
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
