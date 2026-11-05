// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reduction and normalization operators.
 *
 * `sum(t)` / `mean(t)` reduce everything to the operand's element type. The axis forms
 * (`sum(t, axis)`, `sum(t, axis, keepdims)`, ...) share the export name with
 * the full forms and are selected by arity. Softmax-family operators take an
 * optional axis that defaults to the last one.
 *
 * `max(t)` / `min(t)` / `argmax(t)` / `argmin(t)` are the whole-tensor forms of the four reductions
 * that used to have only the axis form (design principles 7.3 names them under one name; 7.7 gives
 * them the readings below). They answer an element (max/min) or the position counted in row-major
 * order (argmax/argmin, an int64), and a tensor with no element is `InvalidArgument` -- an extremum
 * has no identity, unlike `sum`, whose identity is 0 (7.7).
 *
 * Where the accumulation happens, per form (reduction and rounding are one rule,
 * design principles 7.3):
 *
 *   - `sum(t)`, `mean(t)`: accumulate exactly as the axis forms do — f64 for a floating
 *     element type (kernels/reduce.h `sumAll`), the exact int64 accumulator for an int64
 *     or bool one — and answer the element type, converting once at the end: the same
 *     element type and the same one rounding as `sum(t, axis, ...)`. A mean of an integer
 *     or bool tensor is f32, as by axis.
 *   - `sum(t, axis, ...)`, `mean(t, axis, ...)`: keep the element type (bf16, f16 and
 *     f64 keep theirs; a mean of integers or bools is f32) and accumulate in f64,
 *     rounding once when the element is stored (kernels/reduce.cpp).
 *   - `max` / `min` / `argmax` / `argmin` over an axis: no accumulator at all; they
 *     carry element values and IEEE 754-2019 maximum/minimum are exactly
 *     associative and commutative (7.7). Their rule is not this one: nothing is
 *     converted, so there is no rounding to do once, and their result is an element
 *     (max/min, over an axis) or a position (argmax/argmin, int64).
 *
 * `array::sum` in stdlib/array.cml accumulates in f64 and rounds once for the same
 * reason, so the array path and the axis form agree bit for bit.
 */

#include "../kernels/reduce.h"

#include "camel/core/error/runtime.h"
#include "camel/core/type/composite/array.h"
#include "catalog.h"
#include "support.h"
#include <array>
#include <format>

