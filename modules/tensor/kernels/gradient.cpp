// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Gradient kernels (see gradient.h).
 */

#include "gradient.h"

#include "cpu.h"
#include "gemm.h"
#include "layout.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace camel::tensor::kernels {

using type::TypeCode;

namespace {

/// The gradient and update kernels compute in f32 only. An f64 or integer tensor is refused,
/// naming the argument type and the supported set, never narrowed to f32 (design principles
/// 5.1: no implicit conversion between numeric types).
const Array *asFloat(const Array *t, mm::IAllocator &allocator) {
    (void)allocator;
    if (t->dtype() != TypeCode::Float32) {
        refuseTensorElementType("the gradient kernels", t, {TypeCode::Float32});
    }
    return t;
}

std::string shapeText(std::span<const int64_t> shape) {
    std::string text = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        text += (i ? ", " : "") + std::to_string(shape[i]);
    }
    return text + "]";
}

/// Strides of `small` (right-aligned against `big`, broadcast axes stride 0), or throws when
/// `small` does not broadcast to `big`.
std::vector<int64_t>
broadcastStrides(std::span<const int64_t> small, std::span<const int64_t> big, const char *what) {
    if (small.size() > big.size()) {
        throw ShapeError(
            std::string(what) + ": cannot broadcast " + shapeText(small) + " to " + shapeText(big));
    }
    std::vector<int64_t> strides(big.size(), 0);
    int64_t stride     = 1;
    const size_t shift = big.size() - small.size();
    for (size_t d = small.size(); d-- > 0;) {
        const int64_t extent = small[d];
        if (extent != 1 && extent != big[d + shift]) {
            throw ShapeError(
                std::string(what) + ": cannot broadcast " + shapeText(small) + " to " +
                shapeText(big));
        }
        strides[d + shift] = extent == 1 ? 0 : stride;
        stride *= extent;
    }
    return strides;
}

/// Calls fn(bigIndex, smallIndex) for every element of `big`, where smallIndex is the element
/// of the broadcast operand it reads.
template <typename Fn>
void forEachBroadcast(std::span<const int64_t> big, const std::vector<int64_t> &strides, Fn &&fn) {
    const size_t rank = big.size();
    const uint64_t n  = numelOf(big);
    std::vector<int64_t> counter(rank, 0);
    int64_t small = 0;
    for (uint64_t i = 0; i < n; ++i) {
        fn(i, small);
        for (size_t d = rank; d-- > 0;) {
            if (++counter[d] < big[d]) {
                small += strides[d];
                break;
            }
            small -= strides[d] * (big[d] - 1);
            counter[d] = 0;
        }
    }
}

/// The axes of a broadcast as three contiguous runs in memory: the kept axes before the reduced
/// block, the reduced block itself, and the kept axes after it (`outer`, `reduced`, `inner`, in
/// the order the input is laid out).  The input is then a sequence of `outer * reduced`
/// contiguous runs of `inner` elements, and every output element owns `reduced` of those -- one
/// run when `inner > 1`, one contiguous block when `inner == 1` -- so `sum_to` and
/// `broadcast_to` touch whole runs instead of walking every element through an odometer of rank
/// counters.  A decomposition that would not describe the memory layout is refused, and the
/// caller keeps the general index walk.
struct BroadcastRuns {
    int64_t outer   = 1;
    int64_t reduced = 1;
    int64_t inner   = 1;
};

