// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Convolution, pooling, and batch-norm kernels (see conv.h).
 *
 * Forward convolution and the input gradient parallelize over images; each
 * worker owns its column buffer, and the GEMMs inside run serially because
 * they execute within a parallel region. With a single image the GEMM itself
 * parallelizes instead. The kernel gradient accumulates over images, so it
 * loops over images and lets each GEMM parallelize.
 */

#include "conv.h"

#include "elementwise.h"
#include "gemm.h"
#include "minmax.h"
#include "parallel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace camel::tensor::kernels {

using type::TypeCode;

namespace {

struct ConvGeometry {
    int64_t N, C, H, W;  // input
    int64_t O, KH, KW;   // kernel
    int64_t OH, OW;      // output
    int64_t stride, pad; // symmetric stride and padding
    int64_t ckk() const { return C * KH * KW; }
    int64_t spatial() const { return OH * OW; }
};

std::string dims(std::span<const int64_t> shape) {
    std::string text = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        text += (i ? ", " : "") + std::to_string(shape[i]);
    }
    return text + "]";
}

ConvGeometry geometry(
    std::span<const int64_t> input, std::span<const int64_t> kernel, int64_t stride, int64_t pad) {
    if (input.size() != 4 || kernel.size() != 4) {
        throw ShapeError(
            "conv2d expects NCHW input and [O, C, KH, KW] kernel, got " + dims(input) + " and " +
            dims(kernel));
    }
    if (stride <= 0 || pad < 0) {
        throw std::invalid_argument("conv2d requires stride > 0 and padding >= 0");
    }
    ConvGeometry g{};
    g.N = input[0], g.C = input[1], g.H = input[2], g.W = input[3];
    g.O = kernel[0], g.KH = kernel[2], g.KW = kernel[3];
    g.stride = stride, g.pad = pad;
    if (kernel[1] != g.C) {
        throw ShapeError(
            "conv2d input channels " + std::to_string(g.C) + " do not match kernel channels " +
            std::to_string(kernel[1]));
    }
    if (g.H + 2 * pad < g.KH || g.W + 2 * pad < g.KW) {
        throw ShapeError("conv2d kernel " + dims(kernel) + " does not fit input " + dims(input));
    }
    g.OH = (g.H + 2 * pad - g.KH) / stride + 1;
    g.OW = (g.W + 2 * pad - g.KW) / stride + 1;
    return g;
}

const Array *asFloat(const Array *t, mm::IAllocator &allocator) {
    return t->dtype() == TypeCode::Float32 ? t : cast(t, TypeCode::Float32, allocator);
}

/// Unfolds one image [C, H, W] into cols [C*KH*KW, OH*OW] (zero padding).
void im2col(const ConvGeometry &g, const float *image, float *cols) {
    for (int64_t c = 0; c < g.C; ++c) {
        for (int64_t kh = 0; kh < g.KH; ++kh) {
            for (int64_t kw = 0; kw < g.KW; ++kw) {
                float *row = cols + ((c * g.KH + kh) * g.KW + kw) * g.spatial();
                for (int64_t oh = 0; oh < g.OH; ++oh) {
                    const int64_t ih = oh * g.stride - g.pad + kh;
                    float *dst       = row + oh * g.OW;
                    if (ih < 0 || ih >= g.H) {
                        std::fill_n(dst, g.OW, 0.0f);
                        continue;
                    }
                    const float *src = image + (c * g.H + ih) * g.W;
                    for (int64_t ow = 0; ow < g.OW; ++ow) {
                        const int64_t iw = ow * g.stride - g.pad + kw;
                        dst[ow]          = (iw >= 0 && iw < g.W) ? src[iw] : 0.0f;
                    }
                }
            }
        }
    }
}

/// Adds cols [C*KH*KW, OH*OW] back into one image gradient [C, H, W].
void col2im(const ConvGeometry &g, const float *cols, float *image) {
    for (int64_t c = 0; c < g.C; ++c) {
        for (int64_t kh = 0; kh < g.KH; ++kh) {
            for (int64_t kw = 0; kw < g.KW; ++kw) {
                const float *row = cols + ((c * g.KH + kh) * g.KW + kw) * g.spatial();
                for (int64_t oh = 0; oh < g.OH; ++oh) {
                    const int64_t ih = oh * g.stride - g.pad + kh;
                    if (ih < 0 || ih >= g.H) {
                        continue;
                    }
                    float *dst = image + (c * g.H + ih) * g.W;
                    for (int64_t ow = 0; ow < g.OW; ++ow) {
                        const int64_t iw = ow * g.stride - g.pad + kw;
                        if (iw >= 0 && iw < g.W) {
                            dst[iw] += row[oh * g.OW + ow];
                        }
                    }
                }
            }
        }
    }
}

