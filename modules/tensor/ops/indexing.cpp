// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The index semantics of the tensor operators (see indexing.h). Each instantiation function
 * below states, in its comment, the instance it builds in the notation of the design; every
 * one mirrors the kernel it describes (modules/tensor/kernels), and the declaration evaluator
 * (passes/index_eval.cpp, test only) executes the instances and compares them with the
 * kernels bit for bit.
 *
 *   elementwise   add subtract multiply divide maximum minimum, neg abs exp log sqrt rsqrt
 *                 relu: one Parallel dim per result axis, every operand broadcast to it
 *   layout        transpose permute slice reshape flatten unsqueeze broadcast_like
 *                 expand_axis: a Unique write, each result element read from one element
 *   reductions    sum mean (to a number), sum_axis mean_axis max_axis min_axis, sum_to:
 *                 Reduction dims and a combining write, by a law per element type
 *   matmul        (batch..., m, n, k), k the reduction, NumPy's rank-1 and batch rules
 */

#include "indexing.h"

#include "../kernels/layout.h"
#include "support.h"

#include <algorithm>
#include <bit>
#include <format>
#include <limits>
#include <unordered_map>

namespace camel::tensor::ops {

using namespace camel::core::sem;
using camel::core::type::TypeCode;
using Rel = Constraint::Rel;

namespace {

using Fn = IndexFn;

// ---- Shared helpers -------------------------------------------------------------------------

/// The shape of operand `i` when its rank is known (extents may be unknown).
std::optional<StaticShape> shapeOf(const InferContext &ctx, size_t i) {
    return ctx.has(i) ? ctx.facts(i).shape : std::nullopt;
}

bool fullyKnown(const StaticShape &shape) {
    return std::ranges::none_of(shape, [](int64_t e) { return e == kUnknownDim; });
}

/// `axis` normalized against `rank` (negative counts from the end); nullopt out of range.
std::optional<size_t> normalizeAxis(int64_t axis, size_t rank) {
    const auto r = static_cast<int64_t>(rank);
    if (axis < 0) {
        axis += r;
    }
    return axis >= 0 && axis < r ? std::optional(static_cast<size_t>(axis)) : std::nullopt;
}

/// The coordinates d0, ..., of the dims appended so far, in order.
std::vector<ExprId> allDims(IndexBuilder &b) {
    std::vector<ExprId> out;
    for (size_t d = 0; d < b.instance.domain.dims.size(); ++d) {
        out.push_back(b.x().dim(static_cast<uint32_t>(d)));
    }
    return out;
}

/// Dims over `shape` (operand `operand`'s extents) with the given roles; returns them.
std::vector<ExprId> dimsOver(
    IndexBuilder &b, size_t operand, const StaticShape &shape,
    const std::vector<DimRole> &roles = {}) {
    std::vector<ExprId> out;
    for (size_t a = 0; a < shape.size(); ++a) {
        out.push_back(b.dim(
            b.extent(operand, a, shape[a]), roles.empty() ? DimRole::Parallel : roles[a]));
    }
    return out;
}

/**
 * The coordinates, in a tensor of extents `in`, of the element at the row-major linear index
 * `linear` (an expression of the dims): (l floordiv stride0, (l floordiv stride1) mod in1, ...).
 * Both shapes are known.
 */
std::vector<ExprId> delinearize(IndexBuilder &b, ExprId linear, const StaticShape &in) {
    std::vector<ExprId> coords(in.size());
    int64_t stride = 1;
    for (size_t a = in.size(); a-- > 0;) {
        ExprId c = b.x().floorDiv(linear, b.x().constant(std::max<int64_t>(stride, 1)));
        if (a > 0) {
            c = b.x().mod(c, b.x().constant(std::max<int64_t>(in[a], 1)));
        }
        coords[a] = c;
        stride *= in[a];
    }
    return coords;
}

/**
 * The coordinates in `in` of the element of `out` at the dims `dims`, when both shapes hold
 * the same elements in row-major order (reshape, flatten). When the two shapes are the same
 * once their extent-1 axes are dropped, the map is affine (each remaining axis reads the
 * matching dim, an extent-1 axis 0); otherwise it linearizes the dims and delinearizes.
 */
std::vector<ExprId> reshapeCoords(
    IndexBuilder &b, const StaticShape &in, const StaticShape &out,
    const std::vector<ExprId> &dims) {
    std::vector<size_t> inAxes, outAxes;
    for (size_t a = 0; a < in.size(); ++a) {
        if (in[a] != 1) {
            inAxes.push_back(a);
        }
    }
    for (size_t a = 0; a < out.size(); ++a) {
        if (out[a] != 1) {
            outAxes.push_back(a);
        }
    }
    const bool same = inAxes.size() == outAxes.size() &&
                      std::ranges::equal(inAxes, outAxes, [&](size_t i, size_t o) {
                          return in[i] == out[o];
                      });
    if (same) {
        std::vector<ExprId> coords(in.size(), b.x().constant(0));
        for (size_t k = 0; k < inAxes.size(); ++k) {
            coords[inAxes[k]] = dims[outAxes[k]];
        }
        return coords;
    }
    std::vector<int64_t> strides(out.size(), 1);
    for (size_t a = out.size(); a-- > 1;) {
        strides[a - 1] = strides[a] * out[a];
    }
    ExprId linear = b.x().constant(0);
    for (size_t a = 0; a < out.size(); ++a) {
        linear = b.x().add(linear, b.x().scale(strides[a], dims[a]));
    }
    return delinearize(b, linear, in);
}

// ---- Laws -----------------------------------------------------------------------------------

constexpr uint64_t kF32MinusZero = 0x80000000u;
constexpr uint64_t kF64MinusZero = 0x8000000000000000u;
constexpr uint64_t kF32MinusInf  = 0xff800000u;
constexpr uint64_t kF32PlusInf   = 0x7f800000u;
constexpr uint64_t kI64Lowest    = 0x8000000000000000u;
constexpr uint64_t kI64Highest   = 0x7fffffffffffffffu;

/// The laws of an operator are listed per element type in this order; the instance names the
/// one of its operand's dtype.
std::optional<uint8_t> lawIndex(std::optional<TypeCode> dtype) {
    if (dtype == TypeCode::Float32) {
        return 0;
    }
    if (dtype == TypeCode::Int64) {
        return 1;
    }
    if (dtype == TypeCode::Bool) {
        return 2;
    }
    return std::nullopt;
}

/// A sum from +0 in ascending order: float accumulators are not associative (only with
/// reassociate), integer ones are (wrapping). The identity of float addition is -0.
ReductionLaw sumLaw(std::string name, TypeCode in, TypeCode acc, TypeCode result) {
    const bool exact = acc == TypeCode::Int64;
    return ReductionLaw{
        .name             = std::move(name),
        .kind             = LawKind::Add,
        .input            = in,
        .accumulator      = acc,
        .result           = result,
        .exactAssociative = exact,
        .exactCommutative = true,
        .hasIdentity      = true,
        .identityBits     = acc == TypeCode::Float32   ? kF32MinusZero
                            : acc == TypeCode::Float64 ? kF64MinusZero
                                                       : 0,
    };
}

ReductionLaw meanLaw(std::string name, TypeCode in, TypeCode acc, TypeCode result, LawEmpty empty) {
    ReductionLaw law = sumLaw(std::move(name), in, acc, result);
    law.finalize     = LawFinalize::DivideByCount;
    law.empty        = empty;
    return law;
}

/// IEEE maximum or minimum from its identity: exactly associative and commutative.
ReductionLaw extremumLaw(std::string name, bool max, TypeCode type, LawEmpty empty) {
    uint64_t identity = 0;
    if (type == TypeCode::Float32) {
        identity = max ? kF32MinusInf : kF32PlusInf;
    } else if (type == TypeCode::Int64) {
        identity = max ? kI64Lowest : kI64Highest;
    } else {
        identity = max ? 0 : 1; // bool: false is below true
    }
    return ReductionLaw{
        .name             = std::move(name),
        .kind             = max ? LawKind::Max : LawKind::Min,
        .input            = type,
        .accumulator      = type,
        .result           = type,
        .initBits         = identity,
        .empty            = empty,
        .exactAssociative = true,
        .exactCommutative = true,
        .hasIdentity      = true,
        .identityBits     = identity,
    };
}

/// argmax / argmin: the position (int64) of the first extreme, from the first contribution.
/// The pair (value, position) ordered by the IEEE maximum (minimum) and then the earlier
/// position is exactly associative and commutative (the positions are distinct), so a back end
/// may regroup it if it carries the positions; an empty axis is a fault.
ReductionLaw positionLaw(std::string name, bool max, TypeCode type) {
    return ReductionLaw{
        .name             = std::move(name),
        .kind             = max ? LawKind::ArgMax : LawKind::ArgMin,
        .input            = type,
        .accumulator      = type,
        .result           = TypeCode::Int64,
        .init             = LawInit::FirstContribution,
        .empty            = LawEmpty::Fault,
        .exactAssociative = true,
        .exactCommutative = true,
    };
}

// ---- Elementwise ----------------------------------------------------------------------------

/**
 * The first `count` operands broadcast to the result (NumPy rules); a number operand is rank 0.
 *
 *   add(x: [m, n], b: [n])   domain d0 < s0_0, d1 < s0_1 parallel
 *                            norm 0 (d0, d1); norm 1 (d1); write (d0, d1) unique
 *
 * An operand of extent 1 on an axis reads coordinate 0 there. No instance when no operand is a
 * tensor, a rank is unknown, or an axis may or may not broadcast (Q9).
 */
Fn elementwise(size_t count) {
    return [count](const InferContext &ctx) -> std::optional<IndexedInstance> {
        if (!anyTensor(ctx, count)) {
            return std::nullopt;
        }
        std::vector<StaticShape> shapes;
        std::vector<size_t> operands;
        for (size_t i = 0; i < count && ctx.has(i); ++i) {
            const auto shape = shapeOf(ctx, i);
            if (!shape) {
                return std::nullopt;
            }
            shapes.push_back(*shape);
            operands.push_back(i);
        }
        IndexBuilder b;
        auto coords = b.broadcast(shapes, operands);
        if (!coords) {
            return std::nullopt;
        }
        for (auto &c : *coords) {
            b.instance.norm.push_back(IndexBuilder::affine(std::move(c)));
        }
        return b.finish(allDims(b));
    };
}

// ---- Layout ---------------------------------------------------------------------------------

/// The axes of `x` permuted: result axis j is input axis perm[j] (perm already normalized).
std::optional<IndexedInstance> permuted(const StaticShape &in, const std::vector<size_t> &perm, size_t extraOperands) {
    IndexBuilder b;
    std::vector<ExprId> coords(in.size(), kNoExpr);
    for (size_t j = 0; j < perm.size(); ++j) {
        coords[perm[j]] = b.dim(b.extent(0, perm[j], in[perm[j]]));
    }
    b.instance.norm.push_back(IndexBuilder::affine(std::move(coords)));
    for (size_t i = 0; i < extraOperands; ++i) {
        b.instance.norm.push_back(IndexBuilder::none());
    }
    return b.finish(allDims(b));
}

/**
 * transpose(x: [..., a, b]) -> [..., b, a]: x read at (..., d_last, d_last-1).
 */
std::optional<IndexedInstance> transposeIndexing(const InferContext &ctx) {
    const auto in = shapeOf(ctx, 0);
    if (!in || in->size() < 2) {
        return std::nullopt;
    }
    std::vector<size_t> perm(in->size());
    for (size_t a = 0; a < perm.size(); ++a) {
        perm[a] = a;
    }
    std::swap(perm[perm.size() - 1], perm[perm.size() - 2]);
    return permuted(*in, perm, 0);
}

/**
 * permute(x, perm) (perm constant; negative entries count from the end): result axis j is
 * input axis perm[j], so x is read at d_j on axis perm[j].
 */
std::optional<IndexedInstance> permuteIndexing(const InferContext &ctx) {
    const auto in   = shapeOf(ctx, 0);
    const auto perm = ctx.constInts(1);
    if (!in || !perm || perm->size() != in->size()) {
        return std::nullopt;
    }
    std::vector<size_t> axes;
    std::vector<bool> seen(in->size(), false);
    for (int64_t p : *perm) {
        const auto a = normalizeAxis(p, in->size());
        if (!a || seen[*a]) {
            return std::nullopt; // the kernel rejects the permutation
        }
        seen[*a] = true;
        axes.push_back(*a);
    }
    return permuted(*in, axes, 1);
}

/**
 * slice(x, axis, start, stop, step = 1), step > 0, with the bounds normalized as the kernel
 * does (a negative bound counts from the end). A bound still outside [0, n] is a fault (design
 * principles 3.9): the call then has no instance. A stop before the start is the empty range
 * (3.9):
 *
 *   params   n = extent(0, axis); axis and step (Static); start/stop (a Derived normalized
 *            value, known when the bound is a constant and the extent is known); the raw
 *            constant or Runtime value it came from; len (Derived)
 *   domain   one dim per axis; on `axis` d < len
 *   norm 0   (..., start + step*d_axis, ...)        within [0, n) by construction (Unreachable)
 *   where    0 <= start, start <= n, 0 <= stop, stop <= n, 1 <= step, 0 <= len
 *   check    a raw start/stop whose normalized value is not known at compile time: [-n, n], the
 *            bounds a slice accepts (a negative one counts from the end); outside it the
 *            operator faults with SliceOutOfRange before it reads anything
 *
 * A constant bound is normalized now when the extent is known; one outside [-extent, extent]
 * is the fault the operator raises, so the call has no instance. With the extent unknown a
 * constant bound is checked like a run-time one (whether it is in range depends on the extent).
 * A step that is not constant gives no instance (the operator faults on a non-positive step,
 * which the map cannot state).
 */
std::optional<IndexedInstance> sliceIndexing(const InferContext &ctx) {
    const auto in   = shapeOf(ctx, 0);
    const auto axis = ctx.constInt(1);
    if (!in || !axis) {
        return std::nullopt;
    }
    const auto a = normalizeAxis(*axis, in->size());
    if (!a) {
        return std::nullopt;
    }
    const bool stepGiven = ctx.has(4);
    const auto step      = stepGiven ? ctx.constInt(4) : std::optional<int64_t>(1);
    if (!step || *step <= 0) {
        return std::nullopt;
    }
    const int64_t extent = (*in)[*a];
    const auto startRaw  = ctx.constInt(2);
    const auto stopRaw   = ctx.constInt(3);
    // The normalized value of a bound when it is fixed: a constant bound with a known extent.
    std::optional<int64_t> startNorm, stopNorm;
    if (extent != kUnknownDim) {
        for (const auto &[raw, normalized] :
             {std::pair{startRaw, &startNorm}, std::pair{stopRaw, &stopNorm}}) {
            if (!raw) {
                continue;
            }
            const auto value = kernels::normalizeSliceBound(extent, *raw);
            if (!value) {
                return std::nullopt; // the operator faults with SliceOutOfRange
            }
            *normalized = *value;
        }
    }

    IndexBuilder b;
    b.constant("axis", 1, static_cast<int64_t>(*a));
    const ExprId stepP = stepGiven ? b.constant("step", 4, *step) : b.x().constant(1);
    const ExprId nP    = b.extent(0, *a, extent);
    // The value the access uses: the normalized bound, known only when the extent is. Normalizing
    // a negative bound is not quasi-affine (it branches on the sign), so the parameter states
    // its value or its range, not a definition.
    const ExprId startP = b.derived("start", startNorm, kNoExpr);
    const ExprId stopP  = b.derived("stop", stopNorm, kNoExpr);

    // A bound whose normalized value is not known goes through the check on entry: a slice
    // accepts exactly [-n, n], the values a negative bound counted from the end turns into
    // [0, n].
    const ExprId low = b.x().scale(-1, nP);
    const auto checkBound = [&](size_t operand, const char *name, std::optional<int64_t> raw,
                                std::optional<int64_t> normalized) {
        if (normalized) {
            return;
        }
        const ExprId param = raw ? b.constant(name, operand, *raw) : b.runtime(name, operand);
        b.check(operand, param, low, nP);
    };
    checkBound(2, "start_raw", startRaw, startNorm);
    checkBound(3, "stop_raw", stopRaw, stopNorm);

    std::optional<int64_t> len;
    if (startNorm && stopNorm) {
        len = *stopNorm > *startNorm ? (*stopNorm - *startNorm + *step - 1) / *step : 0;
    }
    const ExprId lenP = b.derived("len", len, kNoExpr);
    std::vector<ExprId> coords;
    for (size_t k = 0; k < in->size(); ++k) {
        if (k == *a) {
            const ExprId d = b.dim(lenP);
            coords.push_back(b.x().add(startP, b.x().mul(stepP, d)));
        } else {
            coords.push_back(b.dim(b.extent(0, k, (*in)[k])));
        }
    }
    for (const ExprId bound : {startP, stopP}) {
        b.require(b.x().constant(0), Rel::Le, bound);
        b.require(bound, Rel::Le, nP);
    }
    b.require(b.x().constant(0), Rel::Le, lenP);
    if (stepGiven) {
        b.require(b.x().constant(1), Rel::Le, stepP);
    }
    b.instance.norm = {IndexBuilder::affine(std::move(coords)), IndexBuilder::none(),
                       IndexBuilder::none(), IndexBuilder::none()};
    if (stepGiven) {
        b.instance.norm.push_back(IndexBuilder::none());
    }
    return b.finish(allDims(b));
}

/// Dims over a known result shape whose extents come from constant elements of operand
/// `shapeOperand` (reshape's target; -1 resolved: a derived parameter).
std::vector<ExprId> dimsOverTarget(
    IndexBuilder &b, const StaticShape &out, const std::optional<std::vector<int64_t>> &target,
    size_t shapeOperand) {
    std::vector<ExprId> dims;
    for (size_t j = 0; j < out.size(); ++j) {
        const bool written = target && j < target->size() && (*target)[j] >= 0;
        const ExprId e     = written ? b.constant(std::format("t{}", j), shapeOperand, out[j], j)
                                     : b.derived(std::format("t{}", j), out[j], kNoExpr);
        dims.push_back(b.dim(e));
    }
    return dims;
}

/**
 * reshape(x, shape): the elements in row-major order in a new shape. Both shapes known (the
 * target constant, one -1 resolved):
 *
 *   domain   d_j < t_j over the result
 *   norm 0   the input coordinates of linear index l = sum stride_j * d_j:
 *            (l floordiv istride_0, (l floordiv istride_1) mod a_1, ...)
 *            or, when the shapes agree once their extent-1 axes are dropped, the matching dims
 */
std::optional<IndexedInstance> reshapeIndexing(const InferContext &ctx) {
    const auto in     = shapeOf(ctx, 0);
    const auto target = ctx.constInts(1);
    if (!in || !fullyKnown(*in) || !target || !isTensorType(ctx.type(0))) {
        return std::nullopt;
    }
    StaticShape out;
    try {
        out = kernels::resolveReshape(*in, *target);
    } catch (const std::exception &) {
        return std::nullopt;
    }
    IndexBuilder b;
    for (size_t a = 0; a < in->size(); ++a) {
        b.extent(0, a, (*in)[a]); // the input extents, for the text form and the cache
    }
    const auto dims = dimsOverTarget(b, out, target, 1);
    b.instance.norm = {IndexBuilder::affine(reshapeCoords(b, *in, out, dims)), IndexBuilder::none()};
    return b.finish(dims);
}

/**
 * flatten(x, axis = 1) -> [d0 * ... * d(axis-1), d(axis) * ... * dn]: reshape's map, with the
 * input shape known and axis constant.
 */
std::optional<IndexedInstance> flattenIndexing(const InferContext &ctx) {
    const auto in   = shapeOf(ctx, 0);
    const auto axis = ctx.has(1) ? ctx.constInt(1) : std::optional<int64_t>(1);
    if (!in || !fullyKnown(*in) || !axis) {
        return std::nullopt;
    }
    const auto r = static_cast<int64_t>(in->size());
    if (*axis < -r || *axis > r) {
        return std::nullopt;
    }
    const auto split = static_cast<size_t>(*axis < 0 ? *axis + r : *axis);
    int64_t outer = 1, inner = 1;
    for (size_t d = 0; d < in->size(); ++d) {
        (d < split ? outer : inner) *= (*in)[d];
    }
    const StaticShape out = {outer, inner};
    IndexBuilder b;
    for (size_t a = 0; a < in->size(); ++a) {
        b.extent(0, a, (*in)[a]);
    }
    std::vector<ExprId> dims = {b.dim(b.derived("outer", outer, kNoExpr)),
                                b.dim(b.derived("inner", inner, kNoExpr))};
    b.instance.norm = {IndexBuilder::affine(reshapeCoords(b, *in, out, dims))};
    if (ctx.has(1)) {
        b.instance.norm.push_back(IndexBuilder::none());
    }
    return b.finish(dims);
}

/**
 * unsqueeze(x, axis): an axis of extent 1 inserted at `axis` (of the result's rank); x is read
 * at the other dims. Extents may be unknown.
 */
std::optional<IndexedInstance> unsqueezeIndexing(const InferContext &ctx) {
    const auto in   = shapeOf(ctx, 0);
    const auto axis = ctx.constInt(1);
    if (!in || !axis) {
        return std::nullopt;
    }
    const auto a = normalizeAxis(*axis, in->size() + 1);
    if (!a) {
        return std::nullopt;
    }
    IndexBuilder b;
    std::vector<ExprId> coords;
    for (size_t j = 0, k = 0; j <= in->size(); ++j) {
        if (j == *a) {
            b.dim(b.x().constant(1));
            continue;
        }
        coords.push_back(b.dim(b.extent(0, k, (*in)[k])));
        ++k;
    }
    b.instance.norm = {IndexBuilder::affine(std::move(coords)), IndexBuilder::none()};
    return b.finish(allDims(b));
}

/**
 * broadcast_like(g, like): g (a tensor, or a number) broadcast to like's shape; like's
 * elements are not read. g's extent-1 axes read 0. No instance when an axis of g may or may
 * not be 1 against like (Q9).
 */
std::optional<IndexedInstance> broadcastLikeIndexing(const InferContext &ctx) {
    const auto g    = shapeOf(ctx, 0);
    const auto like = shapeOf(ctx, 1);
    if (!g || !like || g->size() > like->size()) {
        return std::nullopt;
    }
    IndexBuilder b;
    const size_t shift = like->size() - g->size();
    std::vector<ExprId> coords(g->size());
    for (size_t j = 0; j < like->size(); ++j) {
        const int64_t L = (*like)[j];
        const ExprId d  = b.dim(b.extent(1, j, L));
        if (j < shift) {
            continue;
        }
        const int64_t G = (*g)[j - shift];
        if (G == 1) {
            coords[j - shift] = b.x().constant(0);
        } else if (G == kUnknownDim && L != 1) {
            return std::nullopt; // g may be 1 (broadcast) or L (identity): Q9
        } else if (G != kUnknownDim && L != kUnknownDim && G != L) {
            return std::nullopt; // does not broadcast: the kernel rejects it
        } else {
            coords[j - shift] = d;
        }
    }
    b.instance.norm = {IndexBuilder::affine(std::move(coords)), IndexBuilder::none()};
    return b.finish(allDims(b));
}

/**
 * expand_axis(g, like, axis, keepdims = false): g, the result of a reduction of like over
 * `axis`, spread back over like's shape. keepdims: g is broadcast (0 on `axis` and on its
 * other extent-1 axes). Otherwise the kernel first reshapes g to like's shape with 1 on the
 * axis (any g with that many elements), so with the shapes known g is read through reshape's
 * map; with an extent unknown, only when g's extents are like's (the reshape is the identity).
 */
std::optional<IndexedInstance> expandAxisIndexing(const InferContext &ctx) {
    const auto g        = shapeOf(ctx, 0);
    const auto like     = shapeOf(ctx, 1);
    const auto axis     = ctx.constInt(2);
    const auto keepDims = ctx.has(3) ? ctx.constBool(3) : std::optional<bool>(false);
    if (!g || !like || !axis || !keepDims) {
        return std::nullopt;
    }
    const auto a = normalizeAxis(*axis, like->size());
    if (!a || g->size() != like->size() - (*keepDims ? 0 : 1)) {
        return std::nullopt;
    }
    IndexBuilder b;
    if (!*keepDims && fullyKnown(*g) && fullyKnown(*like)) {
        // The kernel reshapes g to like's shape with 1 on the axis (any g with as many
        // elements), then broadcasts: g is read through reshape's map from that shape.
        StaticShape reduced = *like;
        reduced[*a]         = 1;
        int64_t gCount = 1, reducedCount = 1;
        for (int64_t e : *g) {
            gCount *= e;
        }
        for (int64_t e : reduced) {
            reducedCount *= e;
        }
        if (gCount != reducedCount) {
            return std::nullopt;
        }
        std::vector<ExprId> reducedCoords;
        for (size_t j = 0; j < like->size(); ++j) {
            const ExprId d = b.dim(b.extent(1, j, (*like)[j]));
            reducedCoords.push_back(reduced[j] == 1 ? b.x().constant(0) : d);
        }
        b.instance.norm = {
            IndexBuilder::affine(reshapeCoords(b, *g, reduced, reducedCoords)),
            IndexBuilder::none(),
            IndexBuilder::none()};
        if (ctx.has(3)) {
            b.instance.norm.push_back(IndexBuilder::none());
        }
        return b.finish(allDims(b));
    }
    std::vector<ExprId> coords;
    for (size_t j = 0, k = 0; j < like->size(); ++j) {
        const int64_t L = (*like)[j];
        const ExprId d  = b.dim(b.extent(1, j, L));
        if (j == *a) {
            if (*keepDims) {
                if ((*g)[k] != 1) {
                    return std::nullopt;
                }
                coords.push_back(b.x().constant(0));
                ++k;
            }
            continue;
        }
        const int64_t G = (*g)[k++];
        if (!*keepDims && (G == kUnknownDim || L == kUnknownDim || G != L)) {
            // An extent is not known: the reshape is known to be the identity (and the map
            // fixed) only when g's extents are like's.
            return std::nullopt;
        }
        if (G == 1) {
            coords.push_back(b.x().constant(0));
        } else if ((G == kUnknownDim && L != 1) || (G != kUnknownDim && L != kUnknownDim && G != L)) {
            return std::nullopt;
        } else {
            coords.push_back(d);
        }
    }
    b.instance.norm = {IndexBuilder::affine(std::move(coords)), IndexBuilder::none(), IndexBuilder::none()};
    if (ctx.has(3)) {
        b.instance.norm.push_back(IndexBuilder::none());
    }
    return b.finish(allDims(b));
}

// ---- Reductions -----------------------------------------------------------------------------

/**
 * sum(x), mean(x): every element, in row-major order, into one number.
 *
 *   domain   d_i < s0_i reduction, one per axis of x
 *   norm 0   (d0, d1, ...)
 *   write    () combine     law by x's dtype
 */
std::optional<IndexedInstance> reduceAllIndexing(const InferContext &ctx) {
    const auto in  = shapeOf(ctx, 0);
    const auto law = lawIndex(ctx.facts(0).dtype);
    if (!in || !law) {
        return std::nullopt;
    }
    IndexBuilder b;
    const auto dims =
        dimsOver(b, 0, *in, std::vector<DimRole>(in->size(), DimRole::Reduction));
    b.instance.norm = {IndexBuilder::affine(dims)};
    return b.finish({}, WriteMode::Combine, *law);
}

/**
 * sum_axis(x, axis, keepdims = false) and mean_axis, max_axis, min_axis:
 *
 *   x: [a, w, c], axis 1     domain d0 < s0_0 parallel, d1 < s0_1 reduction, d2 < s0_2 parallel
 *                            norm 0 (d0, d1, d2); write (d0, d2) combine (keepdims: (d0, 0, d2))
 */
std::optional<IndexedInstance> reduceAxisIndexing(const InferContext &ctx) {
    const auto in       = shapeOf(ctx, 0);
    const auto axis     = ctx.constInt(1);
    const auto keepDims = ctx.has(2) ? ctx.constBool(2) : std::optional<bool>(false);
    const auto law      = lawIndex(ctx.facts(0).dtype);
    if (!in || !axis || !keepDims || !law) {
        return std::nullopt;
    }
    const auto a = normalizeAxis(*axis, in->size());
    if (!a) {
        return std::nullopt;
    }
    IndexBuilder b;
    std::vector<DimRole> roles(in->size(), DimRole::Parallel);
    roles[*a]       = DimRole::Reduction;
    const auto dims = dimsOver(b, 0, *in, roles);
    std::vector<ExprId> out;
    for (size_t k = 0; k < dims.size(); ++k) {
        if (k != *a) {
            out.push_back(dims[k]);
        } else if (*keepDims) {
            out.push_back(b.x().constant(0));
        }
    }
    b.instance.norm = {IndexBuilder::affine(dims), IndexBuilder::none()};
    if (ctx.has(2)) {
        b.instance.norm.push_back(IndexBuilder::none());
    }
    return b.finish(std::move(out), WriteMode::Combine, *law);
}

/**
 * sum_to(g, like): g summed down to like's shape, which g broadcasts from. Both shapes known.
 * When they are equal the kernel returns g itself: an identity map, no arithmetic. Otherwise
 * every axis of g is a dim; it is a Reduction dim where like has no such axis or has extent 1
 * against g's larger extent, and the result is written at like's coordinates.
 */
std::optional<IndexedInstance> sumToIndexing(const InferContext &ctx) {
    const auto g    = shapeOf(ctx, 0);
    const auto like = shapeOf(ctx, 1);
    const auto law  = lawIndex(ctx.facts(0).dtype);
    if (!g || !like || !fullyKnown(*g) || !fullyKnown(*like) || !law || like->size() > g->size()) {
        return std::nullopt;
    }
    IndexBuilder b;
    if (*g == *like) {
        const auto dims = dimsOver(b, 0, *g);
        b.instance.norm = {IndexBuilder::affine(dims), IndexBuilder::none()};
        return b.finish(dims);
    }
    const size_t shift = g->size() - like->size();
    std::vector<DimRole> roles(g->size(), DimRole::Reduction);
    for (size_t k = shift; k < g->size(); ++k) {
        const int64_t L = (*like)[k - shift], G = (*g)[k];
        if (L == G) {
            roles[k] = DimRole::Parallel;
        } else if (L != 1) {
            return std::nullopt; // like does not broadcast to g: the kernel rejects it
        }
    }
    const auto dims = dimsOver(b, 0, *g, roles);
    std::vector<ExprId> out;
    for (size_t k = shift; k < g->size(); ++k) {
        out.push_back(roles[k] == DimRole::Parallel ? dims[k] : b.x().constant(0));
    }
    b.instance.norm = {IndexBuilder::affine(dims), IndexBuilder::none()};
    return b.finish(std::move(out), WriteMode::Combine, *law);
}

// ---- matmul ---------------------------------------------------------------------------------

/**
 * matmul(a, b), NumPy rules: a rank-1 operand is a row (a) or a column (b) that the result
 * does not keep; the batch axes (all but the last two) broadcast.
 *
 *   a: [B.., M, K], b: [B.., K, N]
 *   domain   batch dims parallel, m < M, n < N parallel, k < K reduction
 *   norm 0   (batch.., m, k)      norm 1 (batch.., k, n)
 *   write    (batch.., m, n) combine      law fma_f32 (f32) or mul_add_i64 (int64)
 *   where    the two K extents equal
 *
 * Operands of other or mixed dtypes (converted before the product) have no instance.
 */
std::optional<IndexedInstance> matmulIndexing(const InferContext &ctx) {
    const auto A = shapeOf(ctx, 0), B = shapeOf(ctx, 1);
    const auto da = ctx.facts(0).dtype, db = ctx.facts(1).dtype;
    if (!A || !B || A->empty() || B->empty() || da != db ||
        (da != TypeCode::Float32 && da != TypeCode::Int64)) {
        return std::nullopt;
    }
    const bool av = A->size() == 1, bv = B->size() == 1;
    const size_t aBatch = A->size() - (av ? 1 : 2), bBatch = B->size() - (bv ? 1 : 2);
    const int64_t Ka = A->back(), Kb = bv ? (*B)[0] : (*B)[B->size() - 2];
    if (Ka != kUnknownDim && Kb != kUnknownDim && Ka != Kb) {
        return std::nullopt;
    }
    IndexBuilder b;
    const StaticShape batchA(A->begin(), A->begin() + aBatch), batchB(B->begin(), B->begin() + bBatch);
    const StaticShape shapes[] = {batchA, batchB};
    const size_t operands[]    = {0, 1};
    auto batch                 = b.broadcast(shapes, operands);
    if (!batch) {
        return std::nullopt;
    }
    std::vector<ExprId> out = allDims(b);
    std::vector<ExprId> ca = (*batch)[0], cb = (*batch)[1];
    std::optional<ExprId> m, n;
    if (!av) {
        m = b.dim(b.extent(0, A->size() - 2, (*A)[A->size() - 2]));
        out.push_back(*m);
    }
    if (!bv) {
        n = b.dim(b.extent(1, B->size() - 1, B->back()));
        out.push_back(*n);
    }
    const ExprId kA = b.extent(0, A->size() - 1, Ka);
    const ExprId kB = b.extent(1, bv ? 0 : B->size() - 2, Kb);
    const ExprId k  = b.dim(Ka != kUnknownDim ? kA : kB, DimRole::Reduction);
    b.require(kA, Rel::Eq, kB);
    if (m) {
        ca.push_back(*m);
    }
    ca.push_back(k);
    cb.push_back(k);
    if (n) {
        cb.push_back(*n);
    }
    b.instance.norm = {IndexBuilder::affine(std::move(ca)), IndexBuilder::affine(std::move(cb))};
    return b.finish(std::move(out), WriteMode::Combine, da == TypeCode::Float32 ? 0 : 1);
}

// ---- The table ------------------------------------------------------------------------------

struct Indexing {
    Fn fn;
    std::vector<ReductionLaw> laws;
};

const std::unordered_map<std::string_view, Indexing> &indexings() {
    static const std::unordered_map<std::string_view, Indexing> table = [] {
        std::unordered_map<std::string_view, Indexing> t;
        const TypeCode f32 = TypeCode::Float32, f64 = TypeCode::Float64, i64 = TypeCode::Int64,
                       boolean = TypeCode::Bool;
        for (std::string_view name : {"add", "subtract", "multiply", "divide", "maximum", "minimum"}) {
            t.emplace(name, Indexing{elementwise(2), {}});
        }
        for (std::string_view name : {"neg", "abs", "exp", "log", "sqrt", "rsqrt", "relu"}) {
            t.emplace(name, Indexing{elementwise(1), {}});
        }
        t.emplace("transpose", Indexing{transposeIndexing, {}});
        t.emplace("permute", Indexing{permuteIndexing, {}});
        t.emplace("slice", Indexing{sliceIndexing, {}});
        t.emplace("reshape", Indexing{reshapeIndexing, {}});
        t.emplace("flatten", Indexing{flattenIndexing, {}});
        t.emplace("unsqueeze", Indexing{unsqueezeIndexing, {}});
        t.emplace("broadcast_like", Indexing{broadcastLikeIndexing, {}});
        t.emplace("expand_axis", Indexing{expandAxisIndexing, {}});

        // sum and mean of everything: f32 elements are added in f64; int64 and bool elements
        // exactly in int64, wrapping (design principles 7.7), and the result converted to f64
        // once (mean divides in f64 first).
        t.emplace(
            "sum",
            Indexing{
                reduceAllIndexing,
                {sumLaw("sum_f32", f32, f64, f64),
                 sumLaw("sum_i64", i64, i64, f64),
                 sumLaw("sum_bool", boolean, i64, f64)}});
        t.emplace(
            "mean",
            Indexing{
                reduceAllIndexing,
                {meanLaw("mean_f32", f32, f64, f64, LawEmpty::Fault),
                 meanLaw("mean_i64", i64, i64, f64, LawEmpty::Fault),
                 meanLaw("mean_bool", boolean, i64, f64, LawEmpty::Fault)}});
        // By axis: floats in f64 rounded once to f32, integers and bools exactly in int64.
        t.emplace(
            "sum_axis",
            Indexing{
                reduceAxisIndexing,
                {sumLaw("sum_axis_f32", f32, f64, f32),
                 sumLaw("sum_axis_i64", i64, i64, i64),
                 sumLaw("sum_axis_bool", boolean, i64, i64)}});
        // mean over an empty axis is 0 / 0: NaN, not a fault.
        t.emplace(
            "mean_axis",
            Indexing{
                reduceAxisIndexing,
                {meanLaw("mean_axis_f32", f32, f64, f32, LawEmpty::Initial),
                 meanLaw("mean_axis_i64", i64, i64, f32, LawEmpty::Initial),
                 meanLaw("mean_axis_bool", boolean, i64, f32, LawEmpty::Initial)}});
        t.emplace(
            "max_axis",
            Indexing{
                reduceAxisIndexing,
                {extremumLaw("max_axis_f32", true, f32, LawEmpty::Fault),
                 extremumLaw("max_axis_i64", true, i64, LawEmpty::Fault),
                 extremumLaw("max_axis_bool", true, boolean, LawEmpty::Fault)}});
        t.emplace(
            "min_axis",
            Indexing{
                reduceAxisIndexing,
                {extremumLaw("min_axis_f32", false, f32, LawEmpty::Fault),
                 extremumLaw("min_axis_i64", false, i64, LawEmpty::Fault),
                 extremumLaw("min_axis_bool", false, boolean, LawEmpty::Fault)}});
        // argmax and argmin over an axis: the position of the first extreme by IEEE maximum
        // (minimum), the first NaN if any, +0 above -0 (design principles 7.7).
        t.emplace(
            "argmax_axis",
            Indexing{
                reduceAxisIndexing,
                {positionLaw("argmax_axis_f32", true, f32),
                 positionLaw("argmax_axis_i64", true, i64),
                 positionLaw("argmax_axis_bool", true, boolean)}});
        t.emplace(
            "argmin_axis",
            Indexing{
                reduceAxisIndexing,
                {positionLaw("argmin_axis_f32", false, f32),
                 positionLaw("argmin_axis_i64", false, i64),
                 positionLaw("argmin_axis_bool", false, boolean)}});
        // sum_to is the gradient reduction of a broadcast, and it computes at the element type of
        // the tensors it is given: f32 accumulating from +0, and f64 accumulating from +0 as well.
        // An f64 gradient through a broadcast is an f64 result (questions-2026-10-23.md, section
        // f64-tensor-leaf: the element type of a value is always determined), so refusing it is a
        // missing arm and not a rule -- H238's direction is to support f64 too, and design
        // principles 5.1 forbids narrowing it to f32 to make the call go through. An int64 or bool
        // tensor is still refused by the kernel, which says so at the call.
        t.emplace(
            "sum_to",
            Indexing{sumToIndexing, {sumLaw("sum_to_f32", f32, f32, f32), sumLaw("sum_to_f64", f64, f64, f64)}});
        // matmul: the fused multiply-add chain in ascending k from +0 (design principles 7.1,
        // Q7); merging partial sums (with reassociate) is an addition, which commutes.
        t.emplace(
            "matmul",
            Indexing{
                matmulIndexing,
                {ReductionLaw{
                     .name             = "fma_f32",
                     .kind             = LawKind::FmaChain,
                     .input            = f32,
                     .accumulator      = f32,
                     .result           = f32,
                     .exactCommutative = true,
                     .hasIdentity      = true,
                     .identityBits     = kF32MinusZero,
                 },
                 ReductionLaw{
                     .name             = "mul_add_i64",
                     .kind             = LawKind::MulAdd,
                     .input            = i64,
                     .accumulator      = i64,
                     .result           = i64,
                     .exactAssociative = true,
                     .exactCommutative = true,
                     .hasIdentity      = true,
                 }}});
        return t;
    }();
    return table;
}

} // namespace

void attachIndexing(OpDef &def) {
    const auto &table = indexings();
    const auto it     = table.find(def.name);
    if (it == table.end() || !def.semantics) {
        return;
    }
    def.index            = it->second.fn;
    def.semantics->laws  = it->second.laws;
    // Version 1.1: the index semantics are facts added to what 1.0 declared (design 4.1).
    def.semantics->key.minor = std::max<uint16_t>(def.semantics->key.minor, 1);
}

std::optional<IndexedInstance> instantiate(const OpDef &def, const InferContext &ctx) {
    if (!def.index) {
        return std::nullopt;
    }
    std::optional<IndexedInstance> instance;
    try {
        instance = def.index(ctx);
    } catch (const std::exception &) {
        return std::nullopt;
    }
    if (instance && validate(*instance, def.semantics ? &*def.semantics : nullptr)) {
        return std::nullopt;
    }
    return instance;
}

} // namespace camel::tensor::ops
