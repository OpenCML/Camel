// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reverse-mode rules of the builtin float arithmetic (float64 `_d` and
 * float32 `_f` variants), conversions between the float types, and the
 * member read of an aggregate. Integer, boolean and string operators need
 * none: their values have no tangent, so no gradient ever reaches them.
 */

#include "derivative.h"

#include "array_convert.h"

#include "camel/core/derivative.h"
#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/type/base.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/utils/type.h"

#include <array>
#include <format>
#include <stdexcept>
#include <vector>

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

// The two narrow widths are floats too, and a float's tangent space is itself: a space asks them
// only for an addition and a zero. The zero does not go through `toSlot`, which has no narrow
// conversion; half precision has its own, `halfFromFloat`, and that is the one used here.
struct BF16 {
    static Type *type() { return Type::BFloat16(); }
    static slot_t value(double v) { return halfFromFloat<BFloat16>(static_cast<float>(v)); }
    static constexpr std::string_view add = ":op/add_bf";
};

struct F16 {
    static Type *type() { return Type::Float16(); }
    static slot_t value(double v) { return halfFromFloat<Float16>(static_cast<float>(v)); }
    static constexpr std::string_view add = ":op/add_f6";
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

void requireWithInputs(const VjpCall &call, size_t count) {
    if (call.withInputs.size() != count) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects {} with inputs, got {}.",
            call.uri,
            count,
            call.withInputs.size()));
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
    Type *const tangentType = camel::core::machineryTangentTypeOf(arrayType);
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

// y = xs[i], and `m[:, 0]` (design principles 5.4): the position on the axis the call names, 0 --
// the first dimension -- when it names none. dxs = zeros_like(xs) with dy added at that position.
// A read of one position sends its gradient back to that place in the array's gradient (design
// principles 9.9, the reverse of a gather is a scatter-add), which is one element for a rank-1
// array and the slab the position took -- a row of a rank-2 array -- for a deeper one. The
// array's tangent is the array of its elements' tangents, so the zeros and the scatter are the
// tangent-array operators. The axis travels with the rest of the call: it says which dimension
// the position was read on, so the scatter puts the gradient back along the same one.
void idxArrayVjp(VjpBuilder &b, const VjpCall &call) {
    if (call.inputs.size() < 2 || call.inputs.size() > 3) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects 2 or 3 inputs (value, index, axis), got {}.",
            call.uri,
            call.inputs.size()));
    }
    if (!b.gradientOf(call.output)) {
        return;
    }
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::machineryTangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return; // an array whose elements have no tangent: the read has none either
    }
    const auto dy = b.gradientOf(call.output);
    const std::array<vjp_node_t, 1> zeroInputs{call.inputs[0]};
    const vjp_node_t zero =
        b.addOper(tangentType, ":tangent/zeros_arr", zeroInputs);
    std::vector<vjp_node_t> scatterInputs{zero, call.inputs[1], *dy};
    if (call.inputs.size() > 2) {
        scatterInputs.push_back(call.inputs[2]);
    }
    b.accumulateGradient(
        call.inputs[0],
        b.addOper(tangentType, ":tangent/scatter_add_arr", scatterInputs));
}

// y = t[idx]: the positions the indices name, taken along the axis the call names, 0 -- the first
// dimension -- when it names none (design principles 5.4). The reverse of a gather is a
// scatter-add (9.9), and the tangent of an array is the array of its elements' tangents: the
// gradient of `t` is a zero array of `t`'s shape with the elements the indices named added back
// where they were taken from, so a repeated index accumulates its position's gradient. The
// indices are discrete: nothing reaches them. The axis travels with the rest of the call, so the
// scatter puts the gradient back along the dimension the take read.
void idxTakeArrayVjp(VjpBuilder &b, const VjpCall &call) {
    if (call.inputs.size() < 2 || call.inputs.size() > 3) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects 2 or 3 inputs (value, indices, axis), got {}.",
            call.uri,
            call.inputs.size()));
    }
    if (!b.gradientOf(call.output)) {
        return;
    }
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::machineryTangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return; // an array whose elements have no tangent: the take has none either
    }
    const auto dy = b.gradientOf(call.output);
    const std::array<vjp_node_t, 1> zeroInputs{call.inputs[0]};
    const vjp_node_t zero = b.addOper(tangentType, ":tangent/zeros_arr", zeroInputs);
    std::vector<vjp_node_t> scatter{zero, call.inputs[1], *dy};
    if (call.inputs.size() > 2) {
        scatter.push_back(call.inputs[2]);
    }
    b.accumulateGradient(
        call.inputs[0], b.addOper(tangentType, ":tangent/scatter_take_arr", scatter));
}

