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
 * The semantics of an operator: the versioned record that every pass and back end reads to
 * understand an operator it does not know by name (docs/design/op-semantics, sections 3, 4 and
 * 6; design principles 7).
 *
 * It sits beside the operator table's traits (camel/core/operator_table.h): the traits are the
 * five bits a hot path tests, the semantics are the richer facts a pass or a back end asks for
 * when it compiles. A module declares the semantics of its operators when it is loaded
 * (OperatorTable::declareSemantics); the record is then immutable and read without a lock.
 * Every part may be missing, and a missing part reads as the conservative answer.
 *
 * What is here (phase 1 of the design, section 12):
 *
 *   OperatorKey        the operator's identity outside the process, "tensor:matmul@1.0"
 *   NumericSemantics   what the operator itself computes: exact, correctly rounded by a
 *                      canonical algorithm, within a declared error bound, or not declared;
 *                      the special values; which program-granted permissions can change it
 *   FaultSpec          the faults it can raise (optional)
 *   Permission         the relaxations a program grants with the standard permission tags,
 *                      `finite_math` split into its four assumptions: one mask
 *
 * Phase 2 adds the index semantics (design section 5, camel/core/index_semantics.h):
 *
 *   indexing           the instantiation function: for the facts known at one call, the
 *                      iteration domain, the access map of every operand and the write map
 *   laws               the reduction laws (section 7) its instances combine contributions by
 *
 * Phase 4 adds the resources (design section 8, camel/core/resource_semantics.h):
 *
 *   resources          what the operator reads and writes besides its operands' values: which
 *                      operand names which resource, which part of it, how it is accessed, how
 *                      the result relates to it, and whether running it is visible outside
 *                      the program
 *   definition         a Camel function, exported by a library module, that is the operator's
 *                      normative definition (design section 9, camel/runtime/definitions.h)
 *
 * An operator's declaration says only what the operator does. Permissions are never part of
 * it: they are written by the program on its own code and stored on the graph, one mask per
 * node (camel/runtime/graph.h, the permission column). A module cannot widen them.
 */

#pragma once

#include "camel/core/index_semantics.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/symbol.h"
#include "camel/core/type/base.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace camel::core::sem {

// ---- Identity ----------------------------------------------------------------------------

/**
 * The identity of an operator's semantics outside the process. `protocol` and `name` are the
 * two halves of the operator's URI at its first ':' (":op/add_d" has an empty protocol and the
 * name "op/add_d"); the text form is "protocol:name@major.minor".
 *
 * The major version changes when the results, the faults, the accesses or the effects change;
 * the minor version when facts are added that are true of every earlier minor version. A
 * result computed from a declaration and kept outside the process (a cache) records the key;
 * OperatorTable::bind accepts it only while the major versions are equal and this process's
 * minor version is not lower.
 */
struct OperatorKey {
    SymId protocol = kEmptySym;
    SymId name     = kEmptySym;
    uint16_t major = 1;
    uint16_t minor = 0;

    /// The operator's URI, "protocol:name".
    std::string uri() const;
    /// "protocol:name@major.minor".
    std::string toString() const;
    /// The key of `uri` at the given version.
    static OperatorKey of(std::string_view uri, uint16_t major = 1, uint16_t minor = 0);
    /// Parses "protocol:name@major.minor"; nullopt when the text is not of that form.
    static std::optional<OperatorKey> parse(std::string_view text);

    bool operator==(const OperatorKey &) const = default;
};

// ---- Permissions (design principles 7.5) --------------------------------------------------

/**
 * One relaxation of the canonical algorithm, granted by the program with a tag on a function
 * or a block (`#[approx_math]`). The permissions do not imply each other. `finite_math` in the
 * source is the four assumptions kNoNans | kNoInfs | kNoSignedZeros | kFlushSubnormals
 * together.
 */
