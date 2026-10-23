// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The declared semantics of the tensor operators (camel/core/operator_semantics.h,
 * docs/design/op-semantics sections 6.2 and 12, design principles 7.1), in one table so that
 * every declaration can be read next to the others and checked against the kernels.
 *
 * Phase 1 declares the numeric part of the operators a compiler of tensor graphs (Tiles) meets
 * first: elementwise arithmetic, the unary maps exp, log, sqrt, rsqrt, abs, neg, relu, the
 * reductions sum, mean, max, min, matmul, and the layout operators. Phase 2 adds their index
 * semantics (iteration domain, access maps, reduction laws: indexing.cpp), attached here to the
 * same records at version 1.1. Every other tensor operator has no declaration and reads as
 * opaque.
 *
 * A tensor operator works on several element types; the declaration describes its float32
 * case. On int64 and bool elements every operator listed here is exact (integer arithmetic
 * wraps; sum and mean accumulate exactly in int64, mean's final division is in f64).
 *
 * Each declaration states the canonical algorithm the CPU kernel implements bit for bit
 * (modules/tensor/kernels); a back end that gives other bits needs a permission of the node
 * that its declaration lists in sensitiveTo.
 */

#include "indexing.h"
#include "op_def.h"

#include "../type.h"

#include <bit>
#include <string>
#include <unordered_map>

namespace camel::tensor::ops {

namespace {

using namespace camel::core::sem;
using camel::core::type::TypeCode;

OperatorSemantics record(NumericSemantics numeric) {
    return OperatorSemantics{.numeric = std::move(numeric)};
}

/// Moves and selections of elements: no arithmetic, nothing a permission can change.
OperatorSemantics layout(std::string algorithm) {
    return record({
        .cls         = NumericClass::Exact,
        .sensitiveTo = kNoPermissions,
        .algorithm   = std::move(algorithm),
    });
}

/// No rounding, but special values matter: the finite_math assumptions can change the result
/// (a NaN assumed away, the sign of a zero, a flushed subnormal).
OperatorSemantics exactFloat(std::string algorithm) {
    return record({
        .cls         = NumericClass::Exact,
        .compute     = TypeCode::Float32,
        .sensitiveTo = kFiniteMath,
        .algorithm   = std::move(algorithm),
    });
}

/// A fixed sequence of correctly rounded steps in `compute`.
OperatorSemantics rounded(TypeCode compute, PermissionMask sensitiveTo, std::string algorithm) {
    return record({
        .cls         = NumericClass::CorrectlyRounded,
        .compute     = compute,
        .sensitiveTo = sensitiveTo,
        .algorithm   = std::move(algorithm),
    });
}

/// A math function within `ulp` of the exact result; special values exact.
OperatorSemantics bounded(float ulp, SpecialValues specials, std::string algorithm) {
    return record({
        .cls         = NumericClass::Bounded,
        .ulpBound    = ulp,
        .compute     = TypeCode::Float32,
        .specials    = specials,
        .sensitiveTo = kApproxMath | kChangePrecision | kFiniteMath,
        .algorithm   = std::move(algorithm),
    });
}

// The permissions a chain of additions or multiplications can use: regroup or reorder it
// (reassociate), fuse a multiply into the next add (contract), compute at another precision.
constexpr PermissionMask kArith = kReassociate | kContract | kChangePrecision | kFiniteMath;
// A reduction in a declared accumulator type, in ascending order.
constexpr PermissionMask kReduce = kReassociate | kChangePrecision | kFiniteMath;
// One correctly rounded operation that has an approximate counterpart (division, square root).
constexpr PermissionMask kApprox = kApproxMath | kChangePrecision | kFiniteMath;

const std::unordered_map<std::string_view, OperatorSemantics> &declarations() {
    static const std::unordered_map<std::string_view, OperatorSemantics> table = [] {
        std::unordered_map<std::string_view, OperatorSemantics> t;
        const TypeCode f32 = TypeCode::Float32, f64 = TypeCode::Float64;

        // ---- Elementwise arithmetic (operands broadcast; f32 when either side is f32) ----
        t.emplace("add", rounded(f32, kArith, "a + b per element, rounded once in f32"));
        t.emplace("subtract", rounded(f32, kArith, "a - b per element, rounded once in f32"));
        t.emplace("multiply", rounded(f32, kArith, "a * b per element, rounded once in f32"));
        t.emplace(
            "divide",
            rounded(
                f32,
                kApprox,
                "a / b per element, rounded once in f32 (IEEE: x / 0 is an infinity or NaN, not "
                "a fault)"));
        t.emplace(
            "maximum",
            exactFloat(
                "IEEE 754-2019 maximum per element: NaN on either side gives the canonical quiet "
                "NaN, -0 < +0"));
        t.emplace(
            "minimum",
            exactFloat(
                "IEEE 754-2019 minimum per element: NaN on either side gives the canonical quiet "
                "NaN, -0 < +0"));

        // ---- Unary maps ----
        t.emplace("neg", exactFloat("-x per element: the sign flipped"));
        t.emplace("abs", exactFloat("|x| per element: the sign cleared"));
        t.emplace(
            "relu",
            exactFloat("maximum(x, +0) per element: NaN gives the canonical NaN, -0 gives +0"));
        t.emplace("sqrt", rounded(f32, kApprox, "sqrt(x) per element, rounded once in f32"));
        t.emplace(
            "rsqrt",
            rounded(f32, kApprox, "1 / sqrt(x) per element: two steps, each rounded in f32"));
        // exp: measured over every f32 input (portable and AVX2+FMA units), at most 1.01 ulp
        // where the result is a normal float and 0.76 ulp where it is subnormal, NaN, +-inf and
        // +-0 exact (the scale is applied in two normal factors,
        // so a subnormal result is rounded once). Declared at the library target of 2 ulp.
        t.emplace(
            "exp",
            bounded(
                2.0f,
                {},
                "e^x per element, within 2 ulp, subnormal results included (+0 below half the "
                "smallest subnormal); NaN, +-inf and +-0 exact"));
        // log: the C library's logf, measured at most 0.82 ulp over every f32 input.
        t.emplace(
            "log",
            bounded(2.0f, {}, "ln x per element, within 2 ulp; NaN, +-inf, +-0 and x < 0 exact"));

        // ---- Reductions ----
        t.emplace(
            "sum",
            rounded(
                f64,
                kReduce,
                "every element converted to f64 and added to an f64 accumulator from +0 in "
                "row-major order; the result is the f64 sum (int64 and bool elements: added "
                "exactly in int64, wrapping, and converted to f64 once)"));
        t.emplace(
            "mean",
            rounded(
                f64,
                kReduce,
                "the sum of `sum` (f64; int64 for int64 and bool elements) converted to f64 and "
                "divided by the element count in f64; an empty tensor is a fault "
                "(DivisionByZero, as its definition divides by zero)"));
        t.emplace(
            "sum_axis",
            rounded(
                f64,
                kReduce,
                "per output element: the f64 sum from +0 over the axis in ascending index order, "
                "rounded once to f32"));
        t.emplace(
            "mean_axis",
            rounded(
                f64,
                kReduce,
                "the f64 sum of sum_axis divided by the axis extent in f64, rounded once to f32"));
        t.emplace(
            "max_axis",
            exactFloat(
                "IEEE 754-2019 maximum over the axis (exactly associative and commutative: any "
                "order gives the same bits); an empty axis is a fault"));
        t.emplace(
            "min_axis",
            exactFloat(
                "IEEE 754-2019 minimum over the axis (exactly associative and commutative: any "
                "order gives the same bits); an empty axis is a fault"));
        t.emplace(
            "argmax_axis",
            exactFloat(
                "the position (int64) of the first maximum over the axis by IEEE 754-2019 "
                "maximum: the first NaN if there is one, +0 above -0, the earlier position on a "
                "tie; an empty axis is a fault"));
        t.emplace(
            "argmin_axis",
            exactFloat(
                "the position (int64) of the first minimum over the axis by IEEE 754-2019 "
                "minimum: the first NaN if there is one, -0 below +0, the earlier position on a "
                "tie; an empty axis is a fault"));
        t.emplace(
            "sum_to",
            rounded(
                f32,
                kReduce,
                "per output element: an f32 accumulator from +0, adding the contributions in "
                "the row-major order of the input"));

        // ---- matmul ----
        t.emplace(
            "matmul",
            rounded(
                f32,
                kReassociate | kChangePrecision | kFiniteMath,
                "per output element: acc = +0, then acc = fma(a[i][k], b[k][j], acc) for k in "
                "ascending order, every fused multiply-add rounded once in f32 (design "
                "principles 7.2); contract has nothing to add"));

        // ---- Layout: moves, no arithmetic ----
        t.emplace("to_bytes", layout("row-major payload bits encoded in little-endian byte order"));
        for (const char *name : {"from_bytes_f32", "from_bytes_i64", "from_bytes_bool",
                                 "from_bytes_bf16", "from_bytes_f16"}) {
            t.emplace(
                name,
                layout("little-endian payload bits decoded exactly into row-major storage"));
        }
        t.emplace("reshape", layout("the elements in row-major order, in a new shape"));
        t.emplace("flatten", layout("the elements in row-major order, as one axis"));
        t.emplace("unsqueeze", layout("the elements unchanged, with an axis of extent 1 added"));
        t.emplace("transpose", layout("the last two axes swapped"));
        t.emplace("permute", layout("the axes reordered"));
        t.emplace("slice", layout("the elements start, start + step, ... before stop of one axis"));
        t.emplace("broadcast_like", layout("the elements repeated along broadcast axes"));
        t.emplace("expand_axis", layout("the elements repeated along a reduced axis"));
        return t;
    }();
    return table;
}

} // namespace

namespace {

/// The binary laws of the elementwise arithmetic, over float32 elements (the case the
/// declarations describe): the same laws as the scalar `+`, `-`, `*` hold for each element, so
/// an algebraic rewrite that works on the declared laws treats a tensor expression and a scalar
/// one alike. Integer elements are left out: the kernels' integer contract is not declared
/// here as exactly associative.
std::optional<BinaryAlgebra> binaryLaws(std::string_view name) {
    constexpr std::string_view sum = "tensor:add";
    BinaryLaw law{.element = TypeCode::Float32};
    if (name == "add") {
        law.commutative = true;
        law.neutralBits = std::bit_cast<uint32_t>(-0.0f);
    } else if (name == "subtract") {
        law.subtractionOf = std::string(sum);
        law.neutralBits   = 0;
    } else if (name == "multiply") {
        law.commutative     = true;
        law.neutralBits     = std::bit_cast<uint32_t>(1.0f);
        law.distributesOver = std::string(sum);
    } else {
        return std::nullopt;
    }
    BinaryAlgebra algebra{.laws = {law}, .elementwise = true};
    algebra.elementOf = [](const camel::core::type::Type *type) -> std::optional<TypeCode> {
        if (!isTensorType(type)) {
            return std::nullopt;
        }
        return tensorFactsOf(type).dtype;
    };
    return algebra;
}

} // namespace

void attachSemantics(std::vector<OpDef> &defs) {
    const auto &table = declarations();
    for (OpDef &def : defs) {
        if (auto it = table.find(def.name); it != table.end()) {
            def.semantics         = it->second;
            def.semantics->binary = binaryLaws(def.name);
            attachIndexing(def);
        }
    }
}

} // namespace camel::tensor::ops