std::optional<BroadcastRuns>
broadcastRuns(std::span<const int64_t> big, const std::vector<int64_t> &strides, int64_t numel) {
    // An empty tensor has no element to read, so there is no run to describe and the divisions
    // below have no operand: an axis of extent 0 enters the inner run through its stride and makes
    // `inner` zero, and `numel` is zero with it, so the ratio that closes this function would be
    // 0/0.  The general walk visits no element and produces the empty result the operator already
    // answers with.
    if (numel == 0) {
        return std::nullopt;
    }
    BroadcastRuns runs;
    size_t d = big.size();
    // The kept axes after the reduced block, innermost first. `strides[d - 1] != 0` says the axis
    // is kept, and it is the number of elements `big` still has after that axis, which is exactly
    // `inner` once the axes below it are in the run -- that equality is what makes the axes one
    // contiguous region.
    bool contiguousInner = true;
    while (d > 0 && (big[d - 1] == 1 || strides[d - 1] == runs.inner)) {
        // A size-1 axis adds nothing to `inner`, but only a broadcast one (stride 0) is free of
        // the run: any other size-1 axis observes the extent of the input axis below it, so its
        // stride is the run's.  Taking one at another stride sums a block of the wrong length from
        // the wrong offset -- `[3, 1, 256]` summed to `[1, 1, 256]` is exactly that axis.
        contiguousInner &= big[d - 1] == 1 || strides[d - 1] == runs.inner;
        if (big[d - 1] == 1 && strides[d - 1] != 0 && strides[d - 1] != runs.inner) {
            contiguousInner = false;
        }
        runs.inner *= big[d - 1];
        --d;
    }
    // A kept axis *below* a reduced one is not part of the inner run however its stride lines up:
    // the reduced axis above it has stride 0, which is a hole. Without this the walk reads that 0
    // as "not the next inner axis", stops the inner run one axis early, and then sums a block of
    // the wrong length from the wrong offset -- the `[4, 2, 3, 4]` to `[4, 2, 1, 4]` case of the
    // declaration evaluator is that shape (a kept axis between two broadcast ones).
    if (d > 0 && !(big[d - 1] == 1 || strides[d - 1] == 0)) {
        contiguousInner = false;
    }
    // The reduced block: wide in `big`, broadcast in `small`.
    while (d > 0 && (big[d - 1] == 1 || strides[d - 1] == 0)) {
        runs.reduced *= big[d - 1];
        --d;
    }
    if (!contiguousInner) {
        return std::nullopt;
    }
    // The kept axes before it: every one of them is a lane the index walk visits, and the walk is
    // built from `big` and these strides.  An outer axis the operand really has (extent > 1, with
    // a stride of its own) sits between the runs of the lanes below it, so those lanes' reads are
    // not the runs the walk computes: refused, and the index walk runs instead.
    while (d > 0) {
        if (big[d - 1] != 1 && strides[d - 1] != 0) {
            return std::nullopt;
        }
        runs.outer *= big[d - 1];
        --d;
    }
    // Every element of the input is read exactly once, one read per (lane, reduced index).  A
    // broadcast axis observes the extent of the input axis it came from, so the reduced axes must
    // account for `numel / (outer * inner)` elements and no more.  Without this the walk below
    // reads past the input: a `[512, 1]` summed to `[512, 256]` -- a reversed broadcast, which
    // the operator allows -- has both axes at stride 0, and this decomposition would read that as
    // 512 reduced elements over 256 lanes of one element each.
    if (runs.reduced != numel / (runs.outer * runs.inner)) {
        return std::nullopt;
    }
    return runs;
}

} // namespace