// `staticInt(b, n)`: an int constant of the gradient graph, for a fixed argument a rule emits.
vjp_node_t staticInt(VjpBuilder &b, int64_t value) {
    return b.addStatic(toSlot<int64_t>(value), Type::Int64());
}

// y = concat(xs, ys): the gradient of y is the gradient of xs followed by the gradient of ys,
// split at len(xs). Both halves are slices of the upstream gradient, so unlike append's
// init_arr/last_arr halves this rule needs no operator that grows an array with a computed
// element and composes to second order through :op/slice_sub (design principles 9.9).
void concatArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::machineryTangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return;
    }
    const std::array<vjp_node_t, 1> xs{call.inputs[0]};
    const vjp_node_t split = b.addOper(Type::Int64(), ":struct/len_arr", xs);
    const std::array<vjp_node_t, 1> upstream{*dy};
    const vjp_node_t end = b.addOper(Type::Int64(), ":struct/len_arr", upstream);
    // The mask of a slice: bit 0 the end, bit 1 the start. The prefix writes both bounds; the
    // suffix starts at len(xs) and ends at the upstream gradient's own length. A slice node's
    // second argument is the axis, and both halves are rank-1: 0.
    const std::array<vjp_node_t, 6> prefix{
        *dy, staticInt(b, 0), staticInt(b, 0), split, staticInt(b, 1), staticInt(b, 3)};
    b.accumulateGradient(
        call.inputs[0], b.addOper(tangentType, ":op/slice_sub", prefix));
    const std::array<vjp_node_t, 6> suffix{
        *dy, staticInt(b, 0), split, end, staticInt(b, 1), staticInt(b, 2)};
    b.accumulateGradient(
        call.inputs[1], b.addOper(tangentType, ":op/slice_sub", suffix));
}

// A slice of an array — the subscript `xs[a:b:s]` and the call `slice(xs, axis, a, b, s)`
// (`:op/slice_sub`, one node whichever spelling wrote it) — takes a range of positions along one axis, so its adjoint is an array of the parent's shape
// with the upstream gradient at those positions and the zero tangent everywhere else. That is
// `:tangent/scatter_slice_arr`, the scatter that places a slice's gradient. The axis, the
// bounds, the step and the mask are passed through unchanged, so the reverse reads them with the
// same code as the forward, a negative step included: it walks the positions backwards and its
// gradient is placed in that same order. The tangent of an array is the array of its elements'
// tangents (design principles 9.9), so the gradient of the result is already an array of that
// element type and its elements are placed, not converted.
void scatterSliceArray(
    VjpBuilder &b,
    const VjpCall &call,
    vjp_node_t axis,
    vjp_node_t start,
    vjp_node_t end,
    vjp_node_t step,
    vjp_node_t written) {
    Type *const arrayType   = b.nodeType(call.inputs[0]);
    Type *const tangentType = camel::core::machineryTangentTypeOf(arrayType);
    if (tangentType == nullptr) {
        return; // an array whose elements have no tangent: the slice has none either
    }
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const std::array<vjp_node_t, 7> inputs{
        *dy, call.inputs[0], axis, start, end, step, written};
    b.accumulateGradient(
        call.inputs[0], b.addOper(tangentType, ":tangent/scatter_slice_arr", inputs));
}