void requireDy(const ConvGeometry &g, const Array *dy) {
    const int64_t expected[] = {g.N, g.O, g.OH, g.OW};
    if (dy->rank() != 4 || !std::equal(expected, expected + 4, dy->shape())) {
        throw ShapeError(
            "conv2d gradient expects dy of shape " + dims(expected) + ", got " +
            dims(dy->shapeSpan()));
    }
}

} // namespace

Shape conv2dShape(
    std::span<const int64_t> input, std::span<const int64_t> kernel, int64_t stride,
    int64_t padding) {
    const ConvGeometry g = geometry(input, kernel, stride, padding);
    return {g.N, g.O, g.OH, g.OW};
}

Array *conv2d(
    const Array *input, const Array *kernel, const Array *bias, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    if (bias && !(bias->rank() == 1 && bias->dim(0) == g.O)) {
        throw ShapeError(
            "conv2d bias must have shape [" + std::to_string(g.O) + "], got " +
            dims(bias->shapeSpan()));
    }
    const Array *x    = asFloat(input, allocator);
    const Array *w    = asFloat(kernel, allocator);
    const Array *b    = bias ? asFloat(bias, allocator) : nullptr;
    const int64_t outShape[] = {g.N, g.O, g.OH, g.OW};
    Array *out        = createTensor(TypeCode::Float32, outShape, allocator);
    const float *px = x->dataAs<float>(), *pw = w->dataAs<float>();
    float *py = out->dataAs<float>();
    parallelFor(g.N, 1, [&](int64_t begin, int64_t end) {
        std::vector<float> cols(static_cast<size_t>(g.ckk() * g.spatial()));
        for (int64_t n = begin; n < end; ++n) {
            im2col(g, px + n * g.C * g.H * g.W, cols.data());
            float *y   = py + n * g.O * g.spatial();
            float beta = 0.0f;
            if (b) {
                for (int64_t o = 0; o < g.O; ++o) {
                    std::fill_n(y + o * g.spatial(), g.spatial(), b->dataAs<float>()[o]);
                }
                beta = 1.0f;
            }
            sgemm(
                false,
                false,
                g.O,
                g.spatial(),
                g.ckk(),
                1.0f,
                pw,
                g.ckk(),
                cols.data(),
                g.spatial(),
                beta,
                y,
                g.spatial());
        }
    });
    return out;
}

Array *conv2dInputGrad(
    const Array *input, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const Array *w = asFloat(kernel, allocator);
    const Array *d = asFloat(dy, allocator);
    Array *dx = createTensor(TypeCode::Float32, input->shapeSpan(), allocator, true);
    parallelFor(g.N, 1, [&](int64_t begin, int64_t end) {
        std::vector<float> cols(static_cast<size_t>(g.ckk() * g.spatial()));
        for (int64_t n = begin; n < end; ++n) {
            // cols = W^T [CKK x O] @ dy[n] [O x OHOW]
            sgemm(
                true,
                false,
                g.ckk(),
                g.spatial(),
                g.O,
                1.0f,
                w->dataAs<float>(),
                g.ckk(),
                d->dataAs<float>() + n * g.O * g.spatial(),
                g.spatial(),
                0.0f,
                cols.data(),
                g.spatial());
            col2im(g, cols.data(), dx->dataAs<float>() + n * g.C * g.H * g.W);
        }
    });
    return dx;
}