namespace camel::tensor::ops {

using namespace camel::core::type;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
namespace k = camel::tensor::kernels;

/// The element type a reduction answers, written once: the whole-tensor form and the axis form
/// both read this rule (`wholeReductionDtype` and `reduceAxisInfer`), because two copies of it is
/// how the two came to disagree about the mean of an integer. It is the kernel's own rule
/// (`modules/tensor/kernels/reduce.cpp`), stated once: `argmax` and `argmin` answer the position,
/// an `int64`; a bool `sum` and a bool `mean` answer the `int64` count, which is the one width a
/// count has (design principles 5.1, 7.7) -- and no call reaches either, since `is_numeric`
/// excludes `bool`; every other pair keeps the element type, `mean` included: `mean` answers `T`
/// (design principles), so `mean([1, 2, 4])` is `2`.
TypeCode reductionDtype(k::ReduceOp op, std::optional<TypeCode> dtype) {
    if (!dtype) {
        // The operand states no element type (the bare `Tensor` spelling). Such a value reads as
        // `float` by the element-type rule (an unstated element type takes the array and literal
        // rule, f64), so the answer is `float` -- the width this reduction answered with while its
        // result type was `float`, which is what a caller of an unstated element type reads.
        return TypeCode::Float64;
    }
    switch (*dtype) {
    case TypeCode::Bool:
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::BFloat16:
    case TypeCode::Float16:
        if (op == k::ReduceOp::ArgMax || op == k::ReduceOp::ArgMin) {
            return TypeCode::Int64;
        }
        if (*dtype == TypeCode::Bool && (op == k::ReduceOp::Sum || op == k::ReduceOp::Mean)) {
            return TypeCode::Int64;
        }
        // `max_axis` and `min_axis` keep a bool: an extreme of bools is one of them.
        return *dtype;
    default:
        // No other element type is one this reduction answers for.
        return TypeCode::Float64;
    }
}

namespace {

bool keepDimsArg(ArgsView &norm, size_t index) {
    return hasArg(norm, index) && boolArg(norm, index);
}

template <k::ReduceOp Op> slot_t reduceAxisKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("reduce", [&] {
        return wrap(
            norm.resultType(),
            k::reduceAxis(
                Op,
                tensorArg(norm, 0),
                intArg(norm, 1),
                keepDimsArg(norm, 2),
                resultAllocator()));
    });
}

/// The result of an axis reduction: the operand's element type (the reduction's own rule) at the
/// extents `reduceShape` keeps. The shape follows from the operand's type and from the values of
/// `axis` and `keepdims`, so it is stated whenever those are known -- and the result states less
/// when they are not, by the same rule rather than by a promise: an axis that is a value of the
/// run leaves the rank but not the extents (`T[?]`), and a `keepdims` that is a value of the run
/// leaves the rank open (`T[..]`). None of the three is guessed.
///
/// This is the signature a program reads: the axis reductions are computed rather than declared
/// (`modules/tensor/module.cpp#kDeclaredOperators`), because a declaration's result is a type and
/// these extents are a function of two values.
InferFn reduceAxisInfer(k::ReduceOp op) {
    return [op](const InferContext &ctx) -> std::optional<Type *> {
        const TensorFacts in          = ctx.facts(0);
        const bool keepDims           = ctx.has(2) ? ctx.constBool(2).value_or(false) : false;
        std::optional<TypeCode> dtype = reductionDtype(op, in.dtype);
        // Without a constant keepdims flag the rank is only known when it cannot change.
        if (ctx.has(2) && !ctx.constBool(2)) {
            return tensorOf(dtype, std::nullopt);
        }
        return tensorOf(dtype, reduceShape(in.shape, ctx.constInt(1), keepDims));
    };
}

template <k::ReduceOp Op>
OpDef reduceAxisDef(
    std::string_view name, std::string_view exportName, std::string_view resultDoc) {
    OpDef def{
        .name = name,
        // The name a program writes: the whole reduction's (`sum`, `mean`, ...). The axis form is
        // the same name with the axis and the flag in angle brackets (design principles 7.3), so
        // the algebraic spelling is the only exported name the operator has; `name` stays the URI
        // suffix, which is an identity of the implementation and not a spelling of a call.
        .exports = {exportName},
        .params =
            {{"t", ParamKind::Tensor},
             {"axis", ParamKind::Int},
             {"keepdims", ParamKind::Bool, true}},
        .resultDoc = resultDoc,
        .infer     = reduceAxisInfer(Op),
        .kernel    = &positionally<reduceAxisKernel<Op>>,
        .traits    = {},
        // One call spelling: `t` in the parentheses, the axis and the flag in angle brackets
        // (`mean<0>(xs)`, `argmin<0, true>(rows)`). `ordinaryParams` is one, so
        // `OpDefResolver::resolveForCall` refuses a call that writes the axis in the parentheses --
        // that spelling named a second operator (`mean_axis(t, axis, keepdims)`), and the author's
        // ruling of 2026-11-05 removed it: a reduction over an axis has one name, the whole
        // reduction's. `withAfterNorm` joins the angle-bracketed parameters back into the kernel's
        // one list.
        //
        // The extents are `reduceAxisInfer`'s answer. They have to be: a declaration cannot state
        // them at all -- they follow from the operand's type *and* from the values of `axis` and
        // `keepdims` (design principles 5.4).
        .withAfterNorm = true,
    };
    def.ordinaryParams = 1;
    return def;
}

/// The whole-tensor form's element type: `reductionDtype`, the one rule it shares with the axis
/// form (`reduceAxisInfer`), so the full form and the axis form agree on the element type the way
/// they agree on the accumulation.
TypeCode wholeReductionDtype(k::ReduceOp op, std::optional<TypeCode> dtype) {
    return reductionDtype(op, dtype);
}

/// The accumulator converted into the result's element type: the one rounding of the
/// whole-tensor reduction (design principles 7.3), the same rounding the axis form stores with.
/// A bf16 or f16 result is the f32 the conversion gives brought to that type once.
slot_t elementTypedSlot(TypeCode result, double value) {
    switch (result) {
    case TypeCode::Float32:
        return camel::core::rtdata::toSlot(static_cast<float>(value));
    case TypeCode::BFloat16:
        return camel::core::rtdata::halfFromValue<camel::core::rtdata::BFloat16>(value);
    case TypeCode::Float16:
        return camel::core::rtdata::halfFromValue<camel::core::rtdata::Float16>(value);
    default:
        return camel::core::rtdata::toSlot(value);
    }
}

/// The element type the result slot is written as: the type the node itself carries, which is the
/// fact the infer and the kernel both read. A node that states no type, or states an array where
/// the whole-tensor reduction answers one element, is written as the f64 answer this operator
/// gave while its result type was `float`.
TypeCode resultElementCode(const Type *result) {
    if (result == nullptr || result->code() == TypeCode::Array) {
        return TypeCode::Float64;
    }
    return result->code();
}

slot_t sumAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    Array *t              = tensorArg(norm, 0);
    const TypeCode result = resultElementCode(norm.resultType());
    return runKernel("sum", [&] {
        // An integer result is the exact int64 accumulator (5.1, 7.7): int64 as it stands, and
        // int32 truncated into its own element type, where truncating once gives the same bits as
        // wrapping at every step — what the axis form stores. Every float element type takes the
        // f64 accumulator converted once.
        if (result == TypeCode::Int64) {
            return camel::core::rtdata::toSlot(k::sumAllInt64(t));
        }
        if (result == TypeCode::Int32) {
            return camel::core::rtdata::toSlot(static_cast<int32_t>(k::sumAllInt64(t)));
        }
        return elementTypedSlot(result, k::sumAll(t));
    });
}

slot_t meanAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    Array *t = tensorArg(norm, 0);
    // The definition of `mean` is sum(x) / (numel(x) as float) (stdlib/tensor_definitions.cml),
    // and the canonical form of the element type T is T(sum(float(x)) / float(numel(x))): the
    // sum and the division both happen in f64 and the result is rounded into T once, which is
    // what this kernel computes. For a tensor without elements that float division by zero
    // faults, so the kernel raises the same fault (design principles 7.1: a kernel agrees with
    // its definition, faults included). Raised outside runKernel, which would turn it into a
    // kernel error.
    if (t->numel() == 0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }
    const TypeCode result = resultElementCode(norm.resultType());
    return runKernel("mean", [&] {
        // The accumulation and the division are in f64, and the conversion into the element type
        // is the only rounding of the whole reduction — never a sum rounded to the element type
        // and then divided again. An integer element type takes the conversion `int(·)` already
        // has (toward zero): `mean([1, 2, 4])` is `int(7 / 3) = 2`, which is the normative form
        // (modules/math/mean.h), not a lost average.
        const double mean = k::sumAll(t) / static_cast<double>(t->numel());
        if (result == TypeCode::Int64) {
            return camel::core::rtdata::toSlot(static_cast<int64_t>(mean));
        }
        if (result == TypeCode::Int32) {
            return camel::core::rtdata::toSlot(static_cast<int32_t>(mean));
        }
        return elementTypedSlot(result, mean);
    });
}

