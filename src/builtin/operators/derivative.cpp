/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
    registry.setRule(":struct/append_arr", appendArrayVjp);

    registry.setTangentSpace(TypeCode::Float64, floatTangentSpace<F64>());
    registry.setTangentSpace(TypeCode::Float32, floatTangentSpace<F32>());
    // Arrays have no entry of their own either: their tangent is the array of their elements'
    // tangents, and adding two of them is elementwise (design principles 9.9).
    TangentSpace arraySpace;
    arraySpace.addUri      = ":tangent/add_arr";
    arraySpace.zerosLikeUri = ":tangent/zeros_arr";
    registry.setTangentSpace(TypeCode::Array, std::move(arraySpace));
}