Array *conv2dKernelGrad(
    const Array *input, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const Array *x = asFloat(input, allocator);
    const Array *d = asFloat(dy, allocator);
    Array *dw =
        createTensor(TypeCode::Float32, kernel->shapeSpan(), allocator, true);
    std::vector<float> cols(static_cast<size_t>(g.ckk() * g.spatial()));
    for (int64_t n = 0; n < g.N; ++n) {
        im2col(g, x->dataAs<float>() + n * g.C * g.H * g.W, cols.data());
        // dW [O x CKK] += dy[n] [O x OHOW] @ cols^T [OHOW x CKK]
        sgemm(
            false,
            true,
            g.O,
            g.ckk(),
            g.spatial(),
            1.0f,
            d->dataAs<float>() + n * g.O * g.spatial(),
            g.spatial(),
            cols.data(),
            g.spatial(),
            1.0f,
            dw->dataAs<float>(),
            g.ckk());
    }
    return dw;
}

Array *conv2dBiasGrad(const Array *dy, mm::IAllocator &allocator) {
    if (dy->rank() != 4) {
        throw ShapeError("conv2d bias gradient expects a rank-4 dy, got " + dims(dy->shapeSpan()));
    }
    const Array *d = asFloat(dy, allocator);
    const int64_t N = dy->dim(0), O = dy->dim(1), S = dy->dim(2) * dy->dim(3);
    const int64_t shape[] = {O};
    Array *db      = createTensor(TypeCode::Float32, shape, allocator);
    for (int64_t o = 0; o < O; ++o) {
        double acc = 0.0;
        for (int64_t n = 0; n < N; ++n) {
            const float *plane = d->dataAs<float>() + (n * O + o) * S;
            for (int64_t s = 0; s < S; ++s) {
                acc += plane[s];
            }
        }
        db->dataAs<float>()[o] = static_cast<float>(acc);
    }
    return db;
}

Shape pool2dShape(std::span<const int64_t> input, const Window2d &window) {
    if (input.size() != 4) {
        throw ShapeError("pool2d expects NCHW input, got " + dims(input));
    }
    if (window.kernelH <= 0 || window.kernelW <= 0 || window.strideH <= 0 || window.strideW <= 0 ||
        window.padH < 0 || window.padW < 0) {
        throw std::invalid_argument(
            "pool2d requires positive kernel/stride and non-negative padding");
    }
    if (input[2] + 2 * window.padH < window.kernelH ||
        input[3] + 2 * window.padW < window.kernelW) {
        throw ShapeError("pool2d window does not fit input " + dims(input));
    }
    return {input[0], input[1], window.outH(input[2]), window.outW(input[3])};
}

Array *pool2d(
    PoolKind kind, const Array *input, const Window2d &window, mm::IAllocator &allocator) {
    const Shape outShape  = pool2dShape(input->shapeSpan(), window);
    const Array *x = asFloat(input, allocator);
    Array *out     = createTensor(TypeCode::Float32, outShape, allocator);
    const int64_t H = input->dim(2), W = input->dim(3), OH = outShape[2], OW = outShape[3];
    const int64_t planes = outShape[0] * outShape[1];
    parallelFor(planes, 4, [&](int64_t begin, int64_t end) {
        for (int64_t p = begin; p < end; ++p) {
            const float *src = x->dataAs<float>() + p * H * W;
            float *dst       = out->dataAs<float>() + p * OH * OW;
            for (int64_t oh = 0; oh < OH; ++oh) {
                for (int64_t ow = 0; ow < OW; ++ow) {
                    // Max pooling is a maximum reduction over the window's inside elements
                    // (IEEE maximum from its identity -inf: a NaN in the window gives NaN).
                    float best = maximumIdentity<float>();
                    double sum = 0.0;
                    for (int64_t kh = 0; kh < window.kernelH; ++kh) {
                        const int64_t ih = oh * window.strideH - window.padH + kh;
                        for (int64_t kw = 0; kw < window.kernelW; ++kw) {
                            const int64_t iw  = ow * window.strideW - window.padW + kw;
                            const bool inside = ih >= 0 && ih < H && iw >= 0 && iw < W;
                            const float v     = inside ? src[ih * W + iw] : 0.0f;
                            if (inside) {
                                best = maximum(best, v);
                            }
                            sum += v; // zero padding counts toward the average (count_include_pad)
                        }
                    }
                    dst[oh * OW + ow] =
                        kind == PoolKind::Max
                            ? best
                            : static_cast<float>(
                                  sum / static_cast<double>(window.kernelH * window.kernelW));
                }
            }
        }
    });
    return out;
}