/// The whole-tensor form's result type, from the operand's element type.
InferFn wholeReductionInfer(k::ReduceOp op) {
    return [op](const InferContext &ctx) -> std::optional<Type *> {
        return Type::create(wholeReductionDtype(op, ctx.facts(0).dtype));
    };
}

// ---- the reductions with their extents stated ----
//
// A reduction forgets the extents it reduced, and the reverse of a reduction puts them back: the
// adjoint of `sum(t)` spreads one number over `shape(t)`, and the adjoint of `mean(t, axis, ...)`
// spreads each lane's mean over the lane. Neither can be built from the operand on the reverse
// side -- a transpose law reads only **known** operands, and the operand a cotangent comes back
// through is not one: it has no value there (author, 2026-11-03 11:10, 17:35) -- and `T[..]`
// carries no extents in its type (design principles 5.4). So the forward half of a reduction states
// them, and the two operators below are what it states them to.
//
// Both are second spellings of the operator above them for the reason `tensor:pick_at` is a
// second spelling of `:op/idx_arr` (`modules/tensor/ops/layout_ops.cpp#elementAtKernel`): a law's
// parameters are bound one per operand, and `:op/idx_arr`'s key is filed at two operands in the
// core, so a third value cannot ride on it. The extents are operands of their own here -- the walk
// answers an operator with a third *parameter*; what it refuses is a third *tensor* (measured
// 2026-11-04 on `take_stated`, two `int[]` beside the tensor, both legs answered).
// `rulesets.jvp`'s `jvp_sum` and `jvp_mean_axis` state them, and `rulesets.transpose`'s
// `sum_stated_t` and `mean_axis_stated_t` read them and nothing of the operand.

/// `sum_stated(t, extents)`: the whole-tensor sum, with the operand's extents beside it.
///
/// The value is `sum(t)` -- `sumAllKernel` below is the same kernel, so the accumulation, the
/// element type and the one rounding are the definition's own -- and `extents` is checked against
/// the operand's shape: an extent nobody verified would be a gradient of the wrong shape
/// (`support.h#checkStatedExtents`).
slot_t sumStatedKernel(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    const std::vector<int64_t> extents = intListArg(norm, 1, "sum_stated");
    checkStatedExtents(tensorArg(norm, 0), extents, 0, "sum_stated");
    return sumAllKernel(with, norm, ctx);
}

std::optional<Type *> sumStatedInfer(const InferContext &ctx) {
    return Type::create(wholeReductionDtype(k::ReduceOp::Sum, ctx.facts(0).dtype));
}

/// `mean_axis_stated(t, axis, keepdims, extents)`: the lane mean, with the axis, the flag and the
/// operand's extents stated as parameters of their own types.
///
/// The value is `mean_axis(t, axis, keepdims)` -- `k::reduceAxis` is the kernel the axis form runs,
/// so the lane's mean and its one rounding are the definition's own -- and `extents` is checked
/// against the operand's shape. The reverse law needs the *shape* (`numel(t) / numel(ct)` is the
/// lane length, and the answer is `full(shape, 1) * (ct / lanes)`) and the value needs the axis, so
/// the three facts are read where they are stated instead of being counted out of a list.
slot_t meanAxisStatedKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *t                     = tensorArg(norm, 0);
    const int64_t axis                 = intArg(norm, 1);
    const bool keepDims                = boolArg(norm, 2);
    const std::vector<int64_t> extents = intListArg(norm, 3, "mean_axis_stated");
    checkStatedExtents(t, extents, 0, "mean_axis_stated");
    return runKernel("mean_axis_stated", [&] {
        return wrap(
            norm.resultType(),
            k::reduceAxis(k::ReduceOp::Mean, t, axis, keepDims, resultAllocator()));
    });
}

/// The stated lane mean's result: the axis form's own element type at the extents the call states.
///
/// The three parameters carry the values `reduceAxisInfer` reads as arguments -- `axis`, `keepdims`
/// and the operand's `extents` -- and each is read by name now, not by its place in a list. That is
/// why this name is **computed** rather than declared
/// (`modules/tensor/module.cpp#kDeclaredOperators`): a declaration's result is a type, and these
/// extents are a function of three values, so the only type a declaration could write is `T[..]`.
///
/// When a call states all three, the extents are known and the answer is stated with them: `axis
/// 0`, no `keepdims`, over a rank-1 operand of four elements answers the element itself (design
/// principles 5.4), and the graph reads `float` -- not `float[..]` and a conversion. That is the
/// difference between a result somebody stated and a result nobody did: with the declaration this
/// name had, every call read `float[..]`, and the cotangent the reverse law was handed had no rank
/// while its type said array (`numel(ct)` in `stdlib/rulesets/transpose.cml#mean_axis_cotangent`
/// read a number's bits as a pointer,
/// docs/handover/questions/2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md).
///
/// A call whose parameters are **values of the run** has stated nothing, and this inference says
/// exactly that: the operand's element type at a rank nobody fixed (`T[..]`). It does not refuse,
/// because the two call sites that exist -- `stdlib/rulesets/jvp.cml#jvp_mean_axis` and its tangent
/// call -- pass `shape(xs)`, whose extents are the primal's and therefore a value; refusing would
/// turn a legal program away for a shape its own operand has. Measured on that tree: both sites
/// report the extents as a value, and the reverse walk used to
/// stop on a SIGSEGV in `numelKernel`, because the cotangent a transposition is handed is typed
/// from this node's type (`modules/transforms/transpose.cpp`, `cotangentTypeOf`) and a rank-0
/// answer under an array type has no conversion to make. The repair is not here and not in the walk
/// -- a slot whose type already claims a rank has nothing to convert -- but in the law that reads
/// the cotangent: it states the cotangent as the element
/// (`stdlib/rulesets/transpose.cml#mean_axis_stated_t`), so the language's own `T` -> `T[..]`
/// conversion at its next call argument boxes the value and `numel(ct)` answers 1. The rank-0
/// program answers `[0.25, 0.25, 0.25, 0.25]` instead of dying
/// (docs/handover/questions/2026-11-04-who-states-the-extents-of-a-stated-reduction.cn.md).
std::optional<Type *> meanAxisStatedInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    const TypeCode dtype = reductionDtype(k::ReduceOp::Mean, in.dtype);
    const auto axis      = ctx.constInt(1);
    const auto keepDims  = ctx.constBool(2);
    const auto extents   = ctx.constInts(3);
    if (!axis.has_value() || !keepDims.has_value() || !extents.has_value()) {
        return tensorOf(dtype, std::nullopt);
    }
    const StaticShape stated(extents->begin(), extents->end());
    // The extents a stated reduction names are the operand's own, and a list that names another
    // number of them is refused here rather than at the run: `support.h#checkStatedExtents` makes
    // the same comparison when the value is known, and a program that writes its extents is the
    // one case where the comparison can be made before it runs.
    if (in.shape && in.shape->size() != stated.size()) {
        throw ShapeError(
            std::format(
                "mean_axis_stated states {} extent(s) and the operand has rank {}",
                stated.size(),
                in.shape->size()));
    }
    return tensorOf(dtype, reduceShape(stated, *axis, *keepDims));
}

