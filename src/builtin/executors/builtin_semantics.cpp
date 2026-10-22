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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The semantics of the built-in scalar arithmetic (`:op/` operators on i32, int, f32 and
 * float): their stable keys, numeric semantics and faults (camel/core/operator_semantics.h,
 * docs/design/op-semantics section 6.2, design principles 7.1).
 *
 *   integer + - * neg, comparisons    exact (two's complement, wrapping)
 *   integer / %                       exact; a zero divisor is a fault
 *   float + - *                       one correctly rounded IEEE step in the operand type
 *   float /                           the same; a divisor of +0 or -0 is a fault (3.9)
 *   float neg, comparisons            exact
 *
 * Every operator is declared at version 1.0. Operators not listed here (pow, the untyped
 * `op/mul` and `op/div`, string operators) have no declaration and read as opaque.
 *
 * Beside them, `:mark/unordered_reduce_arr` declares its index semantics and its reduction law
 * (docs/design/op-semantics 6.6 and 7): an unordered reduction by the function it is given.
 *
 * The built-in operators that touch resources declare them (docs/design/op-semantics 8,
 * declareBuiltinOperatorResources): the variable operators and the assignments their cell,
 * the console and the profiler their ambient state. The higher-order `:mark/` operators run
 * the functions they are given and are left to the conservative reading.
 */

#include "builtin.h"

#include "camel/core/operator_table.h"

#include <bit>
#include <string>
#include <string_view>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
using camel::core::type::TypeCode;

/// The four numeric suffixes of the typed `:op/` operators and their types.
struct NumericType {
    const char *suffix;
    TypeCode type;
    bool isFloat;
    bool isComplex = false;
};
constexpr NumericType kNumericTypes[] = {
    {"i", TypeCode::Int32, false},
    {"l", TypeCode::Int64, false},
    {"f", TypeCode::Float32, true},
    {"d", TypeCode::Float64, true},
};
/// A complex number is two IEEE parts: an operation rounds each part once, in the part's own
/// type (f32 for c64, f64 for c128). No slot bit pattern names a complex zero (a complex value
/// is a boxed object, and each part has a signed zero), so no complex law declares a neutral
/// element.
constexpr NumericType kComplexTypes[] = {
    {"c", TypeCode::Complex64, true, true},
    {"z", TypeCode::Complex128, true, true},
};

/// The bit pattern of `value` in the type: what a neutral element is declared as.
uint64_t bitsOf(const NumericType &t, int value) {
    if (!t.isFloat) {
        return static_cast<uint64_t>(value);
    }
    if (t.type == TypeCode::Float32) {
        return std::bit_cast<uint32_t>(static_cast<float>(value));
    }
    return std::bit_cast<uint64_t>(static_cast<double>(value));
}

/// The laws of `+`, `-` and `*` over one numeric type (design section 7b): a sum is commutative
/// and, on wrapping integers, exactly associative; a float sum has a neutral element of -0 for
/// the sum and +0 for the difference; the product distributes over the sum; the difference is
/// the sum of the negation.
std::optional<BinaryAlgebra> binaryLaws(const std::string &name, const NumericType &t) {
    const std::string sum = std::string(":op/add_") + t.suffix;
    BinaryLaw law{.element = t.type};
    if (name == "add") {
        law.commutative      = true;
        law.exactAssociative = !t.isFloat;
        law.neutralBits      = t.isComplex
                                   ? std::nullopt
                                   : std::optional<uint64_t>(
                                         t.isFloat ? (t.type == TypeCode::Float32
                                                          ? std::bit_cast<uint32_t>(-0.0f)
                                                          : std::bit_cast<uint64_t>(-0.0))
                                                   : 0);
    } else if (name == "sub") {
        law.subtractionOf = sum;
        law.neutralBits =
            t.isComplex ? std::nullopt : std::optional<uint64_t>(bitsOf(t, 0));
    } else if (name == "mul") {
        law.commutative      = true;
        law.exactAssociative = !t.isFloat;
        law.neutralBits =
            t.isComplex ? std::nullopt : std::optional<uint64_t>(bitsOf(t, 1));
        law.distributesOver = sum;
    } else {
        return std::nullopt;
    }
    return BinaryAlgebra{.laws = {law}};
}

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
            .binary  = binaryLaws(name, t),
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
            .key          = OperatorKey::of(uri),
            .indexing     = indexing,
            .laws         = {law},
            .elementCalls = ElementCalls{.shape = ElementShape::Fold, .independent = true},
        });
}

