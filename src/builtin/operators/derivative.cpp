// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reverse-mode rules of the builtin float arithmetic (float64 `_d` and
 * float32 `_f` variants) and conversions between the float types. Integer,
 * boolean and string operators need none: their values have no tangent, so
 * no gradient ever reaches them.
 */

#include "derivative.h"

#include "camel/core/derivative.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/type.h"

#include <array>
#include <format>
#include <stdexcept>

using namespace camel::core;
using namespace camel::core::rtdata;
using camel::core::type::Type;
using camel::core::type::TypeCode;

namespace {

struct F64 {
    static Type *type() { return Type::Float64(); }
    static slot_t value(double v) { return toSlot<Float64>(v); }
    static constexpr std::string_view add = ":op/add_d";
    static constexpr std::string_view sub = ":op/sub_d";
    static constexpr std::string_view mul = ":op/mul_d";
    static constexpr std::string_view div = ":op/div_d";
    static constexpr std::string_view neg = ":op/neg_d";
    static constexpr std::string_view pow = ":op/pow_d";
};

struct F32 {
    static Type *type() { return Type::Float32(); }
    static slot_t value(double v) { return toSlot<Float32>(static_cast<Float32>(v)); }
    static constexpr std::string_view add = ":op/add_f";
    static constexpr std::string_view sub = ":op/sub_f";
    static constexpr std::string_view mul = ":op/mul_f";
    static constexpr std::string_view div = ":op/div_f";
    static constexpr std::string_view neg = ":op/neg_f";
    static constexpr std::string_view pow = ":op/pow_f";
};

template <typename F>
vjp_node_t binary(VjpBuilder &b, std::string_view uri, vjp_node_t x, vjp_node_t y) {
    const std::array<vjp_node_t, 2> inputs{x, y};
    return b.addOper(F::type(), uri, inputs);
}

template <typename F> vjp_node_t negate(VjpBuilder &b, vjp_node_t x) {
    const std::array<vjp_node_t, 1> inputs{x};
    return b.addOper(F::type(), F::neg, inputs);
}

void requireInputs(const VjpCall &call, size_t count) {
    if (call.inputs.size() != count) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects {} inputs, got {}.",
            call.uri,
            count,
            call.inputs.size()));
    }
}

template <typename F> void addVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], *dy);
        b.accumulateGradient(call.inputs[1], *dy);
    }
}

template <typename F> void subVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], *dy);
        b.accumulateGradient(call.inputs[1], negate<F>(b, *dy));
    }
}

template <typename F> void mulVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], binary<F>(b, F::mul, *dy, call.inputs[1]));
        b.accumulateGradient(call.inputs[1], binary<F>(b, F::mul, *dy, call.inputs[0]));
    }
}

// y = a / b: da = dy / b, db = -dy * y / b.
template <typename F> void divVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t denominator = call.inputs[1];
        const vjp_node_t da          = binary<F>(b, F::div, *dy, denominator);
        b.accumulateGradient(call.inputs[0], da);
        b.accumulateGradient(
            denominator,
            negate<F>(
                b,
                binary<F>(b, F::div, binary<F>(b, F::mul, *dy, call.output), denominator)));
    }
}

template <typename F> void negVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], negate<F>(b, *dy));
    }
}

// y = a ^ n: da = dy * n * a ^ (n - 1). The exponent must be a constant; its own derivative would
// need a logarithm.
template <typename F> void powVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t base     = call.inputs[0];
        const vjp_node_t exponent = call.inputs[1];
        if (!b.staticValueOf(exponent)) {
            throw std::runtime_error(
                std::format("Derivative of '{}' requires a constant exponent.", call.uri));
        }
        const vjp_node_t lowered =
            binary<F>(b, F::sub, exponent, b.addStatic(F::value(1.0), F::type()));
        const vjp_node_t slope =
            binary<F>(b, F::mul, exponent, binary<F>(b, F::pow, base, lowered));
        b.accumulateGradient(base, binary<F>(b, F::mul, *dy, slope));
    }
}

// y = xs->append<v>: dxs is the gradient of y without its last element, and dv is its last
// element. The appended value is the call's `with` input. The append is what grows an array
// round by round, so this is the rule that carries a loop state array's gradient backwards
// (design principles 9.9).
void appendArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    if (!b.gradientOf(call.output)) {
        return;
    }
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::tangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return;
    }
    const auto *tangentArray = tt::as_ptr<type::ArrayType>(tangentType);
    const auto dy            = *b.gradientOf(call.output);
    const std::array<vjp_node_t, 1> split{dy};
    b.accumulateGradient(call.inputs[0], b.addOper(tangentType, ":tangent/init_arr", split));
    if (!call.withInputs.empty()) {
        b.accumulateGradient(
            call.withInputs[0],
            b.addOper(tangentArray->elemType(), ":tangent/last_arr", split));
    }
}

void identityVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], *dy);
    }
}

// y = xs[i]: dxs = zeros_like(xs) with dy added at i. A read of one element sends its gradient
// back to that element's place in the array's gradient (design principles 9.9, the reverse of
// a gather is a scatter-add). The array's tangent is the array of its elements' tangents, so
// the zeros and the scatter are the tangent-array operators.
void idxArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (!b.gradientOf(call.output)) {
        return;
    }
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::tangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return; // an array whose elements have no tangent: the read has none either
    }
    const auto dy = b.gradientOf(call.output);
    const std::array<vjp_node_t, 1> zeroInputs{call.inputs[0]};
    const vjp_node_t zero =
        b.addOper(tangentType, ":tangent/zeros_arr", zeroInputs);
    const std::array<vjp_node_t, 3> scatterInputs{zero, call.inputs[1], *dy};
    b.accumulateGradient(
        call.inputs[0],
        b.addOper(tangentType, ":tangent/scatter_add_arr", scatterInputs));
}