enum Permission : uint16_t {
    kReassociate      = 1 << 0, // reassociate:      reorder or regroup a reduction
    kContract         = 1 << 1, // contract:         a multiply and an add as one fused multiply-add
    kApproxMath       = 1 << 2, // approx_math:      approximate math functions
    kChangePrecision  = 1 << 3, // change_precision: lower or raise input or accumulation precision
    kNoNans           = 1 << 4, // finite_math:      assume no NaN
    kNoInfs           = 1 << 5, //                   assume no infinity
    kNoSignedZeros    = 1 << 6, //                   the sign of a zero does not matter
    kFlushSubnormals  = 1 << 7, //                   subnormal inputs and results may become zero
    kNonDeterministic = 1 << 8, // nondeterministic: the result may change with the schedule
};

/// A set of permissions: 0 when none is granted. Two bytes since `nondeterministic` (review of
/// H2b: the mask grows; there is no second column).
using PermissionMask = uint16_t;

inline constexpr PermissionMask kNoPermissions = 0;
inline constexpr PermissionMask kFiniteMath = kNoNans | kNoInfs | kNoSignedZeros | kFlushSubnormals;
inline constexpr PermissionMask kAllPermissions = 0xFFFF;

/// The source name of a single permission bit ("reassociate", ..., "no_nans", ...).
std::string_view permissionName(Permission permission);

/// The permissions a source name grants: one bit, or the four of "finite_math"; nullopt for a
/// name that is not a permission.
std::optional<PermissionMask> parsePermission(std::string_view name);

/// The names of a set, in bit order, separated by ", "; "finite_math" stands for its four
/// bits when all four are present. Empty for the empty set.
std::string permissionsText(PermissionMask mask);

// ---- Numeric semantics (design section 6.2) -----------------------------------------------

enum class NumericClass : uint8_t {
    /// Nothing declared: every permission is assumed to matter and nothing is known about
    /// rounding. The reading of an operator without a declaration.
    Opaque = 0,
    /// No rounding at all: moves, selections, sign operations, integer arithmetic (wrapping),
    /// IEEE maximum and minimum.
    Exact,
    /// A fixed sequence of correctly rounded IEEE steps: the canonical algorithm, the same
    /// bits on every back end and every device.
    CorrectlyRounded,
    /// A math function (exp, log, ...): within `ulpBound` units in the last place of the exact
    /// result; the special values are exact.
    Bounded,
};

/// The text form: "opaque", "exact", "correctly_rounded", "bounded".
std::string_view numericClassName(NumericClass cls);

/// How the operator treats special values (when it computes in floating point). `true` is the
/// IEEE behaviour; a false member says where the operator itself departs from it.
struct SpecialValues {
    bool nans        = true; // a NaN operand gives NaN
    bool infinities  = true; // infinities come in and go out as IEEE says
    bool signedZeros = true; // the sign of a zero result is IEEE's
    bool subnormals  = true; // subnormal inputs and results are kept, not flushed to zero
};

struct NumericSemantics {
    NumericClass cls = NumericClass::Opaque;
    /// Bounded only: the largest error, in ulps of the exact result.
    float ulpBound = 0;
    /// The floating-point type in which the steps are rounded (Float32, Float64); Void when the
    /// operator does no floating-point arithmetic. For an operator over several element types
    /// (the tensor operators), the type of its float case: on integer and bool elements every
    /// tensor operator is exact.
    type::TypeCode compute = type::TypeCode::Void;
    SpecialValues specials;
    /// The permissions that can change this operator's result. A consumer need not look for
    /// any other: e.g. an integer sum is exactly associative, so `reassociate` is not in it.
    PermissionMask sensitiveTo = kAllPermissions;
    /// The canonical algorithm, in words, for the reference and for diagnostics.
    std::string algorithm;
};

// ---- Numeric policy (design principles 7.3) -----------------------------------------

