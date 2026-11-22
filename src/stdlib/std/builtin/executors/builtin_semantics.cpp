// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
 * The faults of the built-in operators are declared here too, in one table
 * (declareBuiltinOperatorSemantics below): the arithmetic that faults on a zero divisor, and the
 * operators with no fault path at all, which an empty table states. Nothing outside this file
 * says whether a built-in operator may fault; the traits and the node-level answer
 * (`src/kernel/runtime/graph.cpp#nodeMayFault`) are both read off this table, and so is the
 * record's `panic` -- an operator declared to raise nothing states `panic = false` in the same
 * place, because the two are one item in two places (design principles 3.9).
 *
 * Beside them, `:mark/unordered_reduce_arr` declares its index semantics and its reduction law
 * (docs/design/op-semantics 6.6 and 7): an unordered reduction by the function it is given.
 *
 * The built-in operators that touch resources declare them (docs/design/op-semantics 8,
 * declareBuiltinOperatorResources): the variable operators and the assignments their cell,
 * the console and the profiler their ambient state. The rest declare an empty access list, which
 * is what makes them pure (src/builtin/executors/builtin.cpp#kAccessFree). The higher-order
 * `:mark/` operators run the functions they are given and are left to the conservative reading.
 */

#include "builtin.h"

#include "camel/core/operator_table.h"
#include "camel/ops/operator_record.h"

#include <bit>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
using camel::core::type::TypeCode;

/// The four numeric suffixes of the typed `:op/` operators and their types.
struct NumericType {
    const char *suffix = nullptr;
    TypeCode type = TypeCode::Int32;
    bool isFloat = false;
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
    // An empty table is the statement "this operator can raise nothing" (the comment on
    // declareBuiltinOperatorFaults says why that is written at all). The record's `panic` states
    // the same item, from the list that names every such operator once.
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
    // What the operator already has is kept and this declares the addition: its declared accesses
    // and, on the second context that loads the module, the version the record
    // already carries. A record declared again from a fixed key is refused as a lower version, so
    // the key comes from the table the way `declareElementCalls` takes it.
    OperatorSemantics record{
        .key          = OperatorKey::of(uri),
        .indexing     = indexing,
        .laws         = {law},
        .elementCalls = ElementCalls{.shape = ElementShape::Fold, .independent = true},
    };
    auto &table = OperatorTable::instance();
    if (const auto id = table.lookup(uri); id && table.semantics(*id) != nullptr) {
        record = *table.semantics(*id);
        record.indexing     = indexing;
        record.laws         = {law};
        record.elementCalls = ElementCalls{.shape = ElementShape::Fold, .independent = true};
    }
    table.declareSemantics(uri, std::move(record));
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

/// One operator the built-in module declares never to fault, as stated at the C++ entrance: the
/// URI it answers on and the record's `panic` answer for it. The URI is the whole one (the empty
/// prefix is the core), and both fields are literals, so a reader of the source can enumerate the
/// statements: `scripts/check-operator-panic.mjs` compares this entrance with each operator's
/// implementation and reads literals, not the values expressions are computed to.
struct CppPanicStatement {
    std::string_view uri;
    bool panic; // false: a call of it raises nothing
};

/**
 * The fault table of the built-in operators (docs/design/op-semantics/README.cn.md 4.2): an operator
 * that can raise nothing declares an empty table, and that is the one place "never faults" is
 * written. declare above states the faults of the numeric operators; this states the empty
 * table of the operators that have no numeric semantics of their own, and the record's `panic`
 * for the same operators -- the one item at the two entrances design principles 3.9 gives it,
 * written from one list so that neither entrance can say one thing where the other says another.
 */
void declareBuiltinOperatorFaults() {
    auto &table = OperatorTable::instance();
    // The operators whose implementation has no fault path at all: an empty fault table is how
    // a declaration says "this can raise nothing", and it is the one place that is said --
    // design principles 3.3 leaves "may fault" undeclared-means-yes, and design principles 3.4
    // reads the answer when it decides whether a computation may be scheduled freely. The
    // passes do act on it: `src/kernel/runtime/graph.cpp#nodeMayFault` reads the trait derived from
    // this table, and `nodeIsFreelySchedulable` hands that answer to whoever may drop, move or
    // recompute a node -- a computation left out of this table is a running the program
    // observes, so `std::opt` keeps it alive with `:op/keep` when a removal takes its last
    // read (`src/stdlib/std/cxx/opt/generic/rewrite.cpp#isObservedRun`). Integer arithmetic wraps,
    // floating-point arithmetic rounds and a comparison only compares; the predicates over a
    // collection walk one and build no value; `pow` of the scalar and complex types is the
    // library call of the same name, which returns inf or NaN outside its domain. Each entry
    // is one operator whose implementation is read against this table.
    //
    // The same list is the record's `panic`: each entry carries its whole URI and its answer as
    // literals, because that entrance states one item per operator and a check reads it from the
    // source. A registration written in a loop over bare names is a statement no check can see.
    static constexpr CppPanicStatement kCppPanicStatements[] = {
        // `+ - *` and negation, in every numeric type: the four scalar suffixes, the two complex
        // ones (each part rounded once in its own type) and the two boxed ones.
        {":op/add_i", false},
        {":op/add_l", false},
        {":op/add_f", false},
        {":op/add_d", false},
        {":op/add_c", false},
        {":op/add_z", false},
        {":op/add_bf", false},
        {":op/add_f6", false},
        {":op/sub_i", false},
        {":op/sub_l", false},
        {":op/sub_f", false},
        {":op/sub_d", false},
        {":op/sub_c", false},
        {":op/sub_z", false},
        {":op/sub_bf", false},
        {":op/sub_f6", false},
        {":op/mul_i", false},
        {":op/mul_l", false},
        {":op/mul_f", false},
        {":op/mul_d", false},
        {":op/mul_c", false},
        {":op/mul_z", false},
        {":op/mul_bf", false},
        {":op/mul_f6", false},
        {":op/neg_i", false},
        {":op/neg_l", false},
        {":op/neg_f", false},
        {":op/neg_d", false},
        {":op/neg_c", false},
        {":op/neg_z", false},
        {":op/neg_bf", false},
        {":op/neg_f6", false},
        // The comparisons: IEEE comparison on the float types, ordinary comparison on the rest,
        // and content comparison on strings.
        {":op/lt_i", false},
        {":op/lt_l", false},
        {":op/lt_f", false},
        {":op/lt_d", false},
        {":op/lt_s", false},
        {":op/lt_bf", false},
        {":op/lt_f6", false},
        {":op/le_i", false},
        {":op/le_l", false},
        {":op/le_f", false},
        {":op/le_d", false},
        {":op/le_s", false},
        {":op/le_bf", false},
        {":op/le_f6", false},
        {":op/gt_i", false},
        {":op/gt_l", false},
        {":op/gt_f", false},
        {":op/gt_d", false},
        {":op/gt_s", false},
        {":op/gt_bf", false},
        {":op/gt_f6", false},
        {":op/ge_i", false},
        {":op/ge_l", false},
        {":op/ge_f", false},
        {":op/ge_d", false},
        {":op/ge_s", false},
        {":op/ge_bf", false},
        {":op/ge_f6", false},
        // Equality: numbers by value, strings and bytes by content, complex numbers part by
        // part.
        {":op/eq_i", false},
        {":op/eq_l", false},
        {":op/eq_f", false},
        {":op/eq_d", false},
        {":op/eq_b", false},
        {":op/eq_s", false},
        {":op/eq_c", false},
        {":op/eq_z", false},
        // `pow` of the scalar and complex types is the library call of the same name, which
        // returns inf or NaN outside its domain.
        {":op/pow_i", false},
        {":op/pow_l", false},
        {":op/pow_f", false},
        {":op/pow_d", false},
        {":op/pow_c", false},
        {":op/pow_c_i", false},
        {":op/pow_c_l", false},
        {":op/pow_z", false},
        {":op/pow_z_i", false},
        {":op/pow_z_l", false},
        {":op/pow_bf", false},
        {":op/pow_f6", false},
        // Logic, and the two operators whose answer is a property of a value rather than a
        // computation over one.
        {":op/and", false},
        {":op/or", false},
        {":op/not", false},
        {":op/extent_of", false},
        {":op/copy", false},
        // The predicates and measures over a collection walk one and build no value.
        {":struct/len_arr", false},
        {":struct/len_str", false},
        {":struct/len_bytes", false},
        {":struct/contains_arr", false},
        {":struct/contains_str", false},
        {":coll/len", false},
        {":coll/has", false},
        {":coll/set_contains", false},
        {":coll/eq", false},
        {":coll/set_is_subset", false},
        {":op/eq_bytes", false},
    };
    for (const CppPanicStatement &statement : kCppPanicStatements) {
        // The table the passes read and the record's `panic`: the same answer, written once.
        // For the operators `declare` above already gave an empty table (the two complex types'
        // `+ - *` and negation) this states the same empty table again, not a second one.
        table.declareFaults(statement.uri, {});
        camel::ops::CppRegistration registration(statement.uri, statement.panic);
        camel::ops::Registry::instance().registerOperator(registration);
        // Read both stores back, per operator. One item in two places is one statement only while
        // the two agree, and a load that left them disagreeing would be read two different ways by
        // two readers of one graph; this refuses the load and names the operator instead.
        const std::optional<camel::core::OperatorTraits> traits = table.find(statement.uri);
        const auto id     = table.lookup(statement.uri);
        const camel::ops::Record *record = id ? table.recordOf(*id) : nullptr;
        const bool tableAnswer           = traits.has_value() ? traits->mayFault : true;
        const bool recordAnswer          = record == nullptr ? true : record->mayFault();
        if (tableAnswer != recordAnswer) {
            throw std::invalid_argument(
                "The operator '" + std::string(statement.uri) +
                "' states `panic` one way at the record and the other in its fault table.");
        }
    }
    // `:op/tag_unwrap` is the one operator of the tagged forms that faults: a null where the tag has
    // already been read (`src/builtin/operators/tagged.cpp`). The fault is of no operand's value --
    // it is the absence of one -- and it is the compiler's own error, so it names no arithmetic kind.
    table.declareFaults(":op/tag_unwrap", {{FaultKind::Other, 0}});
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
    // `+ - *`, negation and the comparisons have no fault path: integer arithmetic wraps,
    // floating-point arithmetic rounds and a comparison only compares. `div` and `%` are the two
    // of a numeric type that fault (a zero divisor). The scalar `mul` and `neg` state the empty
    // table themselves; the never-faulting operators that have no numeric semantics of their own
    // are in the table `declareBuiltinOperatorFaults` below.
    for (const NumericType &t : kNumericTypes) {
        if (t.isFloat) {
            declare("add", t, rounded(t, kAddMul, "a + b, rounded once"), none);
            declare("sub", t, rounded(t, kAddMul, "a - b, rounded once"), none);
            declare("mul", t, rounded(t, kAddMul, "a * b, rounded once"), none);
            declare(
                "div",
                t,
                rounded(t, kDiv, "a / b, rounded once; b = +0 or -0 is a fault"),
                zeroDivisor);
        } else {
            declare("add", t, exact(t, "a + b, wrapping"), none);
            declare("sub", t, exact(t, "a - b, wrapping"), none);
            declare("mul", t, exact(t, "a * b, wrapping"), none);
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
            none);
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
        // Each part is rounded, never range-checked, so neither of these has a fault path; the
        // complex operators have no entry in the trait, so the list is written here.
        declare("mul", t, std::move(mul), none);
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
            none);
    }
    declareBuiltinOperatorFaults();
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
    // `:var/after(cell, call)`: the cell as it stands after the call that wrote it
    // (Builder::valueVersionOf). What it reaches is that cell, which the read above it reads and
    // the call beside it writes; what it gives is the cell itself, so the result is operand 0.
    table.declareResources(
        ":var/after",
        ResourceSpec{
            .accesses = {cell(AccessMode::Read)},
            .result   = ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0}});
    // The assignments: `=` stores its second operand into the cell, and a compound assignment
    // reads the cell, combines and stores. What either gives is the version of the variable --
    // the cell itself (design principles 2.1: a write produces this version's cell,
    // and the value it wrote is its second operand, where a position that wants the value takes
    // it) -- so the result names operand 0, the cell, and never the value written or an object
    // that value holds. The cell is the resource: two writes of one variable are one resource
    // and two variables are two (design principles 3.3 and 10.5), which is what lets a read of
    // one variable be told from a read of another.
    const auto cellResult = [] {
        return ResultAlias{.kind = ResultAlias::Kind::Same, .operand = 0};
    };
    for (std::string_view name : names) {
        if (!storesIntoCell(name)) {
            continue;
        }
        const std::string uri       = ":" + std::string(name);
        const std::string_view rest = name.substr(std::string_view("op/assn").size());
        // Writing a variable is not an effect (design principles 3.1 and 3.5):
        // the cell is one of the program's own variables, so an assignment is
        // state-only -- it changes what the program keeps and nothing outside it. The access list
        // below says the same thing one operand at a time; this is the word that keeps the reading
        // from falling back to "an effect was declared and nothing itemized, so it reaches
        // everything".
        table.set(uri, {.stateOnly = true});
        ResourceSpec spec;
        if (rest.empty() || rest == "_owned" || (rest.size() == 2 && rest[0] == '_')) {
            spec = ResourceSpec{.accesses = {cell(AccessMode::Write)}, .result = cellResult()};
        } else {
            spec =
                ResourceSpec{.accesses = {cell(AccessMode::ReadModifyWrite)}, .result = cellResult()};
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
    for (const char *uri :
         {":profiler/begin", ":profiler/end", ":profiler/instant", ":profiler/enable"}) {
        table.declareResources(
            uri,
            ResourceSpec{
                .accesses       = {ambientAccess("profiler", AccessMode::ReadModifyWrite)},
                .externalEffect = true});
    }
    // The private copy of an array made for one state of a loop whose body only appends to it
    // (`Builder::markLoopStateInPlace`) is pure and declared in the pure list of
    // `registerBuiltinOperatorTraits`: `copyWithRoom` allocates a new array, fills it with the
    // elements of the one it is given and leaves that one exactly as it was. The loop-state append
    // (`:struct/append_arr_owned`) sits in the same list, for the reason `:struct/append_arr` is:
    // it appends inside storage the program holds alone and copies when it does not, so what a
    // program with a second holder of the array sees does not change. Its parameter is ordinary,
    // not `var` -- it promises nothing about the array it is handed -- and a declared write through
    // an ordinary parameter is refused by the declaration check, which is the same answer.

    // A local of a generator body, whose cell is made before it has a value (`Tuple::create(1)`
    // and nothing in its slot): a new variable, the same resource `:var/new` makes.
    table.declareResources(
        ":var/new/empty",
        ResourceSpec{.allocates = Resource{.cls = ResourceClass::Variable}});
    // `std::mem::blocks()` reports the block counters of the process: state outside the program,
    // read and never written. Reads are dependencies and not effects, so the call is ordered by the
    // values it gives -- and it is not pure, because those values change without an argument
    // changing (design principles 3.3: an observer of a counter the program does not own).
    table.declareResources(":op/mem_blocks", externalAmbient("mm", AccessMode::Read));
    // `tee_note` writes the record of a tee into the report of a profile session: the profiler's
    // own ambient resource, written the way the profiler's operators write it.
    table.declareResources(
        ":stream/tee_note",
        ResourceSpec{
            .accesses       = {ambientAccess("profiler", AccessMode::ReadModifyWrite)},
            .externalEffect = true});
    // `:macro/cmp` is pure and declared in the pure list of `registerBuiltinOperatorTraits`: its
    // executor answers the null slot and the compiler makes no node of it
    // (src/compile/gct/builder.cpp, `ReservedDataOp::Comp`), and the two operands are function
    // values it never calls.
    // The operators that run a function the program hands them (`map`, `filter`, `foreach` and the
    // unordered loop of the same shape): what they touch is that function's, so the declaration
    // names the operand the function arrives in and not a resource (design principles 3.3).
    // It is the same rule a call through a function value already follows
    // (`Builder::reachOfCall`): a reader that has the value reads its function type, and `map` over
    // a pure function touches nothing at all -- itemizing "external" here made such a call enter
    // compare) would make such a call enter the sync chain and lose its freedom. A reader that has
    // only the table takes it as an effect outside the program that itemizes nothing, which is the
    // conservative reading a type without a clause gets too. The function is the first `with`
    // operand in all of them: src/builtin/operators/higher_order.cpp, `functionOf`, takes
    // `with.get<Function *>(0)`. The result is a new array (or the null slot), so nothing is
    // claimed about it either.
    for (const char *uri :
         {":mark/map_arr", ":mark/filter_arr", ":mark/foreach_arr", ":mark/unordered_foreach_arr"}) {
        table.declareResources(
            uri, ResourceSpec{.functionOperand = ResourceBinding{.kind = ResourceBinding::Kind::With}});
    }
    // The two that hand a value back out of the callback: `reduce` gives the accumulator it was
    // given or the value the last call made, and the marked call of `attempt(f)` gives what the
    // callee gave, which may be an object the callee holds. Nothing is known about either result,
    // and that stays even though the accesses are the function's. `reduce`'s function is its first
    // `with` operand too; `:mark/isolate`'s is its first ordinary one (operator_semantics.h,
    // `ElementCalls`, and the resolver of the marked call in tagged.cpp).
    for (const char *uri : {":mark/reduce_arr", ":mark/unordered_reduce_arr"}) {
        table.declareResources(
            uri,
            ResourceSpec{
                .result          = ResultAlias{.kind = ResultAlias::Kind::Unknown},
                .functionOperand = ResourceBinding{.kind = ResourceBinding::Kind::With}});
    }
    table.declareResources(
        ":mark/isolate",
        ResourceSpec{
            .result          = ResultAlias{.kind = ResultAlias::Kind::Unknown},
            .functionOperand = ResourceBinding{.kind = ResourceBinding::Kind::Norm}});
}