// `staticInt(b, n)`: an int constant of the gradient graph, for a fixed argument a rule emits.
vjp_node_t staticInt(VjpBuilder &b, int64_t value) {
    return b.addStatic(toSlot<int64_t>(value), Type::Int64());
}

// A slice of an array — `xs->slice<start, end>` (`:struct/slice_arr`) and `xs[a:b:s]`
// (`:op/slice_sub`) — takes a range of positions, so its adjoint is an array of the parent's
// length with the upstream gradient at those positions and the zero tangent everywhere else.
// That is `:tangent/scatter_slice_arr`, the array counterpart of `tensor:slice_grad`. The
// bounds, the step and the mask are passed through unchanged, so the reverse normalizes them
// with the same code as the forward, a negative step included: it walks the positions backwards
// and its gradient is placed in that same order. The tangent of an array is the array of its
// elements' tangents (design principles 9.9), so the gradient of the result is already an array
// of that element type and its elements are placed, not converted.
void scatterSliceArray(
    VjpBuilder &b,
    const VjpCall &call,
    vjp_node_t start,
    vjp_node_t end,
    vjp_node_t step,
    vjp_node_t written) {
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::tangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return; // an array whose elements have no tangent: the slice has none either
    }
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const std::array<vjp_node_t, 6> inputs{*dy, call.inputs[0], start, end, step, written};
    b.accumulateGradient(
        call.inputs[0], b.addOper(tangentType, ":tangent/scatter_slice_arr", inputs));
}

// `xs->slice<start, end>`: the bounds are the call's two `with` inputs, and both are written.
void sliceArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    if (call.withInputs.size() != 2) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects 2 with inputs (start, end), got {}.",
            call.uri,
            call.withInputs.size()));
    }
    // The mask of a slice whose bounds are both written: bit 0 the end, bit 1 the start.
    scatterSliceArray(
        b, call, call.withInputs[0], call.withInputs[1], staticInt(b, 1), staticInt(b, 3));
}

// `xs[a:b:s]`: start, end, step and the mask are the call's last four norm inputs.
void sliceSubArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 5);
    scatterSliceArray(b, call, call.inputs[1], call.inputs[2], call.inputs[3], call.inputs[4]);
}

// A conversion between float widths: the gradient converts back.
template <typename To> void convertVjp(VjpBuilder &b, const VjpCall &call, std::string_view back) {
    requireInputs(call, 1);
    if (auto dy = b.gradientOf(call.output)) {
        const std::array<vjp_node_t, 1> inputs{*dy};
        b.accumulateGradient(call.inputs[0], b.addOper(To::type(), back, inputs));
    }
}

void dtofVjp(VjpBuilder &b, const VjpCall &call) { convertVjp<F64>(b, call, ":op/ftod"); }
void ftodVjp(VjpBuilder &b, const VjpCall &call) { convertVjp<F32>(b, call, ":op/dtof"); }

template <typename F> void registerFloatRules(DerivativeRegistry &registry) {
    registry.setRule(F::add, addVjp<F>);
    registry.setRule(F::sub, subVjp<F>);
    registry.setRule(F::mul, mulVjp<F>);
    registry.setRule(F::div, divVjp<F>);
    registry.setRule(F::neg, negVjp<F>);
    registry.setRule(F::pow, powVjp<F>);
}

template <typename F> TangentSpace floatTangentSpace() {
    TangentSpace space;
    space.addUri = std::string(F::add);
    space.zero   = F::value(0.0);
    return space;
}

} // namespace

void registerBuiltinDerivatives() {
    auto &registry = DerivativeRegistry::instance();
    registerFloatRules<F64>(registry);
    registerFloatRules<F32>(registry);
    registry.setRule(":op/dtod", identityVjp);
    registry.setRule(":op/ftof", identityVjp);
    registry.setRule(":op/dtof", dtofVjp);
    registry.setRule(":op/ftod", ftodVjp);
    registry.setRule(":op/idx_arr", idxArrayVjp);
    // A slice of an array is a range of positions, so its adjoint is the range scatter of the
    // tangent array: the gradient of the elements the slice took goes back to their places.
    registry.setRule(":struct/slice_arr", sliceArrayVjp);
    registry.setRule(":op/slice_sub", sliceSubArrayVjp);
    registry.setRule(":struct/append_arr", appendArrayVjp);
    // The form the loop-state rewrite uses (Builder::markLoopStateInPlace): the same call, so
    // the same rule - it reads the output's gradient and never the array's own value.
    registry.setRule(":struct/append_arr_owned", appendArrayVjp);
    // The private copy a rewritten loop starts from is the identity on values: its gradient is
    // the gradient of the array it copied, unchanged.
    registry.setRule(":struct/copy_arr", identityVjp);

    registry.setTangentSpace(TypeCode::Float64, floatTangentSpace<F64>());
    registry.setTangentSpace(TypeCode::Float32, floatTangentSpace<F32>());
    // Arrays have no entry of their own either: their tangent is the array of their elements'
    // tangents, and adding two of them is elementwise (design principles 9.9).
    TangentSpace arraySpace;
    arraySpace.addUri      = ":tangent/add_arr";
    arraySpace.zerosLikeUri = ":tangent/zeros_arr";
    registry.setTangentSpace(TypeCode::Array, std::move(arraySpace));
}