/// Sum of `g` over the axes that `shape` broadcasts away: the backward of a broadcast.
///
/// Accumulation type: f32. The input is converted to f32 (`asFloat` above) whatever its element
/// type, the accumulator is f32 and the answer is f32, so the additions happen at the width of
/// the values being added: nothing is accumulated below the type it is given, and no permission
/// enters. The rule that a floating-point reduction accumulates in f64 and rounds once at the end
/// (design principles 7.3, ruling reduced-precision.md (d)) is stated for the reductions that
/// return their input's element type — `array::sum` and the tensor axis `sum` — and this helper
/// is neither: it is the module's broadcast backward and answers f32 by construction. Raising
/// this accumulator to f64 would move the bits of every f32 gradient computed through it, which
/// is a decision of its own rather than a consequence of (d), so it is left as it is.
Array *
sumTo(const Array *g, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    if (g->shapeSpan().size() == shape.size() &&
        std::equal(shape.begin(), shape.end(), g->shapeSpan().begin())) {
        return const_cast<Array *>(g);
    }
    const Array *src = asFloat(g, allocator);
    const auto strides      = broadcastStrides(shape, src->shapeSpan(), "sum_to");
    Array *out       = createTensor(TypeCode::Float32, shape, allocator, true);
    const float *in         = src->dataAs<float>();
    float *acc              = out->dataAs<float>();
    const auto big          = src->shapeSpan();
    // Both shapes the step issues are three runs: the bias and gain gradients reduce leading axes
    // ([512, 256] to [256], 512 inputs per output element) and the RMSNorm denominator's gradient
    // reduces the trailing one ([512, 256] to [512, 1], 256 contiguous inputs per output).  Each
    // output's contributions are added in the order the index walk visits them, into the same f32
    // accumulator, so this is the reduction the declaration folds and the walk below computes --
    // the licence and the accumulation type do not move.  What it does not do is walk 131072
    // elements through a rank-2 odometer.
    if (const auto runs = broadcastRuns(big, strides, static_cast<int64_t>(src->numel()))) {
        const int64_t lanes = runs->outer * runs->inner;
        // One unit is one element of the input, whichever branch runs: a lane's `reduced`
        // contributions plus, when the run is a gather, the span of outputs it feeds.
        const int64_t grain = std::max<int64_t>(
            1, grainForWork(kNsPerFloat * 2.0) / std::max<int64_t>(runs->reduced * runs->inner, 1));
        if (runs->inner == 1) {
            // One output element owns one contiguous run of the input, and its additions are a
            // dependent chain -- one accumulator, in index order, is the declared reduction.
            parallelFor(lanes, grain, [&](int64_t begin, int64_t end) {
                for (int64_t lane = begin; lane < end; ++lane) {
                    const float *row = in + lane * runs->reduced;
                    float sum        = 0.0f;
                    for (int64_t r = 0; r < runs->reduced; ++r) {
                        sum += row[r];
                    }
                    acc[lane] = sum;
                }
            });
            return out;
        }
        // The reduced axes are the outermost of the input, so this is a gather: a run of `inner`
        // consecutive inputs belongs to `inner` consecutive outputs.  The run is the vector loop
        // and each output still adds its contributions in ascending order of the reduced index,
        // which is the order the index walk visits them.
        parallelFor(lanes, grain, [&](int64_t begin, int64_t end) {
            for (int64_t lane = begin; lane < end;) {
                const int64_t block = lane / runs->inner;
                const int64_t first = lane % runs->inner;
                const int64_t stop  = std::min(end, (block + 1) * runs->inner);
                float *held         = acc + block * runs->inner + first;
                const float *plane  = in + (block * runs->reduced) * runs->inner + first;
                for (int64_t r = 0; r < runs->reduced; ++r) {
                    const float *row = plane + r * runs->inner;
                    for (int64_t k = 0; k < stop - first; ++k) {
                        held[k] += row[k];
                    }
                }
                lane = stop;
            }
        });
        return out;
    }
    // Interleaved: a kept axis between two reduced ones, so no run is contiguous. Kept for the
    // shapes no run describes.
    forEachBroadcast(big, strides, [&](uint64_t i, int64_t j) { acc[j] += in[i]; });
    return out;
}