/**
 * Which relaxations a consumer applies on top of what the program grants. The permissions of a
 * node are the program's own (permission tags); a policy is the consumer's: the algebra pass of
 * 7.3 relaxes by default and takes `safe_fp=true` to be strict, a back end relaxes
 * the accumulation order by default and has a safe mode. They are the same question, so the
 * code asks it here: a rewrite or a schedule asks `permits(nodePermissions, required)`, and
 * reorderFreedom (camel/core/index_semantics.h) takes the policy with the node's permissions.
 */
struct NumericPolicy {
    /// Permissions every node has under this policy, with or without a tag.
    PermissionMask defaultGrants = kNoPermissions;
    /// Whether the permissions a node carries count. A strict policy ignores them: even a node
    /// with wider permissions keeps its canonical rounding path (7.4, safe mode).
    bool nodeGrants = true;

    /// Nothing but what the program writes (what the node's own permissions give).
    static constexpr NumericPolicy explicitOnly() { return {kNoPermissions, true}; }
    /// The default of 7.3: the order and grouping of a sum or product may change
    /// (kReassociate), nothing else is assumed. Precision, approximate math, fused multiply-add
    /// and the special values still need their permission.
    static constexpr NumericPolicy relaxed() { return {kReassociate, true}; }
    /// The safe mode (`safe_fp=true`): only what is exactly equivalent.
    static constexpr NumericPolicy strict() { return {kNoPermissions, false}; }

    /// The permissions a node with `node` has under this policy.
    constexpr PermissionMask effective(PermissionMask node) const {
        return static_cast<PermissionMask>(defaultGrants | (nodeGrants ? node : 0));
    }
    constexpr bool permits(PermissionMask node, PermissionMask required) const {
        return (effective(node) & required) == required;
    }
    bool operator==(const NumericPolicy &) const = default;
};

// ---- The numeric contract of a rewrite -----------------------------------------------------

/**
 * The error a rewrite may introduce, from the numeric contract of the operators it uses
 * (design principles 7.2-7.4; rewrite design 4, `check`). `declaredUlps` is the sum of the
 * declarations of the operators the rule's two sides use: none for an exact operator, one ulp
 * for a correctly rounded step, the declared `ulpBound` of a bounded math function, and one for
 * an operator that declares nothing. `steps` is the number of the rule's rounding steps.
 *
 * A rule that needs no permission must agree bit for bit. The permissions do not share one
 * uniform rounding bound:
 *   reassociate        moving the grouping may move each rounding: `steps + 1` ulps on top of
 *                      the declarations;
 *   contract           a fused multiply-add rounds once where the two sides round twice: no
 *                      extra ulps (the declarations already cover one rounding per step);
 *   approx_math        the bounded operator's declared `ulpBound` is in `declaredUlps`;
 *   change_precision   each step may round in bf16, the least precision 7.3 allows: 2^-8
 *                      relative per step;
 *   finite_math        the check excludes the NaN, infinity, signed zero and subnormal samples
 *                      the assumptions rule out; the finite values keep the rounding bound;
 *   nondeterministic   no numeric bound exists, so the check cannot certify the rule.
 */
struct RewriteErrorBound {
    /// False when the permissions have no numeric contract (`nondeterministic`): the check can
    /// compare nothing and reports the rule as unchecked.
    bool checkable = true;
    /// The allowed difference, in units of the element epsilon and of the magnitude.
    double ulps = 0.0;
    /// The allowed difference relative to the magnitude, independent of the element epsilon.
    double relative = 0.0;
};

/// The error one operator node may add, in ulps of its element type, from its declaration.
double stepUlps(const NumericSemantics &numeric);

/// The bound of a rule that needs `needs`, has `steps` rounding steps and whose operators
/// declare `declaredUlps` between them (see RewriteErrorBound).
RewriteErrorBound
rewriteErrorBound(PermissionMask needs, size_t steps, double declaredUlps);

// ---- Binary algebra (design section 7b) -----------------------------------------------------