// `xs[a:b:s]` and `slice(xs, axis, a, b, s)`: the value, the axis, the two bounds, the step and
// the mask, in that order -- the one parameter table both spellings write (design principles 5.4:
// both spellings are one node with one adjoint rule). The axis
// travels with the rest, so the adjoint scatters along the axis the forward took the elements
// from, whatever the rank.
//
// A call of the function form that leaves the step, or the step and the mask, out is read here as
// the forward kernel reads it: step 1, mask 3 ("both bounds written", the value the declaration
// defaults them to). The subscript's lowering always writes all six words, so a node of four or
// five is a function-form call -- and the adjoint answers every call the forward rule accepts
// (`src/builtin/modules/globals.cpp`, `coreSliceFunctionResolver`), not only the six-word ones.
void sliceSubArrayVjp(VjpBuilder &b, const VjpCall &call) {
    if (call.inputs.size() < 4 || call.inputs.size() > 6) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects 4 to 6 inputs (value, axis, start, end, step, written), "
            "got {}.",
            call.uri,
            call.inputs.size()));
    }
    const vjp_node_t step =
        call.inputs.size() > 4 ? call.inputs[4] : staticInt(b, 1); // step left out: 1
    const vjp_node_t written =
        call.inputs.size() > 5 ? call.inputs[5] : staticInt(b, 3); // mask left out: both written
    scatterSliceArray(b, call, call.inputs[1], call.inputs[2], call.inputs[3], step, written);
}

// ---- the rules of a gradient graph: the second derivative of an array expression ----
//
// The tangent of an array is the array of the tangents of its elements (design principles 9.9),
// so the first-order rules above build gradient graphs out of the `:tangent/*` operators of an
// array. Differentiating such a graph meets those operators as ordinary forward nodes, and each
// one that a differentiated value passes through needs a rule of its own: these are the
// second-order rules of the array kind. They are ordinary reverse-mode rules of the tangent
// operators, written against the same builder, so they compose to any order.
//
// Two of the six tangent operators are deliberately left without a rule:
// `:tangent/init_arr` (the gradient of an array without its last element) and
// `:tangent/last_arr` (the gradient of the appended element). Their adjoints need an operator
// that takes the element to append or the zero tangent to pad with as an *ordinary* input, and
// neither exists: `append` and `extend` take their value as a `with` input, which a rule cannot
// emit (VjpBuilder::addOper takes norm inputs only), and the zero tangent is not a constant for
// every element type. A second derivative that reaches either refuses by name instead of
// dropping the gradient. See the report for the two operations that would unblock them.

// `tangent/add_arr(lhs, rhs)`: the elementwise sum of two tangent arrays. Its gradient is the
// sum's gradient, unchanged, to each side.
void addTangentArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], *dy);
        b.accumulateGradient(call.inputs[1], *dy);
    }
}

// `tangent/scatter_add_arr(base, i, delta[, axis])`: `base` with `delta` added at the position `i`
// along `axis`. The gradient of `base` is the upstream gradient, unchanged; the gradient of
// `delta` is what `i` names on that axis of the *upstream gradient* -- the positions of a scatter
// are a gather of the gradient that reaches it, and it is the same position read, along the same
// axis, that took `delta` in the forward. The index is discrete, so no gradient reaches it
// (design principles 9.9); what the accumulator materializes for it is the zero of its own type.
void scatterAddArrayVjp(VjpBuilder &b, const VjpCall &call) {
    if (call.inputs.size() < 3 || call.inputs.size() > 4) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects 3 or 4 inputs (base, index, delta, axis), got {}.",
            call.uri,
            call.inputs.size()));
    }
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], *dy);
        if (b.isActive(call.inputs[2])) {
            Type *const element =
                tt::as_ptr<type::ArrayType>(b.nodeType(call.inputs[0]))->elemType();
            std::vector<vjp_node_t> gather{*dy, call.inputs[1]};
            if (call.inputs.size() > 3) {
                gather.push_back(call.inputs[3]);
            }
            b.accumulateGradient(call.inputs[2], b.addOper(element, ":op/idx_arr", gather));
        }
    }
}

