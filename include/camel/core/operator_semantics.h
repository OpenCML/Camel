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
 * The semantics of an operator: the versioned record that every pass and back end reads to
 * understand an operator it does not know by name (docs/design/op-semantics, sections 3, 4 and
 * 6; design principles 2.13).
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
 *   Permission         the five permissions a program grants with @numeric(...), the fifth
 *                      (finite_math) split into its four assumptions: one byte
 *
 * Phase 2 adds the index semantics (design section 5, camel/core/index_semantics.h):
 *
 *   indexing           the instantiation function: for the facts known at one call, the
 *                      iteration domain, the access map of every operand and the write map
 *   laws               the reduction laws (section 7) its instances combine contributions by
 *
 * The resources and the definition graph (sections 8 and 9) come in phase 4, each a new
 * optional member of OperatorSemantics, absent until then.
 *
 * An operator's declaration says only what the operator does. Permissions are never part of
 * it: they are written by the program on its own code and stored on the graph, one byte per
 * node (camel/runtime/graph.h, the permission column). A module cannot widen them.
 */

#pragma once

#include "camel/core/index_semantics.h"
#include "camel/core/symbol.h"
#include "camel/core/type/base.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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

// ---- Permissions (design principles 2.13) --------------------------------------------------

/**
 * One relaxation of the canonical algorithm, granted by the program with @numeric(...) on a
 * function or a block. The permissions do not imply each other. `finite_math` in the source is
 * the four assumptions kNoNans | kNoInfs | kNoSignedZeros | kFlushSubnormals together.
 */
enum Permission : uint8_t {
    kReassociate     = 1 << 0, // reassociate:      reorder or regroup a reduction
    kContract        = 1 << 1, // contract:         a multiply and an add as one fused multiply-add
    kApproxMath      = 1 << 2, // approx_math:      approximate math functions
    kChangePrecision = 1 << 3, // change_precision: lower or raise input or accumulation precision
    kNoNans          = 1 << 4, // finite_math:      assume no NaN
    kNoInfs          = 1 << 5, //                   assume no infinity
    kNoSignedZeros   = 1 << 6, //                   the sign of a zero does not matter
    kFlushSubnormals = 1 << 7, //                   subnormal inputs and results may become zero
};

/// A set of permissions: one byte, 0 when none is granted.
using PermissionMask = uint8_t;

inline constexpr PermissionMask kNoPermissions  = 0;
inline constexpr PermissionMask kFiniteMath     = kNoNans | kNoInfs | kNoSignedZeros | kFlushSubnormals;
inline constexpr PermissionMask kAllPermissions = 0xFF;

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
    FaultKind kind  = FaultKind::Other;
    /// The operand whose value triggers the fault (index among the ordinary operands), 0xFF
    /// when none in particular does.
    uint8_t operand = 0xFF;

    bool operator==(const FaultSpec &) const = default;
};

// ---- The record --------------------------------------------------------------------------

struct OperatorSemantics {
    OperatorKey key;
    /// The index semantics (camel/core/index_semantics.h): the instance of one call. Empty when
    /// not declared: the operator reads its operands whole and writes its result whole, and
    /// nothing is known about which elements depend on which.
    InstantiateFn indexing;
    /// The reduction laws its instances name (IndexedInstance::law indexes this list): one
    /// per element type the operator reduces over, since the same operation is a different law
    /// on another type (design section 7).
    std::vector<ReductionLaw> laws;
    NumericSemantics numeric;
    /// The faults the operator can raise. nullopt: not declared, the operator's mayFault trait
    /// decides ("may fault, anyhow"). An empty list: it never faults.
    std::optional<std::vector<FaultSpec>> faults;
};

} // namespace camel::core::sem