/**
 * What a binary operator satisfies over one element type, declared beside the reduction laws
 * so that an algebraic rewrite selects operators by these properties and not by name. The
 * properties are about the exact operation: whether regrouping also keeps the bits is
 * `exactAssociative` (wrapping integers) and otherwise needs the policy's reassociate.
 */
struct BinaryLaw {
    /// The element type the law holds for (the scalar itself, or the element of a tensor).
    type::TypeCode element = type::TypeCode::Void;
    bool commutative       = false;
    bool exactAssociative  = false;
    /// The bit pattern, in `element`, of x with `x op neutral == x`; none when there is none.
    std::optional<uint64_t> neutralBits;
    /// The URI of the operator this one distributes over (a multiplication: the addition).
    std::string distributesOver;
    /// The URI of the operator that `x op y` is `x that (-y)` for (a subtraction: the
    /// addition). Empty for the others.
    std::string subtractionOf;

    bool operator==(const BinaryLaw &) const = default;
};

/// The laws of a binary operator, per element type, and how its operands and result map to the
/// element type they are laws about.
struct BinaryAlgebra {
    std::vector<BinaryLaw> laws;
    /// The element type of a value of this type, nullopt when the laws do not speak of it. An
    /// empty function reads a scalar number as its own element type. An elementwise operator over
    /// a tensor says so here: the laws then hold element by element, and a rewrite may only
    /// combine operands of the type of the result (no broadcasting).
    std::function<std::optional<type::TypeCode>(const type::Type *)> elementOf;
    /// The operator is elementwise over aggregates (tensors): a coefficient is a scalar constant.
    bool elementwise = false;
};

// ---- Faults ------------------------------------------------------------------------------

enum class FaultKind : uint8_t {
    DivisionByZero,
    IndexOutOfRange,
    EmptyReduction,
    ConversionRange,
    ShapeMismatch,
    Other,
};

std::string_view faultKindName(FaultKind kind);

struct FaultSpec {
    FaultKind kind = FaultKind::Other;
    /// The operand whose value triggers the fault (index among the ordinary operands), 0xFF
    /// when none in particular does.
    uint8_t operand = 0xFF;

    bool operator==(const FaultSpec &) const = default;
};

// ---- Definition (design section 9) ---------------------------------------------------------

/// The value an optional operand has when a call leaves it out (softmax's axis: -1).
using DefinitionDefault = std::variant<int64_t, double, bool>;

/// Where one parameter of a definition function gets its value at a call of the operator.
struct DefinitionParam {
    enum class Kind : uint8_t {
        Norm, // the ordinary operand `operand`
        With, // the with operand `operand`
    };
    Kind kind       = Kind::Norm;
    uint8_t operand = 0;
    /// The value of an optional operand the call leaves out; without one the operand is
    /// required (a call without it is not expanded).
    std::optional<DefinitionDefault> fallback;

    bool operator==(const DefinitionParam &) const = default;
};

/**
 * The definition of an operator (design section 9; design principles 7.1: "a definition graph
 * is normative"): a function written in Camel, exported by a library module, that computes
 * exactly what the operator computes: the axis, the numeric stabilization, the accumulator
 * type, every conversion. A kernel, fused or hand-written, must agree with it (bit for bit on
 * the correctly rounded and exact parts; within the declared bound on bounded functions).
 *
 * The library is compiled the first time a definition of it is asked for, in the program's
 * context, and its graphs are kept by the core (camel/runtime/definitions.h). Nothing expands
 * a definition unless a pass or a back end asks to (expandDefinition).
 */
struct Definition {
    /// The library module, by the name a program imports it with ("tensor_definitions"): a
    /// Camel file on the module search path. It imports modules written in C++ only.
    std::string module;
    /// The exported function that is the definition ("softmax").
    std::string function;
    /// One per parameter of the function, in order.
    std::vector<DefinitionParam> params;

    bool operator==(const Definition &) const = default;
};

// ---- Element calls (higher-order operators) ---------------------------------------------------