/// A pure layout op: every element type is copied as it is, so a bool or int64 tensor broadcasts
/// without being converted (the element it is written with is the element it keeps).
Array *
broadcastTo(const Array *t, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    const auto strides = broadcastStrides(t->shapeSpan(), shape, "broadcast");
    Array *out         = createTensor(t->dtype(), shape, allocator);
    dispatchDType(t->dtype(), [&]<typename T>() {
        const T *in = t->dataAs<T>();
        T *dst      = out->dataAs<T>();
        // The same three runs as `sum_to` (see BroadcastRuns), read the other way: the reduction
        // that shaped the gradient wrote one contiguous run per output element, and this writes
        // that run back.  `inner == 1` is the reduction axis and becomes a fill of `reduced`
        // contiguous elements; otherwise each run of `inner` elements is copied `reduced` times.
        // An expand of a reduction along the trailing axis -- the mean and sum VJPs of the step --
        // is the first case: 256 floats written per source element, in one vectorizable loop,
        // instead of 256 odometer steps of the rank-2 counter.
        if (const auto runs = broadcastRuns(shape, strides, static_cast<int64_t>(t->numel()))) {
            if (runs->inner == 1) {
                const int64_t grain =
                    std::max<int64_t>(1, grainForWork(kNsPerFloat * runs->reduced));
                parallelFor(runs->outer, grain, [&](int64_t begin, int64_t end) {
                    for (int64_t lane = begin; lane < end; ++lane) {
                        T *run       = dst + lane * runs->reduced;
                        const T held = in[lane];
                        for (int64_t r = 0; r < runs->reduced; ++r) {
                            run[r] = held;
                        }
                    }
                });
            } else {
                // One contiguous run of `inner` elements per outer position, `reduced` times.
                const int64_t grain = std::max<int64_t>(
                    1, grainForWork(kNsPerFloat * runs->inner * runs->reduced));
                parallelFor(runs->outer, grain, [&](int64_t begin, int64_t end) {
                    for (int64_t o = begin; o < end; ++o) {
                        const T *source = in + o * runs->inner;
                        T *run          = dst + o * runs->reduced * runs->inner;
                        for (int64_t r = 0; r < runs->reduced; ++r) {
                            std::memcpy(run + r * runs->inner, source,
                                        sizeof(T) * static_cast<size_t>(runs->inner));
                        }
                    }
                });
            }
            return;
        }
        forEachBroadcast(shape, strides, [&](uint64_t i, int64_t j) { dst[i] = in[j]; });
    });
    return out;
}

Array *fill(double value, TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    return full(dtype, shape, value, allocator);
}

Array *expandAxis(
    const Array *g, std::span<const int64_t> shape, int64_t axis, bool keepDims,
    mm::IAllocator &allocator) {
    const auto rank = static_cast<int64_t>(shape.size());
    const int64_t a = axis < 0 ? axis + rank : axis;
    if (a < 0 || a >= rank) {
        throw std::invalid_argument("reduction gradient axis out of range");
    }
    Shape reduced(shape.begin(), shape.end());
    reduced[static_cast<size_t>(a)] = 1;
    const Array *kept        = keepDims ? g : reshape(g, reduced, allocator);
    return broadcastTo(kept, shape, allocator);
}

namespace {

/// Serial cost of one element of the fused GELU gradient, as a multiple of one element of a plain
/// float pass (parallel.h, kNsPerFloat). It is an estimate for sizing the chunk, not a measurement
/// per call: this pass reads two arrays, writes one and evaluates a tangent, and the isolated probe
/// puts one element of it at 1.61 ns against 0.16 for a multiply over the same 262144 elements
/// (test/cases/perf/step_budget.py, one thread) -- about ten plain passes. Eight is the smaller end
/// of that order, so a chunk has to be worth the split and the chunks stay large enough that their
/// tails are not worth counting.
constexpr double kNsPerGeluGradElement = 8.0 * kNsPerFloat;

/// The unit that computes the gradient on this CPU, chosen once (the check the other per-ISA
/// kernels use). An element is independent of its neighbours and nothing here is a sum, so the unit
/// decides how many elements one instruction handles and nothing else; both units round the
/// polynomial and the slope as written (gradient_loops.inl), and the tangent is `vmath::tanh`,
/// which is where, and the only place where, the two can part.
detail::GeluGradFn geluGradUnit() {
    static const detail::GeluGradFn unit = [] {
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
        if (cpuFeatures().avx2Fma) {
            return &detail::geluGradAvx2;
        }
#endif
        return &detail::geluGradGeneric;
    }();
    return unit;
}

} // namespace

Array *geluGrad(const Array *x, const Array *dy, mm::IAllocator &allocator) {
    const Array *xs = asFloat(x, allocator);
    const Array *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("gelu gradient: dy shape differs from the input");
    }
    const int64_t n = static_cast<int64_t>(xs->numel());
    Array *out      = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
    const float *px = xs->dataAs<float>();
    const float *pg = gs->dataAs<float>();
    float *po       = out->dataAs<float>();
    // One pass: the input and the incoming gradient are read once each, the result is written
    // once, and nothing holds the polynomial or its tangent (gradient_loops.inl).
    const detail::GeluGradFn run = geluGradUnit();
    parallelFor(n, grainForWork(kNsPerGeluGradElement), [&](int64_t begin, int64_t end) {
        run(px + begin, pg + begin, po + begin, end - begin);
    });
    return out;
}