// `tangent/scatter_take_arr(base, indices, delta[, axis])`: `base` with `delta`'s elements added
// back at the positions the indices name along `axis`. The forward is a gather of the slabs along
// that axis, so the gradient of `delta` is those same positions of the upstream gradient -- a
// gather of it, which is the take itself, along the same axis -- and `base`, whose other positions
// only padding came from, keeps the upstream gradient (design principles 9.9). The indices are
// discrete.
void scatterTakeArrayVjp(VjpBuilder &b, const VjpCall &call) {
    if (call.inputs.size() < 3 || call.inputs.size() > 4) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' expects 3 or 4 inputs (base, indices, delta, axis), got {}.",
            call.uri,
            call.inputs.size()));
    }
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], *dy);
        if (b.isActive(call.inputs[2])) {
            std::vector<vjp_node_t> take{*dy, call.inputs[1]};
            if (call.inputs.size() > 3) {
                take.push_back(call.inputs[3]);
            }
            b.accumulateGradient(
                call.inputs[2], b.addOper(b.nodeType(call.inputs[2]), ":op/idx_take", take));
        }
    }
}

// `tangent/scatter_slice_arr(dx, like, axis, start, end, step, written)`: the array of `like`'s
// shape whose sliced positions hold `dx`'s elements. The forward is a gather over a range, so the
// gradient of `dx` is the same range of the upstream gradient: the slice operator with the same
// axis, bounds, step and mask. `like` gives only the shape and the zero tangent of its elements,
// and the axis, the bounds, the step and the mask only say where the positions are: shape and
// position are discrete, so no gradient reaches any of them (design principles 9.9).
void scatterSliceArrayVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 7);
    if (auto dy = b.gradientOf(call.output)) {
        if (b.isActive(call.inputs[0])) {
            const std::array<vjp_node_t, 6> slice{
                *dy,
                call.inputs[2],
                call.inputs[3],
                call.inputs[4],
                call.inputs[5],
                call.inputs[6]};
            b.accumulateGradient(
                call.inputs[0],
                b.addOper(b.nodeType(call.inputs[0]), ":op/slice_sub", slice));
        }
    }
}

// ---- the member read of an aggregate ----
//
// `access::<T, U><at>(x)` and `access_key::<T, U><key>(x)` read one member of an aggregate, and a
// differentiated value passes through that read: the reverse of taking one member out is putting
// the gradient back where the member stood, with the zero tangent in every other place. So the
// read's gradient is the aggregate's tangent with `dy` at the member's position and nothing else
// anywhere -- it is neither dropped nor handed to the aggregate whole.
//
// The place (`at`, an int) or the name (`key`, a string) is the operator's compile-time parameter
// and a `with` argument of the call, so which member is written is decided here, once; which
// spelling the call used does not decide it, because that is `__access_value__`'s reading too: the
// with argument's type says whether the member is read by place or by name (prelude.cml has the
// two URIs).

/// The member the constant with argument `where` names, as an element index of `aggregate`.
std::optional<size_t> memberIndexOf(VjpBuilder &b, Type *aggregate, vjp_node_t where) {
    if (!isAggregate(aggregate)) {
        return std::nullopt;
    }
    const std::optional<slot_t> constant = b.staticValueOf(where);
    if (!constant) {
        return std::nullopt;
    }
    Type *const whereType = b.nodeType(where);
    if (whereType != nullptr && whereType->code() == TypeCode::String) {
        const auto *key = fromSlot<const String *>(*constant);
        if (key == nullptr) {
            return std::nullopt;
        }
        return tt::as_ptr<type::StructType>(aggregate)->findField(key->view());
    }
    const int64_t at = fromSlot<int64_t>(*constant);
    if (at < 0 || static_cast<uint64_t>(at) >= aggregateSize(aggregate)) {
        return std::nullopt;
    }
    return static_cast<size_t>(at);
}

/// The element of `aggregate` whose place in the aggregate's tangent is `position`.
std::optional<size_t> primalElementOf(Type *aggregate, size_t position) {
    for (size_t i = 0; i < aggregateSize(aggregate); ++i) {
        const std::optional<size_t> at = machineryTangentElementIndex(aggregate, i);
        if (at && *at == position) {
            return i;
        }
    }
    return std::nullopt;
}