Array *pool2dGrad(
    PoolKind kind, const Array *input, const Array *dy, const Window2d &window,
    mm::IAllocator &allocator) {
    const Shape outShape = pool2dShape(input->shapeSpan(), window);
    if (dy->rank() != 4 || !std::equal(outShape.begin(), outShape.end(), dy->shape())) {
        throw ShapeError(
            "pool2d gradient expects dy of shape " + dims(outShape) + ", got " +
            dims(dy->shapeSpan()));
    }
    const Array *x = asFloat(input, allocator);
    const Array *d = asFloat(dy, allocator);
    Array *dx = createTensor(TypeCode::Float32, input->shapeSpan(), allocator, true);
    const int64_t H = input->dim(2), W = input->dim(3), OH = outShape[2], OW = outShape[3];
    const int64_t planes = outShape[0] * outShape[1];
    const float scale    = 1.0f / static_cast<float>(window.kernelH * window.kernelW);
    parallelFor(planes, 4, [&](int64_t begin, int64_t end) {
        for (int64_t p = begin; p < end; ++p) {
            const float *src = x->dataAs<float>() + p * H * W;
            const float *g   = d->dataAs<float>() + p * OH * OW;
            float *dst       = dx->dataAs<float>() + p * H * W;
            for (int64_t oh = 0; oh < OH; ++oh) {
                for (int64_t ow = 0; ow < OW; ++ow) {
                    const float grad  = g[oh * OW + ow];
                    int64_t bestIndex = -1;
                    float best        = -std::numeric_limits<float>::infinity();
                    for (int64_t kh = 0; kh < window.kernelH; ++kh) {
                        const int64_t ih = oh * window.strideH - window.padH + kh;
                        for (int64_t kw = 0; kw < window.kernelW; ++kw) {
                            const int64_t iw = ow * window.strideW - window.padW + kw;
                            if (ih < 0 || ih >= H || iw < 0 || iw >= W) {
                                continue;
                            }
                            if (kind == PoolKind::Average) {
                                dst[ih * W + iw] += grad * scale;
                            } else if (src[ih * W + iw] > best) {
                                best      = src[ih * W + iw];
                                bestIndex = ih * W + iw;
                            }
                        }
                    }
                    if (kind == PoolKind::Max && bestIndex >= 0) {
                        dst[bestIndex] += grad;
                    }
                }
            }
        }
    });
    return dx;
}

Array *batchNorm(
    const Array *input, const Array *mean, const Array *var,
    const Array *gamma, const Array *beta, double eps, mm::IAllocator &allocator) {
    if (input->rank() < 2) {
        throw ShapeError(
            "batch_norm expects input of rank >= 2 with channels on axis 1, got " +
            dims(input->shapeSpan()));
    }
    const int64_t C = input->dim(1);
    for (const Array *p : {mean, var, gamma, beta}) {
        if (p->rank() != 1 || p->dim(0) != C) {
            throw ShapeError(
                "batch_norm parameters must have shape [" + std::to_string(C) + "], got " +
                dims(p->shapeSpan()));
        }
    }
    const Array *x = asFloat(input, allocator);
    const Array *m = asFloat(mean, allocator), *v = asFloat(var, allocator);
    const Array *gm = asFloat(gamma, allocator), *bt = asFloat(beta, allocator);
    Array *out = createTensor(TypeCode::Float32, input->shapeSpan(), allocator);
    int64_t inner     = 1;
    for (size_t d = 2; d < input->rank(); ++d) {
        inner *= input->dim(d);
    }
    const int64_t N = input->dim(0);
    parallelFor(
        N * C,
        std::max<int64_t>(1, 16384 / std::max<int64_t>(inner, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t plane = begin; plane < end; ++plane) {
                const int64_t c   = plane % C;
                const float scale = gm->dataAs<float>()[c] /
                                    std::sqrt(v->dataAs<float>()[c] + static_cast<float>(eps));
                const float shift = bt->dataAs<float>()[c] - m->dataAs<float>()[c] * scale;
                const float *src  = x->dataAs<float>() + plane * inner;
                float *dst        = out->dataAs<float>() + plane * inner;
                for (int64_t i = 0; i < inner; ++i) {
                    dst[i] = src[i] * scale + shift;
                }
            }
        });
    return out;
}