Array *erfGrad(const Array *x, const Array *dy, mm::IAllocator &allocator) {
    const Array *xs = asFloat(x, allocator);
    const Array *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("erf gradient: dy shape differs from the input");
    }
    Array *out      = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
    const float *px        = xs->dataAs<float>();
    const float *pg        = gs->dataAs<float>();
    float *po              = out->dataAs<float>();
    constexpr float kScale = 1.1283791670955126f; // 2 / sqrt(pi)
    for (uint64_t i = 0; i < xs->numel(); ++i) {
        po[i] = pg[i] * kScale * std::exp(-px[i] * px[i]);
    }
    return out;
}

Array *layerNormGrad(
    const Array *x, const Array *gamma, const Array *dy, double eps,
    LayerNormGrad which, mm::IAllocator &allocator) {
    const Array *xs = asFloat(x, allocator);
    const Array *ws = asFloat(gamma, allocator);
    const Array *gs = asFloat(dy, allocator);
    if (!xs->sameShape(gs)) {
        throw ShapeError("layer_norm gradient: dy shape differs from the input");
    }
    const auto width = static_cast<int64_t>(ws->numel());
    const auto rows  = static_cast<int64_t>(xs->numel()) / std::max<int64_t>(width, 1);
    const float *px  = xs->dataAs<float>();
    const float *pw  = ws->dataAs<float>();
    const float *pg  = gs->dataAs<float>();

    if (which == LayerNormGrad::Input) {
        Array *out = createTensor(TypeCode::Float32, xs->shapeSpan(), allocator);
        float *po         = out->dataAs<float>();
        parallelFor(rows, 16, [&](int64_t begin, int64_t end) {
            for (int64_t r = begin; r < end; ++r) {
                const float *row = px + r * width;
                const float *g   = pg + r * width;
                double mean = 0.0, var = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    mean += row[k];
                }
                mean /= static_cast<double>(width);
                for (int64_t k = 0; k < width; ++k) {
                    const double d = row[k] - mean;
                    var += d * d;
                }
                var /= static_cast<double>(width);
                const double inv = 1.0 / std::sqrt(var + eps);
                // dx = inv * (h - mean(h) - xhat * mean(h * xhat)), h = dy * gamma.
                double sumH = 0.0, sumHX = 0.0;
                for (int64_t k = 0; k < width; ++k) {
                    const double h = static_cast<double>(g[k]) * pw[k];
                    sumH += h;
                    sumHX += h * (row[k] - mean) * inv;
                }
                const double meanH  = sumH / static_cast<double>(width);
                const double meanHX = sumHX / static_cast<double>(width);
                float *o            = po + r * width;
                for (int64_t k = 0; k < width; ++k) {
                    const double h    = static_cast<double>(g[k]) * pw[k];
                    const double xhat = (row[k] - mean) * inv;
                    o[k]              = static_cast<float>(inv * (h - meanH - xhat * meanHX));
                }
            }
        });
        return out;
    }

    Array *out = createTensor(TypeCode::Float32, ws->shapeSpan(), allocator, true);
    float *po         = out->dataAs<float>();
    std::vector<double> acc(static_cast<size_t>(width), 0.0);
    for (int64_t r = 0; r < rows; ++r) {
        const float *row = px + r * width;
        const float *g   = pg + r * width;
        if (which == LayerNormGrad::Beta) {
            for (int64_t k = 0; k < width; ++k) {
                acc[static_cast<size_t>(k)] += g[k];
            }
            continue;
        }
        double mean = 0.0, var = 0.0;
        for (int64_t k = 0; k < width; ++k) {
            mean += row[k];
        }
        mean /= static_cast<double>(width);
        for (int64_t k = 0; k < width; ++k) {
            const double d = row[k] - mean;
            var += d * d;
        }
        var /= static_cast<double>(width);
        const double inv = 1.0 / std::sqrt(var + eps);
        for (int64_t k = 0; k < width; ++k) {
            acc[static_cast<size_t>(k)] += static_cast<double>(g[k]) * (row[k] - mean) * inv;
        }
    }
    for (int64_t k = 0; k < width; ++k) {
        po[k] = static_cast<float>(acc[static_cast<size_t>(k)]);
    }
    return out;
}