/// `sum_axis_stated(t, axis, keepdims, extents)`: the lane sum, with the three facts its adjoint
/// reads stated as parameters of their own types.
///
/// The value is `sum_axis(t, axis, keepdims)` -- `k::reduceAxis` is the kernel the axis form runs,
/// so the accumulation, the element type and the one rounding are the definition's own -- and
/// `extents` is checked against the operand's shape (`support.h#checkStatedExtents`). It exists
/// beside `mean_axis_stated` for the same reason: the adjoint of a lane reduction spreads each
/// lane's cotangent back over the lane, and the transpose side has no operand to read that shape
/// from.
slot_t sumAxisStatedKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *t                     = tensorArg(norm, 0);
    const int64_t axis                 = intArg(norm, 1);
    const bool keepDims                = boolArg(norm, 2);
    const std::vector<int64_t> extents = intListArg(norm, 3, "sum_axis_stated");
    checkStatedExtents(t, extents, 0, "sum_axis_stated");
    return runKernel("sum_axis_stated", [&] {
        return wrap(
            norm.resultType(),
            k::reduceAxis(k::ReduceOp::Sum, t, axis, keepDims, resultAllocator()));
    });
}

/// The stated lane sum's result: `sum_axis`'s element type at the rank the axis and the flag
/// decide, and both are values of the run in this spelling, so the rank stays open.
std::optional<Type *> sumAxisStatedInfer(const InferContext &ctx) {
    return tensorOf(reductionDtype(k::ReduceOp::Sum, ctx.facts(0).dtype), std::nullopt);
}

/// `max_axis_stated(t, mask, axis, keepdims, extents)` / `min_axis_stated(t, mask, axis, keepdims,
/// extents)`: the lane extremum, with the tie selection, the axis, the flag and the extents stated.
///
/// The value is `max_axis(t, axis, keepdims)` -- `k::reduceAxis` again, so the element type and the
/// comparison are the axis form's own -- and the three stated values are parameters of their own
/// types, checked like the lane sum's. `mask` is not read for the value: it is the *selection* the
/// reverse law needs, and an extremum's adjoint is not a spread but "the whole cotangent for every
/// position that holds the extremum" (`extremumAxisVjp` answers exactly that), so the forward half
/// carries `t >= spread(output)` beside the linear call. A mask of another shape is answered where
/// it is used: the reverse law's `where` broadcasts it against the stated extents.
template <k::ReduceOp Op>
slot_t extremumAxisStatedKernel(ArgsView &, ArgsView &norm, context::Context &) {
    constexpr std::string_view name =
        Op == k::ReduceOp::Max ? "max_axis_stated" : "min_axis_stated";
    const Array *t                     = tensorArg(norm, 0);
    const int64_t axis                 = intArg(norm, 2);
    const bool keepDims                = boolArg(norm, 3);
    const std::vector<int64_t> extents = intListArg(norm, 4, name);
    checkStatedExtents(t, extents, 0, name);
    return runKernel(name, [&] {
        return wrap(norm.resultType(), k::reduceAxis(Op, t, axis, keepDims, resultAllocator()));
    });
}

/// The stated extremum's result, for the reason the stated lane sum's is open: the two values of
/// the list it reads decide the rank after the call is built.
template <k::ReduceOp Op> InferFn extremumAxisStatedInfer() {
    return [](const InferContext &ctx) -> std::optional<Type *> {
        return tensorOf(reductionDtype(Op, ctx.facts(0).dtype), std::nullopt);
    };
}

/// `mean_stated(t, at)`: the whole-tensor mean, with `at` the operand's extents.
///
/// The value is `mean(t)` -- `meanAllKernel` below is the same kernel, so the accumulation in
/// `f64` and the one rounding are the definition's own -- and `at` is checked against the
/// operand's shape. `mean` is the whole-tensor twin `sum_stated` already has: its adjoint divides
/// the cotangent by the number of elements, and that count is a value of the primal, so the
/// forward half states it as the extents the reverse law multiplies out.
slot_t meanStatedKernel(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    const std::vector<int64_t> extents = intListArg(norm, 1, "mean_stated");
    checkStatedExtents(tensorArg(norm, 0), extents, 0, "mean_stated");
    return meanAllKernel(with, norm, ctx);
}

std::optional<Type *> meanStatedInfer(const InferContext &ctx) {
    return Type::create(wholeReductionDtype(k::ReduceOp::Mean, ctx.facts(0).dtype));
}

