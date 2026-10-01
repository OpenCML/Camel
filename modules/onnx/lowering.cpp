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

#include <algorithm>
#include <format>
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

std::optional<ConstArg> LowerContext::constArg(size_t index) const {
    return has(index) ? constArgOf(args_[index]) : std::nullopt;
}

void LowerContext::fail(size_t index, std::string_view expected) const {
    throw ExportError(std::format(
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

/// Every argument converted to the result dtype, then one node.
LowerFn sameTypeOp(std::string opType, size_t operands) {
    return [opType = std::move(opType), operands](const LowerContext &ctx) {
        std::vector<std::string> inputs;
        for (size_t i = 0; i < operands; ++i) {
            inputs.push_back(ctx.input(i, ctx.result().dtype));
        }
        return ctx.emit(opType, std::move(inputs));
    };
}

LowerFn compareOp(std::string opType, bool negate) {
    return [opType = std::move(opType), negate](const LowerContext &ctx) {
        const auto dtype = tensor::ops::promote(ctx.facts(0).dtype, ctx.facts(1).dtype);
        std::vector<std::string> inputs{ctx.input(0, dtype), ctx.input(1, dtype)};
        if (!negate) {
            return ctx.emit(opType, std::move(inputs));
        }
        const std::string eq = ctx.emitter().node(opType, std::move(inputs));
        return ctx.emit("Not", {eq});
    };
}

Value lowerRsqrt(const LowerContext &ctx) {
    const std::string sqrt = ctx.emitter().node("Sqrt", {ctx.input(0, ctx.result().dtype)});
    return ctx.emit("Reciprocal", {sqrt});
}

/// GELU with the tanh approximation (the Camel kernel's definition).
Value lowerGelu(const LowerContext &ctx) {
    const std::string x = ctx.input(0, ctx.result().dtype);
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
         ctx.input(1, ctx.result().dtype),
         ctx.input(2, ctx.result().dtype)});
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
    const auto dtype   = ctx.result().dtype;
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

/// matmul_add / matmul_add_relu (produced by tensor::fuse): MatMul, Add (, Relu). ONNX Runtime
/// re-fuses these into Gemm / FusedMatMul itself.
LowerFn matmulAddOp(bool relu) {
    return [relu](const LowerContext &ctx) {
        const auto dtype          = ctx.result().dtype;
        Emitter &e                = ctx.emitter();
        const std::string product = e.node("MatMul", {ctx.input(0, dtype), ctx.input(1, dtype)});
        if (!relu) {
            return ctx.emit("Add", {product, ctx.input(2, dtype)});
        }
        return ctx.emit("Relu", {e.node("Add", {product, ctx.input(2, dtype)})});
    };
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

Value lowerFlatten(const LowerContext &ctx) {
    return ctx.emit("Flatten", {ctx.input(0)}, {intAttr("axis", ctx.constInt(1, 1))});
}

Value lowerUnsqueeze(const LowerContext &ctx) {
    const int64_t axis[] = {ctx.constInt(1)};
    return ctx.emit("Unsqueeze", {ctx.input(0), ctx.emitter().int64s(axis)});
}

Value lowerConcat(const LowerContext &ctx) {
    const auto dtype = ctx.result().dtype;
    return ctx.emit(
        "Concat",
        {ctx.input(0, dtype), ctx.input(1, dtype)},
        {intAttr("axis", ctx.constInt(2))});
}

Value lowerSlice(const LowerContext &ctx) {
    Emitter &e             = ctx.emitter();
    const int64_t axis[]   = {ctx.constInt(1)};
    const int64_t starts[] = {ctx.constInt(2)};
    const int64_t ends[]   = {ctx.constInt(3)};
    const int64_t steps[]  = {ctx.constInt(4, 1)};
    return ctx.emit(
        "Slice",
        {ctx.input(0), e.int64s(starts), e.int64s(ends), e.int64s(axis), e.int64s(steps)});
}

/// Reduction over one axis. Opset 18 moved `axes` from an attribute to an input for every
/// reduction except ReduceSum (which moved at opset 13).
LowerFn reduceAxisOp(std::string opType) {
    return [opType = std::move(opType)](const LowerContext &ctx) {
        const int64_t axis     = ctx.constInt(1);
        const int64_t keepDims = ctx.constBool(2, false) ? 1 : 0;
        const bool axesInput   = opType == "ReduceSum" ? ctx.opset() >= 13 : ctx.opset() >= 18;
        const std::string x    = ctx.input(0, ctx.result().dtype);
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

Value lowerArgMax(const LowerContext &ctx) {
    const int64_t keepDims = ctx.constBool(2, false) ? 1 : 0;
    return ctx.emit(
        "ArgMax",
        {ctx.input(0)},
        {intAttr("axis", ctx.constInt(1)), intAttr("keepdims", keepDims)});
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
    auto *array = ::Array::create(core::mm::autoSpace(), facts.shape->size());
    for (size_t i = 0; i < facts.shape->size(); ++i) {
        array->set(i, static_cast<rtdata::Int64>((*facts.shape)[i]));
    }
    static type::ArrayType *intArray = type::ArrayType::create(type::Type::Int64());
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

/// arr[i] on an int[] that depends on the input (e.g. shape(x)[0] with a dynamic batch).
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
        throw ExportError(
            "':op/idx_arr': only int arrays (shapes) can be indexed by the model input");
    }
    return ctx.emitScalar(
        "Gather",
        {ctx.input(0, TypeCode::Int64), ctx.input(1, TypeCode::Int64)},
        {intAttr("axis", 0)});
}

/// sum(t) / mean(t) over all elements: a Camel float.
LowerFn reduceAllOp(std::string opType) {
    return [opType = std::move(opType)](const LowerContext &ctx) {
        return Value::symbolicScalar(
            ctx.emitter().node(opType, {ctx.input(0, TypeCode::Float32)}, {intAttr("keepdims", 0)}),
            type::Type::Float64());
    };
}

/// t[i] / t[i, j]: element reads producing a Camel float.
Value lowerTensorIndex(const LowerContext &ctx) {
    Emitter &e    = ctx.emitter();
    std::string v = ctx.input(0, TypeCode::Float32);
    for (size_t i = 1; i < ctx.size(); ++i) {
        v = e.node("Gather", {v, ctx.input(i, TypeCode::Int64)}, {intAttr("axis", 0)});
    }
    return Value::symbolicScalar(v, type::Type::Float64());
}

// ---------------------------------------------------------------- builtin scalars

/// Camel int / float / bool arithmetic on scalars: operands converted to the result's dtype.
LowerFn scalarOp(std::string opType, size_t operands) {
    return [opType = std::move(opType), operands](const LowerContext &ctx) {
        const TypeCode dtype = tensor::normalizeTensorDType(ctx.resultType()->code());
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
        const auto dtype = tensor::ops::promote(ctx.facts(0).dtype, ctx.facts(1).dtype);
        std::vector<std::string> inputs{ctx.input(0, dtype), ctx.input(1, dtype)};
        if (!negate) {
            return ctx.emitScalar(opType, std::move(inputs));
        }
        return ctx.emitScalar("Not", {ctx.emitter().node(opType, std::move(inputs))});
    };
}

/// Conversions between Camel scalar types (itol, ltod, dtol, ...).
Value lowerScalarCast(const LowerContext &ctx) {
    const TypeCode target = tensor::normalizeTensorDType(ctx.resultType()->code());
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
    add("tensor:matmul_add", matmulAddOp(false));
    add("tensor:matmul_add_relu", matmulAddOp(true));
    add("tensor:transpose", lowerTranspose);
    add("tensor:permute", lowerPermute);
    add("tensor:reshape", lowerReshape, 5);
    add("tensor:flatten", lowerFlatten);
    add("tensor:unsqueeze", lowerUnsqueeze, 13);
    add("tensor:concat", lowerConcat);
    add("tensor:slice", lowerSlice, 10);
    add("tensor:shape", lowerShape);
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
    add("tensor:argmax_axis", lowerArgMax);
    add("tensor:softmax", softmaxOp("Softmax"), 13);
    add("tensor:log_softmax", softmaxOp("LogSoftmax"), 13);
    add("tensor:layer_norm", lowerLayerNorm, 17);

    // Neural-network layers.
    add("nn:conv2d", lowerConv2d);
    add("nn:max_pool2d", poolOp("MaxPool"));
    add("nn:avg_pool2d", poolOp("AveragePool"));
    add("nn:batch_norm", lowerBatchNorm, 15);
    add("nn:embedding", lowerEmbedding);

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