/// A value of `aggregate`'s tangent type built from `fields`, one per member in the tangent's
/// order: the construction a program writes as an aggregate literal (`fill::<T, ...F>`), which is
/// the operator a member read's reverse needs. Its template is one constant of the type it makes
/// and every member is written from the fields, so the constant holds only the shape.
vjp_node_t tangentAggregateOf(
    VjpBuilder &b, Type *aggregate, const std::vector<vjp_node_t> &fields) {
    Type *const tangent = machineryTangentTypeOf(aggregate);
    if (tangent == nullptr) {
        throw std::runtime_error(std::format(
            "Derivative of a member read has no tangent type for '{}'.", aggregate->toString()));
    }
    const size_t size = aggregateSize(tangent);
    Object *empty     = tangent->code() == TypeCode::Struct
                            ? static_cast<Object *>(::Struct::create(size, mm::autoSpace()))
                            : static_cast<Object *>(::Tuple::create(size, mm::autoSpace()));
    std::vector<vjp_node_t> inputs;
    inputs.reserve(size + 1);
    inputs.push_back(b.addStatic(toSlot<Object *>(empty), tangent));
    inputs.insert(inputs.end(), fields.begin(), fields.end());
    return b.addOper(tangent, ":op/fill", inputs);
}

vjp_node_t zeroTangentOf(VjpBuilder &b, Type *primal, vjp_node_t primalNode);

/// The zero tangent of the aggregate `primal`: every member's own zero in its place, taken member
/// by member the way `machineryTangentTypeOf` keeps them.
vjp_node_t zeroTangentAggregateOf(VjpBuilder &b, Type *primal, vjp_node_t primalNode) {
    Type *const tangent = machineryTangentTypeOf(primal);
    if (tangent == nullptr) {
        throw std::runtime_error(std::format(
            "Derivative of a member read has no tangent type for '{}'.", primal->toString()));
    }
    std::vector<vjp_node_t> fields;
    const size_t size = aggregateSize(tangent);
    fields.reserve(size);
    for (size_t position = 0; position < size; ++position) {
        const std::optional<size_t> member = primalElementOf(primal, position);
        if (!member) {
            throw std::runtime_error(std::format(
                "Derivative of a member read found a tangent member of '{}' with no primal "
                "member.",
                primal->toString()));
        }
        fields.push_back(zeroTangentOf(
            b,
            aggregateElement(primal, *member),
            b.addAccess(primalNode, *member)));
    }
    return tangentAggregateOf(b, primal, fields);
}

/// The zero tangent of the value `primalNode`: a scalar's registered zero, an array's zeros of its
/// shape, an aggregate's every member's zero in its place.
vjp_node_t zeroTangentOf(VjpBuilder &b, Type *primal, vjp_node_t primalNode) {
    if (isAggregate(primal)) {
        return zeroTangentAggregateOf(b, primal, primalNode);
    }
    Type *const tangent = machineryTangentTypeOf(primal);
    if (const std::optional<slot_t> zero = zeroTangentConstantOf(primal)) {
        return b.addStatic(*zero, tangent != nullptr ? tangent : primal);
    }
    const TangentSpace *space = DerivativeRegistry::instance().findTangentSpace(primal);
    if (space != nullptr && !space->zerosLikeUri.empty()) {
        const std::array<vjp_node_t, 1> like{primalNode};
        return b.addOper(tangent != nullptr ? tangent : primal, space->zerosLikeUri, like);
    }
    throw std::runtime_error(std::format(
        "Derivative of a member read cannot build the zero tangent of the member's type '{}'.",
        primal->toString()));
}

// A member read: the gradient of the member goes back to that member's position in the aggregate's
// gradient, and the members that were not read get the zero tangent.
void memberReadVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    requireWithInputs(call, 1);
    const std::optional<vjp_node_t> dy = b.gradientOf(call.output);
    if (!dy) {
        // Nothing reached the read: a member with no tangent of its own (an integer field), or a
        // value no differentiated parameter flows into. A discrete member's derivative is zero by
        // definition (design principles 9.9), so there is no gradient to carry.
        return;
    }
    Type *const aggregate              = b.nodeType(call.inputs[0]);
    const std::optional<size_t> member = memberIndexOf(b, aggregate, call.withInputs[0]);
    if (!member) {
        throw std::runtime_error(std::format(
            "Derivative of '{}' cannot read the member it names: the place or the name is not a "
            "constant of the program, or it names no member of '{}'. A read whose member is "
            "decided while the program runs has no derivative.",
            call.uri,
            aggregate->toString()));
    }
    const std::optional<size_t> position = machineryTangentElementIndex(aggregate, *member);
    if (!position) {
        // The member has no gradient channel of its own, so no gradient can have reached the read.
        return;
    }
    Type *const tangent = machineryTangentTypeOf(aggregate);
    if (tangent == nullptr) {
        return;
    }
    std::vector<vjp_node_t> fields;
    const size_t size = aggregateSize(tangent);
    fields.reserve(size);
    for (size_t at = 0; at < size; ++at) {
        if (at == *position) {
            fields.push_back(*dy);
            continue;
        }
        const std::optional<size_t> other = primalElementOf(aggregate, at);
        if (!other) {
            throw std::runtime_error(std::format(
                "Derivative of '{}' found a tangent member of '{}' with no primal member.",
                call.uri,
                aggregate->toString()));
        }
        fields.push_back(zeroTangentOf(
            b,
            aggregateElement(aggregate, *other),
            b.addAccess(call.inputs[0], *other)));
    }
    b.accumulateGradient(call.inputs[0], tangentAggregateOf(b, aggregate, fields));
}