/// The name a whole-tensor extremum reports its fault under: design principles 7.7 gives the
/// operator whose identity does not exist the fault, so the reading says which of the four was
/// asked about a tensor with no element.
const char *wholeExtremumName(k::ReduceOp op) {
    switch (op) {
    case k::ReduceOp::Max:
        return "max";
    case k::ReduceOp::Min:
        return "min";
    case k::ReduceOp::ArgMax:
        return "argmax";
    default:
        return "argmin";
    }
}

/// `max(t)`, `min(t)`, `argmax(t)`, `argmin(t)`: the same four reductions the axis forms are, over
/// every element, and chosen by arity (design principles 7.3 names them under one name). They carry
/// an element and never round (7.7), so no accumulation type is chosen here; and since an extremum
/// has no identity, a tensor with no element is a fault, not an answer -- `InvalidArgument`, raised
/// outside `runKernel`, which would turn it into a kernel error (7.1: a kernel agrees with its
/// definition, faults included).
template <k::ReduceOp Op> slot_t extremumAllKernel(ArgsView &, ArgsView &norm, context::Context &) {
    Array *t           = tensorArg(norm, 0);
    const char *name   = wholeExtremumName(Op);
    const bool maximum = Op == k::ReduceOp::Max || Op == k::ReduceOp::ArgMax;
    if (t->numel() == 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            name,
            std::string("a tensor with no element has no ") + (maximum ? "maximum" : "minimum"));
    }
    const TypeCode result = resultElementCode(norm.resultType());
    return runKernel(name, [&]() -> slot_t {
        if (Op == k::ReduceOp::ArgMax || Op == k::ReduceOp::ArgMin) {
            // A position, counted in row-major order: an int64, the one width a position has.
            return camel::core::rtdata::toSlot(k::argExtremumAll(Op, t));
        }
        if (result == TypeCode::Int32) {
            return camel::core::rtdata::toSlot(static_cast<int32_t>(k::extremumAllInt(Op, t)));
        }
        if (result == TypeCode::Bool) {
            // An extreme of bools is one of them (as by axis); no call reaches it, `is_numeric`
            // excludes bool, but a declaration that states `T` must still be answered.
            return camel::core::rtdata::toSlot(k::extremumAllInt(Op, t) != 0);
        }
        if (result == TypeCode::Int64) {
            return camel::core::rtdata::toSlot(k::extremumAllInt(Op, t));
        }
        return elementTypedSlot(result, k::extremumAllFloat(Op, t));
    });
}

int64_t axisOrLast(ArgsView &norm, size_t index) {
    return hasArg(norm, index) ? intArg(norm, index) : -1;
}

slot_t softmaxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax", [&] {
        return wrap(
            norm.resultType(),
            k::softmax(tensorArg(norm, 0), axisOrLast(norm, 1), resultAllocator()));
    });
}

slot_t logSoftmaxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("log_softmax", [&] {
        return wrap(
            norm.resultType(),
            k::logSoftmax(tensorArg(norm, 0), axisOrLast(norm, 1), resultAllocator()));
    });
}

slot_t softmaxGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("softmax_grad", [&] {
        return wrap(
            norm.resultType(),
            k::softmaxGrad(
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                axisOrLast(norm, 2),
                resultAllocator()));
    });
}

std::optional<Type *> floatLikeFirst(const InferContext &ctx) {
    const auto dtype = ctx.facts(0).dtype;
    return tensorOf(
        dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                   : std::optional(TypeCode::Float32),
        ctx.facts(0).shape);
}

/// softmax(t, axis = -1), log_softmax: the axis must exist.
std::optional<Type *> softmaxInfer(const InferContext &ctx) {
    const auto shape = ctx.facts(0).shape;
    const auto dtype = ctx.facts(0).dtype;
    const auto axis  = ctx.has(1) ? ctx.constInt(1) : std::optional<int64_t>(-1);
    if (shape && axis) {
        const auto rank = static_cast<int64_t>(shape->size());
        if (*axis < -rank || *axis >= rank) {
            throw ShapeError(
                std::format(
                    "axis {} is out of range for the rank-{} input {}",
                    *axis,
                    rank,
                    formatShape(*shape)));
        }
    }
    return tensorOf(
        dtype == TypeCode::Float64 ? std::optional(TypeCode::Float64)
                                   : std::optional(TypeCode::Float32),
        shape);
}

/// layer_norm(x, gamma, beta): gamma and beta cover the input's trailing dimensions.
std::optional<Type *> layerNormInfer(const InferContext &ctx) {
    const auto x = ctx.facts(0).shape;
    for (size_t i : {size_t{1}, size_t{2}}) {
        const auto p = ctx.facts(i).shape;
        if (!x || !p) {
            continue;
        }
        bool fits = p->size() <= x->size();
        for (size_t d = 0; fits && d < p->size(); ++d) {
            const std::optional<int64_t> want = (*x)[x->size() - p->size() + d], have = (*p)[d];
            fits = !want.has_value() || !have.has_value() || want == have;
        }
        if (!fits) {
            throw ShapeError(
                std::format(
                    "{} {} does not match the trailing dimensions of the input {}",
                    i == 1 ? "gamma" : "beta",
                    formatShape(*p),
                    formatShape(*x)));
        }
    }
    // The element type follows the input's, and the shape does not decide it. The previous form
    // gated the dtype on `x` (the *shape*) being known, so a `float[..]` argument -- shape unknown,
    // dtype f64 -- was declared `f32`: a normalisation in a `float` model then handed an f32 value
    // to the next product and the program stopped at `(f32[..], float[..], ...)`. `floatLikeFirst`
    // is what the neighbouring `softmax_grad` already uses, and it reads the dtype alone.
    return floatLikeFirst(ctx);
}