/**
 * How the higher-order operators call their function (design principles 3.6), the one place
 * a scheduler or a code generator learns it from. `map`, `apply` and `filter` leave the order of
 * their calls open: the function of one element does not depend on that of another, so they
 * may run at once; `foreach` and `reduce` run in order, their unordered forms do not.
 * `attempt` calls its function once and keeps its faults.
 */
void declareElementCalls() {
    struct Row {
        std::string_view uri;
        ElementShape shape;
        bool independent;
    };
    constexpr Row rows[] = {
        {":mark/map_arr", ElementShape::Map, true},
        {":mark/apply_arr", ElementShape::Map, true},
        {":mark/filter_arr", ElementShape::Filter, true},
        {":mark/foreach_arr", ElementShape::Foreach, false},
        {":mark/unordered_foreach_arr", ElementShape::Foreach, true},
        {":mark/reduce_arr", ElementShape::Fold, false},
        {":mark/isolate", ElementShape::Isolate, false},
    };
    auto &table = OperatorTable::instance();
    for (const Row &row : rows) {
        OperatorSemantics record{.key = OperatorKey::of(row.uri)};
        if (const auto id = table.lookup(row.uri); id && table.semantics(*id) != nullptr) {
            record = *table.semantics(*id);
        }
        record.elementCalls = ElementCalls{.shape = row.shape, .independent = row.independent};
        table.declareSemantics(row.uri, std::move(record));
    }
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
            declare("mod", t, exact(t, "the remainder of a / b; b = 0 is a fault"), zeroDivisor);
        }
        declare(
            "neg",
            t,
            exact(t, t.isFloat ? "-a: the sign flipped" : "-a, wrapping"),
            std::nullopt);
        for (const char *cmp : {"lt", "le", "gt", "ge"}) {
            declare(
                cmp,
                t,
                exact(
                    t,
                    t.isFloat ? "IEEE comparison (false when either side is NaN)" : "comparison"),
                none);
        }
    }
    // The complex numbers: `+ - * /` and negation, each part rounded once in its own type. No
    // ordering and no `%` (cf. design principles 5.1); equality is content equality of the
    // parts and has no numeric semantics of its own.
    for (const NumericType &t : kComplexTypes) {
        const TypeCode part = t.type == TypeCode::Complex64 ? TypeCode::Float32
                                                            : TypeCode::Float64;
        NumericSemantics add = rounded(
            t, kAddMul, "a + b: each part added and rounded once in the part's type");
        add.compute = part;
        NumericSemantics sub = rounded(
            t, kAddMul, "a - b: each part subtracted and rounded once in the part's type");
        sub.compute = part;
        NumericSemantics mul = rounded(
            t, kAddMul, "a * b: (ac - bd) + (ad + bc)i, each part rounded once");
        mul.compute = part;
        NumericSemantics div = rounded(
            t, kDiv, "a / b: each part rounded once; b = 0 + 0i is a fault");
        div.compute = part;
        declare("add", t, std::move(add), none);
        declare("sub", t, std::move(sub), none);
        declare("mul", t, std::move(mul), std::nullopt);
        declare("div", t, std::move(div), zeroDivisor);
        declare(
            "neg",
            t,
            NumericSemantics{
                .cls         = NumericClass::Exact,
                .compute     = TypeCode::Void,
                .sensitiveTo = kFiniteMath,
                .algorithm   = "-a: the sign of each part flipped",
            },
            std::nullopt);
    }
    declareUnorderedReduce();
    declareElementCalls();
}