// A conversion between float widths: the gradient converts back.
template <typename To> void convertVjp(VjpBuilder &b, const VjpCall &call, std::string_view back) {
    requireInputs(call, 1);
    if (auto dy = b.gradientOf(call.output)) {
        const std::array<vjp_node_t, 1> inputs{*dy};
        b.accumulateGradient(call.inputs[0], b.addOper(To::type(), back, inputs));
    }
}

// A conversion of an array to another element type: the gradient converts back to the element type
// of the input. An input that is not a float array has no gradient to receive.
void arrayConvertVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    Type *input            = b.nodeType(call.inputs[0]);
    const type::ArrayType *array = type::asArrayType(input);
    if (array == nullptr || !type::isFloatingScalar(array->elemTypeCode())) {
        return;
    }
    const std::array<vjp_node_t, 1> inputs{*dy};
    b.accumulateGradient(
        call.inputs[0],
        b.addOper(input, arrayConversionUri(array->elemTypeCode()), inputs));
}

void dtofVjp(VjpBuilder &b, const VjpCall &call) { convertVjp<F64>(b, call, ":op/ftod"); }
void ftodVjp(VjpBuilder &b, const VjpCall &call) { convertVjp<F32>(b, call, ":op/dtof"); }

// The conversion the operator `:op/cast` performs. The value it makes has the type the node is
// declared with, so the way back is the same operator with the input's own type: a conversion
// between two spellings of one tensor (`f32[2, 3]` and `f32[?, 3]`) checks the value and changes
// nothing, which is what makes the result's gradient the input's gradient; a conversion into a
// value that has no tangent (an integer, a name) has no gradient to give back, and the input is
// not active, so no gradient ever reaches this rule through it (design principles 9.9).
void castVjp(VjpBuilder &b, const VjpCall &call) {
    requireInputs(call, 1);
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    Type *const back = b.nodeType(call.inputs[0]);
    if (back == nullptr) {
        return;
    }
    const type::ArrayType *array =
        back->code() == TypeCode::Array ? tt::as_ptr<const type::ArrayType>(back) : nullptr;
    const bool floating =
        type::isFloatingScalar(back->code()) ||
        (array != nullptr && type::isFloatingScalar(array->elemTypeCode()));
    if (!floating) {
        return;
    }
    const std::array<vjp_node_t, 1> inputs{*dy};
    b.accumulateGradient(call.inputs[0], b.addOper(back, ":op/cast", inputs));
}

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

/// The forward, transpose and batching rules of the float arithmetic are written down in the
/// ruleset modules (`rulesets.jvp`, `rulesets.transpose`, `rulesets.vmap`), and no table here says
/// so: the key a rule is filed under is read off the operator (`camel/core/forward.h#ruleKeyOf`),
/// because `:op/add_d` is written `+` with two operands and that is what `add` means. The one
/// operator whose key is not derivable is named, with its reason, in `ruleKeyExceptions()`.