Array *sliceGrad(
    const Array *dy, std::span<const int64_t> shape, int64_t axisArg, int64_t start,
    int64_t end, int64_t step, mm::IAllocator &allocator) {
    if (step <= 0) {
        throw std::invalid_argument("slice step must be positive");
    }
    const auto rank    = static_cast<int64_t>(shape.size());
    const int64_t axis = axisArg < 0 ? axisArg + rank : axisArg;
    if (axis < 0 || axis >= rank) {
        throw std::invalid_argument("slice axis out of range");
    }
    const int64_t extent = shape[static_cast<size_t>(axis)];
    // The forward slice's bounds, normalized the same way (the forward slice faulted when they
    // were out of range, so they are in range here unless the gradient was built by hand).
    const auto range = normalizeSliceBounds(extent, start, end);
    if (!range) {
        throw std::out_of_range("slice bounds out of range");
    }
    start = range->start;
    end   = range->end;
    const int64_t count     = end > start ? (end - start + step - 1) / step : 0;
    const Array *src = asFloat(dy, allocator);
    if (src->rank() != shape.size() || src->dim(static_cast<size_t>(axis)) != count) {
        throw ShapeError("slice gradient: dy shape does not match the slice");
    }
    Array *out = createTensor(TypeCode::Float32, shape, allocator, true);
    uint64_t outer = 1, inner = 1;
    for (int64_t d = 0; d < axis; ++d) {
        outer *= static_cast<uint64_t>(shape[static_cast<size_t>(d)]);
    }
    for (int64_t d = axis + 1; d < rank; ++d) {
        inner *= static_cast<uint64_t>(shape[static_cast<size_t>(d)]);
    }
    const float *in = src->dataAs<float>();
    float *dst      = out->dataAs<float>();
    for (uint64_t o = 0; o < outer; ++o) {
        for (int64_t k = 0; k < count; ++k) {
            const uint64_t to =
                (o * static_cast<uint64_t>(extent) + static_cast<uint64_t>(start + k * step)) *
                inner;
            const uint64_t from =
                (o * static_cast<uint64_t>(count) + static_cast<uint64_t>(k)) * inner;
            std::copy(in + from, in + from + inner, dst + to);
        }
    }
    return out;
}

namespace {

/// Operands of matmul viewed as matrices: vectors get the unit axis matmul gives them, and dy
/// gets it back.
struct MatmulView {
    const Array *lhs;
    const Array *rhs;
    const Array *dy;
};

MatmulView matrixView(
    const Array *dy, const Array *lhs, const Array *rhs,
    mm::IAllocator &allocator) {
    MatmulView view{asFloat(lhs, allocator), asFloat(rhs, allocator), asFloat(dy, allocator)};
    Shape dyShape = view.dy->shapeVector();
    if (lhs->rank() == 1) {
        view.lhs = reshape(view.lhs, Shape{1, lhs->dim(0)}, allocator);
        dyShape.insert(dyShape.end() - (rhs->rank() == 1 ? 0 : 1), 1);
    }
    if (rhs->rank() == 1) {
        view.rhs = reshape(view.rhs, Shape{rhs->dim(0), 1}, allocator);
        dyShape.push_back(1);
    }
    view.dy = reshape(view.dy, dyShape, allocator);
    return view;
}

} // namespace

