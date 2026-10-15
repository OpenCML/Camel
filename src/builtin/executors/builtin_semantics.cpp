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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The semantics of the built-in scalar arithmetic (`:op/` operators on i32, int, f32 and
 * float): their stable keys, numeric semantics and faults (camel/core/operator_semantics.h,
 * docs/design/op-semantics section 6.2, design principles 2.13).
 *
 *   integer + - * neg, comparisons    exact (two's complement, wrapping)
 *   integer / %                       exact; a zero divisor is a fault
 *   float + - *                       one correctly rounded IEEE step in the operand type
 *   float /                           the same; a divisor of +0 or -0 is a fault (2.9)
 *   float neg, comparisons            exact
 *
 * Every operator is declared at version 1.0. Operators not listed here (pow, the untyped
 * `op/mul` and `op/div`, string operators) have no declaration and read as opaque.
 *
 * Beside them, `:mark/unordered_reduce_arr` declares its index semantics and its reduction law
 * (docs/design/op-semantics 6.6 and 7): an unordered reduction by the function it is given.
 */

#include "builtin.h"

#include "camel/core/operator_table.h"

#include <string>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
using camel::core::type::TypeCode;

/// The four numeric suffixes of the typed `:op/` operators and their types.
struct NumericType {
    const char *suffix;
    TypeCode type;
    bool isFloat;
};
constexpr NumericType kNumericTypes[] = {
    {"i", TypeCode::Int32, false},
    {"l", TypeCode::Int64, false},
    {"f", TypeCode::Float32, true},
    {"d", TypeCode::Float64, true},
};

/// Declares `:op/<name>_<suffix>` with the given numeric semantics and faults.
void declare(
    const std::string &name, const NumericType &t, NumericSemantics numeric,
    std::optional<std::vector<FaultSpec>> faults) {
    const std::string uri = ":op/" + name + "_" + t.suffix;
    OperatorTable::instance().declareSemantics(
        uri,
        OperatorSemantics{
            .key     = OperatorKey::of(uri),
            .numeric = std::move(numeric),
            .faults  = std::move(faults),
        });
}

/// No rounding: integer arithmetic and float sign and comparison operations. A float one can
/// still be changed by the finite_math assumptions (a flushed subnormal, the sign of a zero,
/// a comparison with a NaN assumed away); an integer one by nothing.
NumericSemantics exact(const NumericType &t, std::string algorithm) {
    return NumericSemantics{
        .cls         = NumericClass::Exact,
        .compute     = t.isFloat ? t.type : TypeCode::Void,
        .sensitiveTo = t.isFloat ? kFiniteMath : kNoPermissions,
        .algorithm   = std::move(algorithm),
    };
}

/// One correctly rounded IEEE operation in the operand type.
NumericSemantics rounded(const NumericType &t, PermissionMask sensitiveTo, std::string algorithm) {
    return NumericSemantics{
        .cls         = NumericClass::CorrectlyRounded,
        .compute     = t.type,
        .sensitiveTo = sensitiveTo,
        .algorithm   = std::move(algorithm),
    };
}

/**
 * `unordered_reduce<f, initial>(xs)` (`:mark/unordered_reduce_arr`, module para; design 6.6):
 * one reduction over the elements of xs, combined by the function value f, starting once from
 * `initial`, in an order the operator leaves open. Its law is Custom and Unordered: calling it
 * is the program's own grant of reassociate for this one reduction (reorderFreedom), and grants
 * nothing inside f. Whether f is associative or commutative is not inferred: not claimed.
 *
 *   domain   i < n reduction        n = the length of xs (an extent the type does not fix)
 *   norm 0   (i)
 *   with 0   none                   the combining function
 *   with 1   none                   the initial value (the law's start)
 *   write    () combine
 *
 * Its numeric semantics are those of f: opaque here.
 */
void declareUnorderedReduce() {
    constexpr std::string_view uri = ":mark/unordered_reduce_arr";
    ReductionLaw law{
        .name            = "unordered_reduce",
        .kind            = LawKind::Custom,
        .init            = LawInit::Operand,
        .initOperand     = 1,
        .order           = LawOrder::Unordered,
        .combinerOperand = 0,
    };
    auto indexing = [](const InstanceContext &cx) -> std::optional<IndexedInstance> {
        if (cx.norm.size() != 1 || cx.with.size() != 2 || cx.norm[0]->code() != TypeCode::Array) {
            return std::nullopt;
        }
        IndexedInstance inst;
        ExprPool &x = inst.exprs;
        inst.domain.params.push_back(
            Param{.name = "n", .source = ParamSource::Extent, .operand = 0, .axis = 0});
        inst.domain.dims.push_back({"i", x.param(0), DimRole::Reduction});
        inst.norm   = {Access{.kind = AccessKind::Affine, .coords = {x.dim(0)}}};
        inst.with   = {Access{.kind = AccessKind::None}, Access{.kind = AccessKind::None}};
        inst.output = Write{.mode = WriteMode::Combine};
        inst.law    = 0;
        return inst;
    };
    OperatorTable::instance().declareSemantics(
        uri,
        OperatorSemantics{
            .key      = OperatorKey::of(uri),
            .indexing = indexing,
            .laws     = {law},
        });
}

} // namespace

void declareBuiltinOperatorSemantics() {
    const std::vector<FaultSpec> none;
    const std::vector<FaultSpec> zeroDivisor = {{FaultKind::DivisionByZero, 1}};
    // + - * on floats: a chain of them may be reassociated (a sum is a reduction) or contracted
    // into fused multiply-adds, computed at another precision, or rewritten under the
    // finite_math assumptions, each only with the permission.
    constexpr PermissionMask kAddMul = kReassociate | kContract | kChangePrecision | kFiniteMath;
    // Division: an approximate reciprocal or division is approx_math.
    constexpr PermissionMask kDiv = kApproxMath | kChangePrecision | kFiniteMath;
    for (const NumericType &t : kNumericTypes) {
        if (t.isFloat) {
            declare("add", t, rounded(t, kAddMul, "a + b, rounded once"), none);
            declare("sub", t, rounded(t, kAddMul, "a - b, rounded once"), none);
            declare("mul", t, rounded(t, kAddMul, "a * b, rounded once"), std::nullopt);
            declare(
                "div",
                t,
                rounded(t, kDiv, "a / b, rounded once; b = +0 or -0 is a fault"),
                zeroDivisor);
        } else {
            declare("add", t, exact(t, "a + b, wrapping"), none);
            declare("sub", t, exact(t, "a - b, wrapping"), none);
            declare("mul", t, exact(t, "a * b, wrapping"), std::nullopt);
            declare(
                "div",
                t,
                exact(t, "a / b truncated toward zero; b = 0 is a fault"),
                zeroDivisor);
            declare(
                "mod",
                t,
                exact(t, "the remainder of a / b; b = 0 is a fault"),
                zeroDivisor);
        }
        declare("neg", t, exact(t, t.isFloat ? "-a: the sign flipped" : "-a, wrapping"), std::nullopt);
        for (const char *cmp : {"lt", "le", "gt", "ge"}) {
            declare(
                cmp,
                t,
                exact(t, t.isFloat ? "IEEE comparison (false when either side is NaN)" : "comparison"),
                none);
        }
    }
    declareUnorderedReduce();
}