slot_t layerNormKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("layer_norm", [&] {
        const double eps = hasArg(norm, 3) ? numberArg(norm, 3) : 1e-5;
        return wrap(
            norm.resultType(),
            k::layerNorm(
                tensorArg(norm, 0),
                tensorArg(norm, 1),
                tensorArg(norm, 2),
                eps,
                resultAllocator()));
    });
}

// ---------------------------------------------------------------- VJP rules

void sumVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:broadcast_like", {*dy, call.inputs[0]}));
    }
}

// sum_stated(t, at): the whole-tensor sum, the stated spelling of it (`sumStatedKernel` below).
//
// The adjoint is `sum`'s, because the *value* is `sum`'s -- `sumStatedKernel` runs the same kernel
// -- and a pullback rule is handed the operand, so the shape the spread needs is the operand's
// own. What the stated list answers on the *library's* side is a shape a transpose law does not
// have: `stdlib/rulesets/transpose.cml#sum_stated_t` spreads over `at`'s extents because a
// transpose law reads only known operands and the tensor a cotangent comes back through has no
// value there. Both answer the same array, which is the one line
// `test/cases/compile/transforms/stated_sum_answered_by_the_engine.cml` prints. So `at` carries
// nothing this rule needs: it is read nowhere here, because the array whose shape the spread is
// shaped by is an operand of this rule and not a number inside a list.
void sumStatedVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:broadcast_like", {*dy, call.inputs[0]}));
    }
}

// max / min over every element: the same rule as the axis forms, which send the gradient to the
// elements equal to the extremum (the selection is a bool mask and a mask is not a number, 5.1, so
// the conversion the multiplication needs is written and `cast_like` puts the mask at the
// gradient's element type). What differs from the axis form is the spread: the extremum is one
// value and the gradient is one value, so both are broadcast over the tensor rather than expanded
// along an axis. `strict_eq` is the same comparison the axis rule selects with, so a tie shares the
// gradient the same way, and a NaN extremum selects nothing -- as by axis.
void extremumAllVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t x      = call.inputs[0];
        const vjp_node_t spread = addTensorOperFor(b, x, "tensor:broadcast_like", {*dy, x});
        const vjp_node_t extreme =
            addTensorOperFor(b, x, "tensor:broadcast_like", {call.output, x});
        const vjp_node_t selected = addTensorOper(b, "tensor:strict_eq", {x, extreme});
        const vjp_node_t share    = addTensorOperFor(b, x, "tensor:cast_like", {selected, spread});
        b.accumulateGradient(x, addTensorOper(b, "tensor:multiply", {share, spread}));
    }
}

void meanVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t x = call.inputs[0];
        // dy is a scalar of the mean's element type (design principles 7.3): an f32 mean's
        // pullback shares the gradient out in f32, not in the f64 the count is written in. The
        // count is narrowed to dy's width and the division uses that width's operator, so an f32
        // mean does not divide an f32 gradient by an f64 count as if it were f64.
        const vjp_node_t count =
            addFloatOper(b, ":op/ltod", {b.addOper(Type::Int64(), "tensor:numel", std::array{x})});
        const vjp_node_t atWidth = floatGradientAtWidth(b, *dy, count);
        const type::Type *dyType = b.nodeType(*dy);
        const bool narrow        = dyType != nullptr && dyType->code() == type::TypeCode::Float32;
        const vjp_node_t share   = narrow ? b.addOper(
                                                type::Type::Float32(),
                                                ":op/div_f",
                                                std::array<vjp_node_t, 2>{*dy, atWidth})
                                          : addFloatOper(b, ":op/div_d", {*dy, atWidth});
        b.accumulateGradient(x, addTensorOperFor(b, x, "tensor:broadcast_like", {share, x}));
    }
}

/// dy of a reduction over an axis, re-expanded to the input's shape.
vjp_node_t expandReduced(VjpBuilder &b, const VjpCall &call, vjp_node_t dy) {
    const vjp_node_t keepDims = call.inputs.size() > 2 ? call.inputs[2] : staticBool(b, false);
    return addTensorOperFor(
        b,
        call.inputs[0],
        "tensor:expand_axis",
        {dy, call.inputs[0], call.inputs[1], keepDims});
}

void sumAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], expandReduced(b, call, *dy));
    }
}

void meanAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t extent = addFloatOper(
            b,
            ":op/ltod",
            {b.addOper(Type::Int64(), "tensor:dim", std::array{call.inputs[0], call.inputs[1]})});
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:divide", {expandReduced(b, call, *dy), extent}));
    }
}

// mean_axis_stated(t, axis, keepdims, extents): the lane mean, with the axis, the flag and the
// extents stated as parameters of their own (`meanAxisStatedKernel` above).
//
// The adjoint is `mean_axis`'s: spread the cotangent back over the lane (`tensor:expand_axis`) and
// divide by the lane's length (`tensor:dim`). The axis and the flag are operands this rule is
// handed, so they reach `expand_axis` as they were stated -- the flag as a real `bool`, which is
// what the parameter's type says it is, and no longer an integer read out of a list and compared
// against zero. A slot read at the wrong width answers a lane count of nonsense, which is the one
// way this rule could be wrong silently; a typed parameter cannot be read at the wrong width.
//
// The extents are read nowhere here, for the reason `sumStatedVjp` gives: the operand is at hand,
// so `tensor:dim` takes the lane's length from it, and the stated extents are what the library's
// law reads in the same position (`stdlib/rulesets/transpose.cml#mean_axis_stated_t`, whose
// `mean_axis_cotangent` needs `numel(t) / numel(ct)`).
void meanAxisStatedVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 4, 4);
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t x        = call.inputs[0];
    const vjp_node_t axis     = call.inputs[1];
    const vjp_node_t keepDims = call.inputs[2];
    const vjp_node_t expanded =
        addTensorOperFor(b, x, "tensor:expand_axis", {*dy, x, axis, keepDims});
    const std::array<vjp_node_t, 2> dimInputs{x, axis};
    const vjp_node_t extent =
        addFloatOper(b, ":op/ltod", {b.addOper(Type::Int64(), "tensor:dim", dimInputs)});
    b.accumulateGradient(x, addTensorOper(b, "tensor:divide", {expanded, extent}));
}