/// How an operator that calls a function value uses it.
enum class ElementShape : uint8_t {
    Map,     // the function on each element of an array: the results are the elements of the result
    Filter,  // the function on each element: the elements it answers true for
    Foreach, // the function on each element, for its effects; the operator gives nothing
    Fold,    // the function on the running value and each element, from an initial value
    Isolate, // the function once, with the remaining operands; a fault in it becomes an error
             // value (`attempt`)
};

/**
 * The declaration of a higher-order operator (design principles 3.6): it calls the function
 * value it receives, for the elements of an array or once, in the way `shape` says. Schedulers
 * choose how to run it from this record, not from the operator's name: the operands are the
 * array first and the function first among the with operands (Isolate: the function is the first
 * ordinary operand), a Fold's initial value is the second with operand.
 *
 * `independent` is the property that lets calls run at once: the calls on different elements do
 * not depend on one another, so they may run in any order or in parallel (a Fold: the program
 * leaves the order of combining open). An operator that does not declare it runs its calls in
 * order. The operator must be declared `callsGraphs` in its traits, and every such operator
 * has the native implementation (the function-call service, camel/execute/function_call.h) that a
 * scheduler without a fast path for its shape, or the macro evaluator, runs.
 */
struct ElementCalls {
    ElementShape shape = ElementShape::Map;
    bool independent   = false;

    bool operator==(const ElementCalls &) const = default;
};

// ---- The record --------------------------------------------------------------------------

/**
 * What the operator does besides compute its result (design principles 3.3), the fact the
 * `OperatorTraits` effect bits are derived from. `Undeclared`: nobody said; an operator is then
 * treated as effectful, as if declared `Effectful` (the difference is that `find` still says
 * nobody declared it). `Effectful`: it can change something outside the program's own
 * variables. `StateOnly`: it creates, reads or changes a variable and nothing outside the
 * program's variables. `Pure`: its result depends only on its arguments.
 */
enum class Effect : uint8_t { Undeclared, Pure, StateOnly, Effectful };

struct OperatorSemantics {
    OperatorKey key;
    /// The effect fact; `OperatorTraits::pure` and `stateOnly` are derived from it, never
    /// declared separately (operator_table.h, traitsOf).
    Effect effect = Effect::Undeclared;
    /// Output element i depends only on input elements i (after broadcasting). Also derived
    /// from a declared `binary.elementwise` law, so the two cannot disagree.
    bool elementwise = false;
    /// While it runs, the operator calls function values it received as arguments; also true
    /// for every operator with an `elementCalls` declaration.
    bool callsGraphs = false;
    /// It reads the graphs of the function values it receives instead of only calling them.
    bool readsGraphs = false;
    /// The index semantics (camel/core/index_semantics.h): the instance of one call. Empty when
    /// not declared: the operator reads its operands whole and writes its result whole, and
    /// nothing is known about which elements depend on which.
    InstantiateFn indexing;
    /// The reduction laws its instances name (IndexedInstance::law indexes this list): one
    /// per element type the operator reduces over, since the same operation is a different law
    /// on another type (design section 7).
    std::vector<ReductionLaw> laws;
    NumericSemantics numeric;
    /// The binary laws (section 7b); nullopt: not a binary operator the laws speak of.
    std::optional<BinaryAlgebra> binary;
    /// A higher-order operator's use of the function it calls; nullopt: not one.
    std::optional<ElementCalls> elementCalls;
    /// The faults the operator can raise. nullopt: not declared, the operator's mayFault trait
    /// decides ("may fault, anyhow"). An empty list: it never faults.
    std::optional<std::vector<FaultSpec>> faults;
    /// The resources (camel/core/resource_semantics.h). nullopt: not declared, read
    /// conservatively from the traits (conservativeResources): a pure operator touches none;
    /// any other reads and writes every operand and may return any of them.
    std::optional<ResourceSpec> resources;
    /// The definition graph (design section 9), nullopt when the operator has none.
    std::optional<Definition> definition;
};

} // namespace camel::core::sem
