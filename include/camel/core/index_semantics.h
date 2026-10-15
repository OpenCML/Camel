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
 * The index semantics of an operator (docs/design/op-semantics, section 5): which elements of
 * which operands one result element is computed from, as data a pass or a back end can read
 * without knowing the operator by name.
 *
 * An operator does not declare one fixed map: its maps depend on the ranks, extents and
 * constant arguments of each call (an `add` of [m, n] and [n] is not the `add` of [n] and
 * [n]). It declares an instantiation function (InstantiateFn), which the operator table keeps
 * in the operator's semantics record and which, for the facts known at one call, returns an
 * IndexedInstance:
 *
 *   Domain        an iteration domain: integer tuples (d0, d1, ...), each dim with an extent
 *                 and a role (Parallel, Reduction, Sequential); symbolic parameters (an
 *                 extent of an operand, a constant argument, a runtime scalar, a derived
 *                 value) with their values when they are known; linear constraints on the
 *                 parameters; a guard (points outside it do not exist; see splitDims)
 *   Access        per operand: the element read at each point, one quasi-affine expression per
 *                 axis of the operand, the condition under which the coordinates are
 *                 meaningful and what happens outside it (OnInvalid); or an indirect access
 *                 (the coordinate on one axis is read from another operand: gather); or
 *                 "reads the whole operand" / "reads no element"
 *   Write         the element of the result each point contributes to, and whether points
 *                 meet there (Unique, Combine by the instance's reduction law, or overwrite in
 *                 the domain's order)
 *
 * The meaning: for every point t of the domain inside its guard, read every operand at
 * coords(t) (where valid(t) fails, by its OnInvalid policy), compute one contribution, and
 * write it to the result at the write coordinates. The points are visited in the canonical
 * order, the lexicographic order of the tuples; contributions that meet at one coordinate
 * combine in that order unless the reduction law (and the node's permissions) allow another.
 *
 * Expressions are the closed quasi-affine language of the design: constants, dims, parameters,
 * sums, a product of an expression with a constant or with a parameter, and floordiv and mod
 * by a positive constant or by a parameter the constraints require to be positive. They are
 * mathematical integers; a consumer that lowers them to machine integers proves the range or
 * checks it. Every instance owns its expressions in an ExprPool, interned so that two equal
 * expressions built the same way share one ExprId.
 *
 * What is not known gives no instance: an unknown rank, a broadcast where an extent may or
 * may not be 1 (design Q9, decided: no instance until the shape is known), a constant argument
 * that is not constant. A consumer reads "no instance" as "reads every operand whole, writes
 * the whole result, nothing known about the elements".
 *
 * The logical maps here know nothing of storage: a back end composes them with its own
 * storage map (rowMajorView gives the offset and strides of a row-major operand, which is what
 * Tiles calls a reindex).
 */

#pragma once

#include "camel/core/type/resolver.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::core {
/// The process-local identity of an operator (as in camel/core/operator_table.h).
using OperId = uint32_t;
} // namespace camel::core

namespace camel::core::sem {

// ---- Expressions ---------------------------------------------------------------------------

/// An expression of an instance's ExprPool.
using ExprId = uint32_t;
inline constexpr ExprId kNoExpr = ~ExprId{0};

enum class ExprOp : uint8_t {
    Const,    // value
    Dim,      // the dim at position `value` of the domain
    Param,    // the parameter at position `value` of the domain
    Add,      // lhs + rhs
    Mul,      // lhs * rhs, lhs a Const or a Param (the factor)
    FloorDiv, // lhs floordiv rhs (rounded towards negative infinity), rhs a positive Const or Param
    Mod,      // lhs mod rhs, in [0, rhs), rhs a positive Const or Param
};

struct Expr {
    ExprOp op     = ExprOp::Const;
    int64_t value = 0; // Const: the constant; Dim, Param: the position
    ExprId lhs    = kNoExpr;
    ExprId rhs    = kNoExpr;

    bool operator==(const Expr &) const = default;
};

/// The integer value of every parameter, by position; nullopt for one whose value is unknown.
using ParamValues = std::span<const std::optional<int64_t>>;

/// An expression as an affine function of the dims: constant + sum of coeffs[d] * dim d.
struct AffineForm {
    int64_t constant = 0;
    std::vector<int64_t> coeffs; // one per dim of the domain
};

/**
 * The expressions of one instance. Builders simplify as they go (constants fold, x + 0 and
 * 1 * x are x, a constant factor moves to the left of a product, a constant term to the right
 * of a sum) and intern the result, so building the same expression twice yields the same id.
 * A builder throws std::invalid_argument for a form outside the quasi-affine language (a
 * product of two non-constant expressions, a division by a non-positive constant or by an
 * expression that is not a parameter) and std::overflow_error when folding constants overflows
 * 64 bits.
 */
class ExprPool {
  public:
    ExprId constant(int64_t value);
    ExprId dim(uint32_t position);
    ExprId param(uint32_t position);
    ExprId add(ExprId lhs, ExprId rhs);
    ExprId sub(ExprId lhs, ExprId rhs);
    /// factor * e: `factor` is a Const or a Param.
    ExprId mul(ExprId factor, ExprId e);
    /// c * e.
    ExprId scale(int64_t c, ExprId e);
    /// lhs floordiv divisor: `divisor` is a Const > 0 or a Param.
    ExprId floorDiv(ExprId lhs, ExprId divisor);
    /// lhs mod divisor, in [0, divisor): `divisor` is a Const > 0 or a Param.
    ExprId mod(ExprId lhs, ExprId divisor);

    const Expr &operator[](ExprId id) const { return exprs_[id]; }
    size_t size() const { return exprs_.size(); }

    /// The value of a constant expression.
    std::optional<int64_t> constantValue(ExprId id) const;

    /// The value at a point: `dims` gives every dim, `params` every parameter. Throws
    /// std::invalid_argument when a parameter it needs is unknown, std::overflow_error when the
    /// value does not fit 64 bits.
    int64_t evaluate(ExprId id, std::span<const int64_t> dims, ParamValues params) const;

    /// The affine form in the dims, with the parameters replaced by their values; nullopt when
    /// a parameter it needs is unknown or a floordiv or mod depends on a dim.
    std::optional<AffineForm> affine(ExprId id, size_t dimCount, ParamValues params) const;

    /// True when the expression mentions a dim.
    bool mentionsDim(ExprId id) const;

    /// The text form, with the given names of dims and parameters: "d0 + 2*d1 - 1",
    /// "(d0 + d1) floordiv 4", "step*d0".
    std::string text(
        ExprId id, std::span<const std::string> dimNames,
        std::span<const std::string> paramNames) const;

  private:
    ExprId intern(Expr expr);
    /// Throws unless `divisor` is a positive constant or a parameter.
    void requireDivisor(ExprId divisor) const;

    struct ExprHash {
        size_t operator()(const Expr &e) const;
    };
    std::vector<Expr> exprs_;
    std::unordered_map<Expr, ExprId, ExprHash> interned_;
};

// ---- Domains -------------------------------------------------------------------------------

enum class DimRole : uint8_t {
    Parallel,   // the points along it are independent
    Reduction,  // contributions along it meet at one output coordinate (Combine by the law)
    Sequential, // contributions along it enter a recurrence in order (LOOP, phase 3a)
};

std::string_view dimRoleName(DimRole role);

enum class ParamSource : uint8_t {
    Extent,  // an extent of an operand: shape(operand)[axis]
    Static,  // a constant argument: operand (an int, or element `axis` of an int array)
    Runtime, // a scalar argument known only when the program runs: operand
    Derived, // computed from the others (`definition`, when it is quasi-affine)
};

std::string_view paramSourceName(ParamSource source);

/// A symbolic parameter of an instance. Analyses treat it as an unknown integer; a back end that
/// specializes knows its value and substitutes it.
struct Param {
    std::string name;
    ParamSource source = ParamSource::Extent;
    /// Extent, Static, Runtime: the operand (a norm operand) the value comes from.
    uint8_t operand = 0xFF;
    /// Extent: the axis. Static: the element of an int-array operand, 0xFF for a scalar.
    uint8_t axis = 0xFF;
    /// The value, when the types or the constant arguments fix it.
    std::optional<int64_t> value;
    /// Derived: its value as an expression of other parameters, when there is one in the
    /// quasi-affine language; kNoExpr otherwise (the constraints then say what is known).
    ExprId definition = kNoExpr;
};

/// lhs rel rhs, on mathematical integers.
struct Constraint {
    enum class Rel : uint8_t { Eq, Le, Lt };
    ExprId lhs = kNoExpr;
    Rel rel    = Rel::Le;
    ExprId rhs = kNoExpr;
};

/// A conjunction of constraints; empty means "always".
using Predicate = std::vector<Constraint>;

struct Dim {
    std::string name;
    ExprId extent = kNoExpr; // the dim ranges over [0, extent)
    DimRole role  = DimRole::Parallel;
};

struct Domain {
    /// In canonical order: the order of the points is the lexicographic order of the tuples.
    std::vector<Dim> dims;
    std::vector<Param> params;
    /// What is true of the parameters (n >= 0, step > 0, a * b = p * q, ...): preconditions an
    /// analysis may assume, guaranteed by the operator's checks before it computes anything.
    Predicate constraints;
    /// Points of the box where the guard fails do not exist: they read, contribute and write
    /// nothing. Empty for a declared instance; splitDims adds the original bounds here.
    Predicate guard;

    /// The value of every parameter (nullopt where unknown), by position.
    std::vector<std::optional<int64_t>> paramValues() const;
};

// ---- Accesses and the write ---------------------------------------------------------------

/// What happens at a point where an access's validity condition fails.
enum class OnInvalid : uint8_t {
    Unreachable, // the declaration claims the condition always holds inside the domain
    Fault,       // reaching such a point is a fault (FaultKind::IndexOutOfRange), before any write
    Excluded,    // the contribution does not exist (a reduction skips it, a write does not happen)
    Padding,     // the read yields the value whose bits are `padding`
};

std::string_view onInvalidName(OnInvalid policy);

/// The coordinate on one axis of an operand comes from the elements of another operand
/// (gather; the same form for a write coordinate is scatter). Design section 5.6.
struct Indirect {
    uint8_t indexOperand = 0xFF;     // the norm operand holding the indices
    std::vector<ExprId> indexCoords; // where in indexOperand the index for this point is read
    uint8_t axis = 0;                // the axis of the addressed operand it gives
};

enum class AccessKind : uint8_t {
    Affine,   // coords: one quasi-affine expression per axis of the operand
    Indirect, // coords as Affine, except on `indirect.axis` whose coordinate is read (5.6)
    Whole,    // reads the operand, which elements is not declared (the conservative reading)
    None,     // reads no element: a constant argument, or an operand only its type is used of
};

std::string_view accessKindName(AccessKind kind);

struct Access {
    AccessKind kind = AccessKind::Whole;
    std::vector<ExprId> coords;
    /// Where the coordinates are meaningful; empty: everywhere.
    Predicate valid;
    OnInvalid onInvalid = OnInvalid::Unreachable;
    /// Padding: the bits of the value read, in the operand's element type.
    uint64_t padding = 0;
    std::optional<Indirect> indirect;
};

enum class WriteMode : uint8_t {
    Unique,           // no two points write the same coordinate
    Combine,          // points that meet combine by the instance's law (IndexedInstance::law)
    OrderedOverwrite, // the last point in canonical order wins
};

std::string_view writeModeName(WriteMode mode);

struct Write {
    /// One expression per axis of the result; empty for a scalar result.
    std::vector<ExprId> coords;
    /// Where a point writes; elsewhere it writes nothing.
    Predicate valid;
    WriteMode mode = WriteMode::Unique;
    /// A scatter: the coordinate on `indirect->axis` is read from another operand.
    std::optional<Indirect> indirect;
};

/// No reduction law: IndexedInstance::law of an instance whose writes never meet.
inline constexpr uint8_t kNoLaw = 0xFF;

/// The index semantics of one call of an operator.
struct IndexedInstance {
    ExprPool exprs;
    Domain domain;
    /// One access per operand, in the order of the call's arguments.
    std::vector<Access> norm, with;
    Write output;
    /// Combine: the reduction law (OperatorSemantics::laws) that combines the contributions
    /// meeting at one result coordinate; kNoLaw otherwise.
    uint8_t law = kNoLaw;

    /// The value of every parameter, by position (Domain::paramValues).
    std::vector<std::optional<int64_t>> paramValues() const { return domain.paramValues(); }
    /// The extent of every dim, when the parameters it needs are known.
    std::optional<std::vector<int64_t>> extents() const;
};

// ---- Instantiation ------------------------------------------------------------------------

/**
 * What is known at one call of an operator: the types of its operands as the call sees them
 * (a tensor type carries its dtype and shape; an unknown extent is -1, an unknown rank no
 * shape at all) and the values of the norm operands that are constants (aligned with `norm`;
 * an int array written in place, `[n, 3]`, has its constant elements).
 */
struct InstanceContext {
    std::span<type::Type *const> norm;
    std::span<type::Type *const> with;
    type::static_args_t statics;
};

/**
 * An operator's instantiation function: the instance for one call, or nullopt when the call's
 * facts do not fix it (an unknown rank, a broadcast that may or may not happen, a constant
 * argument that is not constant, arguments the operator rejects). A pure function of its
 * arguments, which must work for every rank the operator accepts, as its type rule does.
 */
using InstantiateFn = std::function<std::optional<IndexedInstance>(const InstanceContext &)>;

struct OperatorSemantics;

/**
 * The structural checks an instance passes before anyone sees it: every ExprId in range;
 * every dim has an extent that does not mention a dim; every divisor of a floordiv or mod is
 * a positive constant, or a parameter whose known value is positive or that the constraints
 * require to be at least 1; a Combine write names a law the semantics declare (when
 * `semantics` is given), and a domain with a Reduction dim does not write Unique. Returns the
 * first problem in words, nullopt when there is none.
 */
std::optional<std::string>
validate(const IndexedInstance &instance, const OperatorSemantics *semantics = nullptr);

/**
 * The instance of the operator `id` for `cx`: nullopt when the operator declares no index
 * semantics, when its instantiation function finds no instance, or when the function fails
 * (throws, or returns an instance that does not validate: a defect of the declaration, which
 * the property tests are there to find; it is read as "no instance", the conservative answer).
 */
std::optional<IndexedInstance> instantiate(OperId id, const InstanceContext &cx);

// ---- Text form -------------------------------------------------------------------------------

/**
 * The text form of an instance, one line per part, for `camel inspect --instances`, for
 * diagnostics and as the canonical serialization a persistent cache keys on: it names every
 * parameter with its source and value, every dim with its extent and role, every constraint,
 * the guard, every access and the write. `semantics` (optional) supplies the law's name.
 *
 *   params   s0_0 = extent(0, 0) = 2; s0_1 = extent(0, 1) = 3
 *   domain   d0 < s0_0 parallel; d1 < s0_1 parallel
 *   norm 0   (d0, d1)
 *   norm 1   (d1)
 *   write    (d0, d1) unique
 */
std::string toString(const IndexedInstance &instance, const OperatorSemantics *semantics = nullptr);

// ---- Transformations ------------------------------------------------------------------------

/// Dim `dim` becomes (outer, inner) with dim = factor * outer + inner, inner < factor.
struct DimSplit {
    uint32_t dim   = 0;
    int64_t factor = 1;
};

/**
 * The instance with dims split for tiling (design 5.2, example 5): each split dim d of extent E
 * is replaced, in place, by an outer dim of extent (E + f - 1) floordiv f and an inner dim of
 * extent f, both of d's role, and every expression is rewritten with d := f * outer + inner.
 * The original bound f * outer + inner < E is added to the guard: in the tail, the points past
 * E do not exist (they write nothing and contribute nothing; a reduction excludes them, which
 * is what the canonical algorithm does, not padding with zeros). Canonical order is preserved on
 * the points that exist. Throws std::invalid_argument for a dim out of range or a factor < 1.
 */
IndexedInstance splitDims(const IndexedInstance &instance, std::span<const DimSplit> splits);

/// The offset and the strides of an access to a row-major operand: the element read at point
/// t is at linear index offset + sum strides[d] * t[d].
struct StridedView {
    int64_t offset = 0;
    std::vector<int64_t> strides; // one per dim of the domain
};

/**
 * The access composed with the row-major storage of an operand of extents `shape`: what Tiles
 * calls a reindex. nullopt when the access is not Affine, its rank differs from `shape`'s, a
 * parameter it needs is unknown, or a coordinate is not affine in the dims (a floordiv or mod
 * of a dim: reshape across axes is not strided).
 */
std::optional<StridedView> rowMajorView(
    const IndexedInstance &instance, const Access &access, std::span<const int64_t> shape);

/// True when `predicate` holds at `point` (every parameter it needs known; throws as
/// ExprPool::evaluate otherwise). For consumers and tests that check conditions on known shapes.
bool holds(
    const IndexedInstance &instance, const Predicate &predicate, std::span<const int64_t> point);

} // namespace camel::core::sem