// max / min over an axis: the gradient goes to the elements equal to the extremum. The selection
// is a bool mask and a mask is not a number (design principles 5.1): the conversion the
// multiplication needs is written, and `cast_like` puts the mask at the gradient's element type,
// which is what the rule can name - it serves f32 and f64 models alike.
void extremumAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t expanded = expandReduced(b, call, *dy);
        const vjp_node_t selected = addTensorOper(
            b,
            "tensor:strict_eq",
            {call.inputs[0], expandReduced(b, call, call.output)});
        const vjp_node_t share =
            addTensorOperFor(b, expanded, "tensor:cast_like", {selected, expanded});
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:multiply", {share, expanded}));
    }
}

void softmaxVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 2);
    if (auto dy = b.gradientOf(call.output)) {
        std::vector<vjp_node_t> inputs{call.output, *dy};
        if (call.inputs.size() > 1) {
            inputs.push_back(call.inputs[1]);
        }
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:softmax_grad", inputs));
    }
}

// y = log_softmax(x): dx = dy - exp(y) * sum(dy, axis, keepdims).
void logSoftmaxVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t axis = call.inputs.size() > 1 ? call.inputs[1] : staticInt(b, -1);
        const vjp_node_t total =
            addTensorOper(b, "tensor:sum_axis", {*dy, axis, staticBool(b, true)});
        const vjp_node_t probs = addTensorOper(b, "tensor:exp", {call.output});
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(
                b,
                "tensor:subtract",
                {*dy, addTensorOper(b, "tensor:multiply", {probs, total})}));
    }
}

// layer_norm(x, gamma, beta, eps?)
void layerNormVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 4);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t x = call.inputs[0], gamma = call.inputs[1];
        for (int64_t which = 0; which < 3; ++which) {
            std::vector<vjp_node_t> inputs{x, gamma, *dy, staticInt(b, which)};
            if (call.inputs.size() > 3) {
                inputs.push_back(call.inputs[3]);
            }
            b.accumulateGradient(
                call.inputs[static_cast<size_t>(which)],
                addTensorOperFor(
                    b,
                    call.inputs[static_cast<size_t>(which)],
                    "tensor:layer_norm_grad",
                    inputs));
        }
    }
}

} // namespace