bool storesIntoCell(std::string_view name) { return name.starts_with("op/assn"); }

void declareBuiltinOperatorResources(std::span<const std::string_view> names) {
    auto &table     = OperatorTable::instance();
    const auto cell = [](AccessMode mode) {
        return operandAccess(ResourceClass::Variable, 0, mode);
    };
    const auto alias = [](uint8_t operand) {
        return ResultAlias{.kind = ResultAlias::Kind::MayAlias, .operand = operand};
    };
    // `var v = e`: a new cell.
    table.declareResources(
        ":var/new",
        ResourceSpec{.allocates = Resource{.cls = ResourceClass::Variable}});
    // Reading a variable reads its cell; the value read is (or shares) what the cell holds.
    for (const char *uri : {":var/get", ":var/peek"}) {
        table.declareResources(
            uri,
            ResourceSpec{.accesses = {cell(AccessMode::Read)}, .result = alias(0)});
    }
    // `:var/take` hands an in-place form the cell's own array when nothing else holds it, so
    // the cell's content changes with what the form does (Builder::inPlaceRebindingOf).
    table.declareResources(
        ":var/take",
        ResourceSpec{.accesses = {cell(AccessMode::ReadModifyWrite)}, .result = alias(0)});
    // The assignments: `=` stores its second operand (and returns it); a compound assignment
    // reads the cell, combines and stores (and returns the new value, a number or a string).
    for (std::string_view name : names) {
        if (!storesIntoCell(name)) {
            continue;
        }
        const std::string uri       = ":" + std::string(name);
        const std::string_view rest = name.substr(std::string_view("op/assn").size());
        ResourceSpec spec;
        if (rest.empty() || rest == "_owned" || (rest.size() == 2 && rest[0] == '_')) {
            spec = ResourceSpec{.accesses = {cell(AccessMode::Write)}, .result = alias(1)};
        } else {
            spec = ResourceSpec{.accesses = {cell(AccessMode::ReadModifyWrite)}};
        }
        table.declareResources(uri, std::move(spec));
    }
    // The in-place element set of a path assignment: it changes the array it was given (the
    // `:var/take` of the variable's own array) and returns it, whole.
    table.declareResources(
        ":op/set_index_in_place",
        ResourceSpec{
            .accesses = {operandAccess(ResourceClass::Object, 0, AccessMode::ReadModifyWrite)},
            .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0}});
    // The in-place form of `append` (Builder::inPlaceRebindingOf): it changes the array
    // `:var/take` gave it and returns it, whole.
    table.declareResources(
        ":struct/append_arr_in_place",
        ResourceSpec{
            .accesses = {operandAccess(ResourceClass::Object, 0, AccessMode::ReadModifyWrite)},
            .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0}});
    // The console and the profiler: state outside the values, visible outside the program.
    // The standard streams are named as design principles 3.3 names them (`stdout`, `stdin`,
    // `stderr`); each writer names the stream it writes.
    for (const char *uri : {":io/print", ":io/println"}) {
        table.declareResources(
            uri,
            ResourceSpec{
                .accesses       = {ambientAccess("stdout", AccessMode::Write)},
                .externalEffect = true});
    }
    table.declareResources(
        ":io/eprintln",
        ResourceSpec{
            .accesses       = {ambientAccess("stderr", AccessMode::Write)},
            .externalEffect = true});
    table.declareResources(
        ":io/input",
        ResourceSpec{
            .accesses       = {ambientAccess("stdin", AccessMode::ReadModifyWrite)},
            .externalEffect = true});
    for (const char *uri :
         {":profiler/begin", ":profiler/end", ":profiler/instant", ":profiler/enable"}) {
        table.declareResources(
            uri,
            ResourceSpec{
                .accesses       = {ambientAccess("profiler", AccessMode::ReadModifyWrite)},
                .externalEffect = true});
    }
}