namespace {

/// The shared geometry of the training-mode batch-norm kernels: channel count, inner (spatial)
/// extent per image, and values per channel M. The kernels parallelize their reductions over
/// channels; every channel reduces N * inner values serially.
struct BnTrainGeometry {
    int64_t N, C, inner;
    int64_t perChannel() const { return N * inner; }
};

BnTrainGeometry bnTrainGeometry(
    const Array *input, std::initializer_list<const Array *> params, const char *op) {
    if (input->rank() < 2) {
        throw ShapeError(
            std::string(op) + " expects input of rank >= 2 with channels on axis 1, got " +
            dims(input->shapeSpan()));
    }
    BnTrainGeometry g{.N = input->dim(0), .C = input->dim(1), .inner = 1};
    for (size_t d = 2; d < input->rank(); ++d) {
        g.inner *= input->dim(d);
    }
    for (const Array *p : params) {
        if (p->rank() != 1 || p->dim(0) != g.C) {
            throw ShapeError(
                std::string(op) + " parameters must have shape [" + std::to_string(g.C) +
                "], got " + dims(p->shapeSpan()));
        }
    }
    if (g.perChannel() < 2) {
        throw ShapeError(
            std::string(op) + " expects more than 1 value per channel when training, got " +
            dims(input->shapeSpan()));
    }
    return g;
}

/// Per-channel batch statistics of `x` (double accumulators): the mean, the biased variance,
/// and 1/sqrt(var + eps).
struct BnTrainStats {
    std::vector<double> mean, var, invStd;
};

BnTrainStats bnTrainStats(const float *x, const BnTrainGeometry &g, double eps) {
    BnTrainStats stats;
    const size_t C = static_cast<size_t>(g.C);
    stats.mean.assign(C, 0.0);
    stats.var.assign(C, 0.0);
    stats.invStd.assign(C, 0.0);
    parallelFor(g.C, 1, [&](int64_t begin, int64_t end) {
        for (int64_t c = begin; c < end; ++c) {
            double sum = 0.0;
            for (int64_t n = 0; n < g.N; ++n) {
                const float *plane = x + (n * g.C + c) * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    sum += plane[i];
                }
            }
            const double mu = sum / static_cast<double>(g.perChannel());
            double sq       = 0.0;
            for (int64_t n = 0; n < g.N; ++n) {
                const float *plane = x + (n * g.C + c) * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    const double d = plane[i] - mu;
                    sq += d * d;
                }
            }
            const size_t u  = static_cast<size_t>(c);
            stats.mean[u]   = mu;
            stats.var[u]    = sq / static_cast<double>(g.perChannel());
            stats.invStd[u] = 1.0 / std::sqrt(stats.var[u] + eps);
        }
    });
    return stats;
}

} // namespace

void batchNormTrain(
    const Array *input, const Array *gamma, const Array *beta,
    const Array *runningMean, const Array *runningVar, double momentum, double eps,
    Array **outY, Array **outRunningMean, Array **outRunningVar,
    mm::IAllocator &allocator) {
    const BnTrainGeometry g =
        bnTrainGeometry(input, {gamma, beta, runningMean, runningVar}, "batch_norm_train");
    const Array *x  = asFloat(input, allocator);
    const Array *gm = asFloat(gamma, allocator), *bt = asFloat(beta, allocator);
    const Array *rm = asFloat(runningMean, allocator), *rv = asFloat(runningVar, allocator);
    const Shape statShape{g.C};
    Array *y   = createTensor(TypeCode::Float32, input->shapeSpan(), allocator);
    Array *nrm = createTensor(TypeCode::Float32, statShape, allocator);
    Array *nrv = createTensor(TypeCode::Float32, statShape, allocator);

    const BnTrainStats stats = bnTrainStats(x->dataAs<float>(), g, eps);

    const float keep = static_cast<float>(1.0 - momentum), mix = static_cast<float>(momentum);
    const double unbiased =
        static_cast<double>(g.perChannel()) / static_cast<double>(g.perChannel() - 1);
    float *prm = nrm->dataAs<float>(), *prv = nrv->dataAs<float>();
    for (int64_t c = 0; c < g.C; ++c) {
        const size_t u = static_cast<size_t>(c);
        prm[c]         = keep * rm->dataAs<float>()[c] + mix * static_cast<float>(stats.mean[u]);
        prv[c] = keep * rv->dataAs<float>()[c] + mix * static_cast<float>(stats.var[u] * unbiased);
    }
    parallelFor(
        g.N * g.C,
        std::max<int64_t>(1, 16384 / std::max<int64_t>(g.inner, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t plane = begin; plane < end; ++plane) {
                const int64_t c   = plane % g.C;
                const size_t u    = static_cast<size_t>(c);
                const float scale = gm->dataAs<float>()[c] * static_cast<float>(stats.invStd[u]);
                const float shift =
                    bt->dataAs<float>()[c] - static_cast<float>(stats.mean[u]) * scale;
                const float *src = x->dataAs<float>() + plane * g.inner;
                float *dst       = y->dataAs<float>() + plane * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    dst[i] = src[i] * scale + shift;
                }
            }
        });
    *outY           = y;
    *outRunningMean = nrm;
    *outRunningVar  = nrv;
}