std::vector<OpDef> reductionOps() {
    // What a reduction gives back is written here as the type it is, not as a sentence about one:
    // `T` is the operand's element type (`reductionDtype` keeps it, except for `argmax`/`argmin`,
    // which answer an `int64` position, and the `int64` count of a bool `sum` or `mean`), and it is
    // the variable the declaration binds (`sum::<T>(t: T[..]): T`). The reference check compares
    // this text against the declaration's result, and a sentence is a type it cannot compare, so
    // the two statements of one promise would never have been read side by side.
    std::vector<OpDef> defs;
    defs.push_back(
        OpDef{
            .name      = "sum",
            .exports   = {"sum"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "T",
            .infer     = wholeReductionInfer(k::ReduceOp::Sum),
            .kernel    = &sumAllKernel,
            .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Sum>("sum_axis", "sum", "T[...]"));
    defs.push_back(
        OpDef{
            .name      = "mean",
            .exports   = {"mean"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "T",
            .infer     = wholeReductionInfer(k::ReduceOp::Mean),
            .kernel    = &meanAllKernel,
            .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Mean>("mean_axis", "mean", "T[...]"));
    // The two stated twins of the reductions above (`sumStatedKernel`, `meanAxisStatedKernel`):
    // the same value, with the extents the reverse law needs as an operand of its own.
    defs.push_back(
        OpDef{
            .name      = "sum_stated",
            .exports   = {"sum_stated"},
            .params    = {{"t", ParamKind::Tensor}, {"extents", ParamKind::IntArray}},
            .resultDoc = "T",
            .infer     = sumStatedInfer,
            .kernel    = &sumStatedKernel,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name    = "mean_axis_stated",
            .exports = {"mean_axis_stated"},
            .params =
                {{"t", ParamKind::Tensor},
                 {"axis", ParamKind::Int},
                 {"keepdims", ParamKind::Bool},
                 {"extents", ParamKind::IntArray}},
            .resultDoc = "T[...]",
            .infer     = meanAxisStatedInfer,
            .kernel    = &meanAxisStatedKernel,
            .traits    = {}});
    // The two stated twins the lane reductions and the whole-tensor mean were missing
    // (`sumAxisStatedKernel`, `meanStatedKernel`): the same value as the operator above them, with
    // the extents the reverse law reads as a parameter beside them.
    defs.push_back(
        OpDef{
            .name    = "sum_axis_stated",
            .exports = {"sum_axis_stated"},
            .params =
                {{"t", ParamKind::Tensor},
                 {"axis", ParamKind::Int},
                 {"keepdims", ParamKind::Bool},
                 {"extents", ParamKind::IntArray}},
            .resultDoc = "T[...]",
            .infer     = sumAxisStatedInfer,
            .kernel    = &sumAxisStatedKernel,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "mean_stated",
            .exports   = {"mean_stated"},
            .params    = {{"t", ParamKind::Tensor}, {"extents", ParamKind::IntArray}},
            .resultDoc = "T",
            .infer     = meanStatedInfer,
            .kernel    = &meanStatedKernel,
            .traits    = {}});
    // The two stated extrema (`extremumAxisStatedKernel`): the value is the axis form's, and the
    // mask is the selection its adjoint needs -- an extremum sends the whole cotangent to every
    // position that holds it, which is not a spread the extents alone could rebuild.
    defs.push_back(
        OpDef{
            .name    = "max_axis_stated",
            .exports = {"max_axis_stated"},
            .params =
                {{"t", ParamKind::Tensor},
                 {"mask", ParamKind::BoolArray},
                 {"axis", ParamKind::Int},
                 {"keepdims", ParamKind::Bool},
                 {"extents", ParamKind::IntArray}},
            .resultDoc = "T[...]",
            .infer     = extremumAxisStatedInfer<k::ReduceOp::Max>(),
            .kernel    = &extremumAxisStatedKernel<k::ReduceOp::Max>,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name    = "min_axis_stated",
            .exports = {"min_axis_stated"},
            .params =
                {{"t", ParamKind::Tensor},
                 {"mask", ParamKind::BoolArray},
                 {"axis", ParamKind::Int},
                 {"keepdims", ParamKind::Bool},
                 {"extents", ParamKind::IntArray}},
            .resultDoc = "T[...]",
            .infer     = extremumAxisStatedInfer<k::ReduceOp::Min>(),
            .kernel    = &extremumAxisStatedKernel<k::ReduceOp::Min>,
            .traits    = {}});
    defs.push_back(reduceAxisDef<k::ReduceOp::Max>("max_axis", "max", "T[...]"));
    defs.push_back(reduceAxisDef<k::ReduceOp::Min>("min_axis", "min", "T[...]"));
    defs.push_back(reduceAxisDef<k::ReduceOp::ArgMax>("argmax_axis", "argmax", "int[...]"));
    defs.push_back(reduceAxisDef<k::ReduceOp::ArgMin>("argmin_axis", "argmin", "int[...]"));
    defs.push_back(
        OpDef{
            .name      = "max",
            .exports   = {"max"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "T",
            .infer     = wholeReductionInfer(k::ReduceOp::Max),
            .kernel    = &extremumAllKernel<k::ReduceOp::Max>,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "min",
            .exports   = {"min"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "T",
            .infer     = wholeReductionInfer(k::ReduceOp::Min),
            .kernel    = &extremumAllKernel<k::ReduceOp::Min>,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "argmax",
            .exports   = {"argmax"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "int",
            .infer     = wholeReductionInfer(k::ReduceOp::ArgMax),
            .kernel    = &extremumAllKernel<k::ReduceOp::ArgMax>,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "argmin",
            .exports   = {"argmin"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "int",
            .infer     = wholeReductionInfer(k::ReduceOp::ArgMin),
            .kernel    = &extremumAllKernel<k::ReduceOp::ArgMin>,
            .traits    = {}});

    defs.push_back(
        OpDef{
            .name           = "softmax",
            .exports        = {"softmax"},
            .params         = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
            .resultDoc      = "float[...]",
            .infer          = softmaxInfer,
            .kernel         = &positionally<softmaxKernel>,
            .traits         = {},
            .withAfterNorm  = true,
            .ordinaryParams = 1});
    defs.push_back(
        OpDef{
            .name           = "log_softmax",
            .exports        = {"log_softmax"},
            .params         = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
            .resultDoc      = "float[...]",
            .infer          = softmaxInfer,
            .kernel         = &positionally<logSoftmaxKernel>,
            .traits         = {},
            .withAfterNorm  = true,
            .ordinaryParams = 1});
    defs.push_back(
        OpDef{
            .name    = "softmax_grad",
            .exports = {"softmax_grad"},
            .params =
                {{"y", ParamKind::Tensor},
                 {"dy", ParamKind::Tensor},
                 {"axis", ParamKind::Int, true}},
            .resultDoc = "float[...]",
            .infer     = floatLikeFirst,
            .kernel    = &softmaxGradKernel,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name    = "layer_norm",
            .exports = {"layer_norm"},
            .params =
                {{"x", ParamKind::Tensor},
                 {"gamma", ParamKind::Tensor},
                 {"beta", ParamKind::Tensor},
                 {"eps", ParamKind::Number, true}},
            .resultDoc = "float[...]",
            .infer     = layerNormInfer,
            .kernel    = &layerNormKernel,
            .traits    = {}});
    setVjp(defs, "sum", &sumVjp);
    setVjp(defs, "mean", &meanVjp);
    setVjp(defs, "max", &extremumAllVjp);
    setVjp(defs, "min", &extremumAllVjp);
    setVjp(defs, "argmax", &camel::core::noGradient);
    setVjp(defs, "argmin", &camel::core::noGradient);
    setVjp(defs, "sum_axis", &sumAxisVjp);
    setVjp(defs, "mean_axis", &meanAxisVjp);
    setVjp(defs, "max_axis", &extremumAxisVjp);
    setVjp(defs, "min_axis", &extremumAxisVjp);
    setVjp(defs, "argmax_axis", &camel::core::noGradient);
    setVjp(defs, "argmin_axis", &camel::core::noGradient);
    setVjp(defs, "softmax", &softmaxVjp);
    setVjp(defs, "log_softmax", &logSoftmaxVjp);
    setVjp(defs, "layer_norm", &layerNormVjp);
    // The two stated reductions, beside the forms whose value they are (`sumStatedVjp`,
    // `meanAxisStatedVjp`): a program may write either spelling, and a stated call whose value is
    // an operator's is a call the reverse pass has to answer for the same reason.
    setVjp(defs, "sum_stated", &sumStatedVjp);
    setVjp(defs, "mean_axis_stated", &meanAxisStatedVjp);
    return defs;
}

} // namespace camel::tensor::ops