namespace {

enum class MatmulOperand { Lhs, Rhs };

/// The gradient of a float matmul operand as GEMMs over the operands' storage (sgemm reads the
/// transposes in place), or nullptr for shapes this does not cover: vectors and broadcast batches.
///   rhs a matrix:        lhs viewed as [M, K] with M the product of its leading axes; one GEMM.
///   equal batch shapes:  one GEMM per batch matrix.
Array *matmulGradGemm(
    MatmulOperand which, const Array *dy, const Array *lhs, const Array *rhs, bool useLibrary,
    mm::IAllocator &allocator) {
    if (lhs->dtype() != TypeCode::Float32 || rhs->dtype() != TypeCode::Float32 ||
        dy->dtype() != TypeCode::Float32 || lhs->rank() < 2 || rhs->rank() < 2) {
        return nullptr;
    }
    const size_t lr = lhs->rank(), rr = rhs->rank();
    const int64_t K = lhs->dim(lr - 1), N = rhs->dim(rr - 1);
    int64_t batches = 1, M = lhs->dim(lr - 2);
    if (rr == 2) {
        M = static_cast<int64_t>(lhs->numel()) / std::max<int64_t>(K, 1);
    } else {
        if (lr != rr || !std::equal(
                            lhs->shapeSpan().begin(),
                            lhs->shapeSpan().end() - 2,
                            rhs->shapeSpan().begin())) {
            return nullptr;
        }
        batches = static_cast<int64_t>(numelOf(lhs->shapeSpan().first(lr - 2)));
    }
    if (static_cast<int64_t>(dy->numel()) != batches * M * N) {
        throw ShapeError("matmul gradient: dy shape does not match the product");
    }
    const float *a = lhs->dataAs<float>(), *b = rhs->dataAs<float>(), *g = dy->dataAs<float>();
    const bool forLhs = which == MatmulOperand::Lhs;
    Array *out =
        createTensor(TypeCode::Float32, (forLhs ? lhs : rhs)->shapeSpan(), allocator);
    float *o            = out->dataAs<float>();
    const int64_t aSize = M * K, bSize = K * N, gSize = M * N;
    using GemmFn = void (*)(
        bool, bool, int64_t, int64_t, int64_t, float, const float *, int64_t, const float *,
        int64_t, float, float *, int64_t);
    const GemmFn gemm = useLibrary ? &sgemmLibrary : &sgemm;
    parallelFor(batches, 1, [&](int64_t begin, int64_t end) {
        for (int64_t i = begin; i < end; ++i) {
            if (forLhs) { // dA = dY @ B^T : [M, K]
                gemm(
                    false,
                    true,
                    M,
                    K,
                    N,
                    1.0f,
                    g + i * gSize,
                    N,
                    b + i * bSize,
                    N,
                    0.0f,
                    o + i * aSize,
                    K);
            } else { // dB = A^T @ dY : [K, N]
                gemm(
                    true,
                    false,
                    K,
                    N,
                    M,
                    1.0f,
                    a + i * aSize,
                    K,
                    g + i * gSize,
                    N,
                    0.0f,
                    o + i * bSize,
                    N);
            }
        }
    });
    return out;
}

} // namespace

Array *matmulGradLhs(
    const Array *dy, const Array *lhs, const Array *rhs, bool useLibrary,
    mm::IAllocator &allocator) {
    if (Array *g = matmulGradGemm(MatmulOperand::Lhs, dy, lhs, rhs, useLibrary, allocator)) {
        return g;
    }
    const MatmulView v = matrixView(dy, lhs, rhs, allocator);
    Array *g    = matmul(v.dy, transposeLast2(v.rhs, allocator), allocator);
    g                  = sumTo(g, v.lhs->shapeSpan(), allocator);
    return reshape(g, lhs->shapeSpan(), allocator);
}

Array *matmulGradRhs(
    const Array *dy, const Array *lhs, const Array *rhs, bool useLibrary,
    mm::IAllocator &allocator) {
    if (Array *g = matmulGradGemm(MatmulOperand::Rhs, dy, lhs, rhs, useLibrary, allocator)) {
        return g;
    }
    const MatmulView v = matrixView(dy, lhs, rhs, allocator);
    Array *g    = matmul(transposeLast2(v.lhs, allocator), v.dy, allocator);
    g                  = sumTo(g, v.rhs->shapeSpan(), allocator);
    return reshape(g, rhs->shapeSpan(), allocator);
}

} // namespace camel::tensor::kernels