void batchNormTrainGrad(
    const Array *input, const Array *gamma, const Array *dy, double eps,
    Array **outDx, Array **outDgamma, Array **outDbeta,
    mm::IAllocator &allocator) {
    const BnTrainGeometry g = bnTrainGeometry(input, {gamma}, "batch_norm_train_grad");
    if (!dy->sameShape(input)) {
        throw ShapeError(
            "batch_norm_train_grad expects dy shaped like the input, got " + dims(dy->shapeSpan()) +
            " vs " + dims(input->shapeSpan()));
    }
    const Array *x  = asFloat(input, allocator);
    const Array *gm = asFloat(gamma, allocator);
    const Array *d  = asFloat(dy, allocator);
    const Shape statShape{g.C};
    Array *dx = createTensor(TypeCode::Float32, input->shapeSpan(), allocator);
    Array *dg = createTensor(TypeCode::Float32, statShape, allocator);
    Array *db = createTensor(TypeCode::Float32, statShape, allocator);

    const BnTrainStats stats = bnTrainStats(x->dataAs<float>(), g, eps);
    const double M           = static_cast<double>(g.perChannel());
    float *pdg = dg->dataAs<float>(), *pdb = db->dataAs<float>();
    // Per-channel sums of dy and dy*xhat (double accumulators).
    parallelFor(g.C, 1, [&](int64_t begin, int64_t end) {
        for (int64_t c = begin; c < end; ++c) {
            const size_t u  = static_cast<size_t>(c);
            const double mu = stats.mean[u], inv = stats.invStd[u];
            double sumDy = 0.0, sumDyXhat = 0.0;
            for (int64_t n = 0; n < g.N; ++n) {
                const float *xplane = x->dataAs<float>() + (n * g.C + c) * g.inner;
                const float *dplane = d->dataAs<float>() + (n * g.C + c) * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    const double xhat = (xplane[i] - mu) * inv;
                    sumDy += dplane[i];
                    sumDyXhat += dplane[i] * xhat;
                }
            }
            pdb[c] = static_cast<float>(sumDy);
            pdg[c] = static_cast<float>(sumDyXhat);
        }
    });
    // dx = gamma*inv * (dy - sum(dy)/M - xhat*sum(dy*xhat)/M)
    parallelFor(
        g.N * g.C,
        std::max<int64_t>(1, 16384 / std::max<int64_t>(g.inner, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t plane = begin; plane < end; ++plane) {
                const int64_t c = plane % g.C;
                const size_t u  = static_cast<size_t>(c);
                const double mu = stats.mean[u], inv = stats.invStd[u];
                const float ginv    = gm->dataAs<float>()[c] * static_cast<float>(inv);
                const float meanDy  = pdb[c] / static_cast<float>(M);
                const float meanDxh = pdg[c] / static_cast<float>(M);
                const float *xplane = x->dataAs<float>() + plane * g.inner;
                const float *dplane = d->dataAs<float>() + plane * g.inner;
                float *dst          = dx->dataAs<float>() + plane * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    const float xhat =
                        (xplane[i] - static_cast<float>(mu)) * static_cast<float>(inv);
                    dst[i] = ginv * (dplane[i] - meanDy - xhat * meanDxh);
                }
            }
        });
    *outDx     = dx;
    *outDgamma = dg;
    *outDbeta  = db;
}

} // namespace camel::tensor::kernels