void registerBuiltinDerivatives() {
    auto &registry = DerivativeRegistry::instance();
    registerFloatRules<F64>(registry);
    registerFloatRules<F32>(registry);
    registry.setRule(":op/dtod", identityVjp);
    registry.setRule(":op/ftof", identityVjp);
    registry.setRule(":op/dtof", dtofVjp);
    registry.setRule(":op/ftod", ftodVjp);
    for (const char *uri :
         {":op/atoi", ":op/atol", ":op/atof", ":op/atod", ":op/atobf", ":op/atof6"}) {
        registry.setRule(uri, arrayConvertVjp);
    }
    // A conversion between two tensor types is one operator now, not a node kind, and the four
    // float-width rules above are the same conversion written narrower: the rule reads the input's
    // type for the way back instead of being filed under a URI per pair of widths.
    registry.setRule(":op/cast", castVjp);
    registry.setRule(":op/idx_arr", idxArrayVjp);
    // `t[idx]` takes whole slabs along the axis it names, so its adjoint scatters them back into
    // the array's gradient along the same axis.
    registry.setRule(":op/idx_take", idxTakeArrayVjp);
    // A slice of an array is a range of positions, so its adjoint is the range scatter of the
    // tangent array: the gradient of the elements the slice took goes back to their places.
    registry.setRule(":op/slice_sub", sliceSubArrayVjp);
    registry.setRule(":struct/concat_arr", concatArrayVjp);
    registry.setRule(":struct/append_arr", appendArrayVjp);
    // The form the loop-state rewrite uses (Builder::markLoopStateInPlace): the same call, so
    // the same rule - it reads the output's gradient and never the array's own value.
    registry.setRule(":struct/append_arr_owned", appendArrayVjp);
    // The in-place form of a rebinding (`Builder::inPlaceRebindingOf`): an append whose appended
    // value is still the call's with input, so the value's gradient is the same last element.
    registry.setRule(":struct/append_arr_in_place", appendArrayVjp);
    // The appended array is a value too: its gradient is the tail of the output's gradient.
    // The private copy a rewritten loop starts from is the identity on values: its gradient is
    // the gradient of the array it copied, unchanged.
    registry.setRule(":struct/copy_arr", identityVjp);
    // The tangent operators a first-order array gradient is built from: their rules are the
    // second-order rules of the array kind (see the block above). `:tangent/zeros_arr` has no
    // gradient to pass on: its values are the zero tangents of the elements of its input, so the
    // only thing it takes from that input is its length.
    registry.setRule(":tangent/add_arr", addTangentArrayVjp);
    registry.setRule(":tangent/zeros_arr", noGradient);
    registry.setRule(":tangent/scatter_add_arr", scatterAddArrayVjp);
    registry.setRule(":tangent/scatter_slice_arr", scatterSliceArrayVjp);
    registry.setRule(":tangent/scatter_take_arr", scatterTakeArrayVjp);
    // A member read: the gradient of the member goes back to that member's place in the
    // aggregate's gradient, and the other members get the zero tangent (see the block above).
    // `:op/access` and `:op/access_key` are one read spelled two ways, and the rule reads which
    // member by the constant operand the call was written with, not by which URI it holds.
    registry.setRule(":op/access", memberReadVjp);
    registry.setRule(":op/access_key", memberReadVjp);

    registry.setTangentSpace(TypeCode::Float64, floatTangentSpace<F64>());
    registry.setTangentSpace(TypeCode::Float32, floatTangentSpace<F32>());
    registry.setTangentSpace(TypeCode::BFloat16, floatTangentSpace<BF16>());
    registry.setTangentSpace(TypeCode::Float16, floatTangentSpace<F16>());
    // Arrays have no entry of their own either: their tangent is the array of their elements'
    // tangents, and adding two of them is elementwise (design principles 9.9).
    TangentSpace arraySpace;
    arraySpace.addUri       = ":tangent/add_arr";
    arraySpace.zerosLikeUri = ":tangent/zeros_arr";
    // One position's gradient goes back to its place by slot: the core's arrays hold their
    // elements one slot each, and a position on a deeper dimension takes the slab along it, which
    // the same scatter places (`:tangent/scatter_add_arr(base, i, delta[, axis])`, save that the
    // reverse loop scatters the element a round took, the first dimension's, so it writes no
    // axis).
    arraySpace.scatterUri = ":tangent/scatter_add_arr";
    registry.setTangentSpace(TypeCode::Array, std::move(arraySpace));
}
