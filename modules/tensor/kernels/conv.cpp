/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

const TensorObject *asFloat(const TensorObject *t, mm::IAllocator &allocator) {
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

void requireDy(const ConvGeometry &g, const TensorObject *dy) {
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

TensorObject *conv2d(
    const TensorObject *input, const TensorObject *kernel, const TensorObject *bias, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    if (bias && !(bias->rank() == 1 && bias->dim(0) == g.O)) {
        throw ShapeError(
            "conv2d bias must have shape [" + std::to_string(g.O) + "], got " +
            dims(bias->shapeSpan()));
    }
    const TensorObject *x    = asFloat(input, allocator);
    const TensorObject *w    = asFloat(kernel, allocator);
    const TensorObject *b    = bias ? asFloat(bias, allocator) : nullptr;
    const int64_t outShape[] = {g.N, g.O, g.OH, g.OW};
    TensorObject *out        = TensorObject::create(TypeCode::Float32, outShape, allocator);
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

TensorObject *conv2dInputGrad(
    const TensorObject *input, const TensorObject *kernel, const TensorObject *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const TensorObject *w = asFloat(kernel, allocator);
    const TensorObject *d = asFloat(dy, allocator);
    TensorObject *dx = TensorObject::create(TypeCode::Float32, input->shapeSpan(), allocator, true);
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

TensorObject *conv2dKernelGrad(
    const TensorObject *input, const TensorObject *kernel, const TensorObject *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const TensorObject *x = asFloat(input, allocator);
    const TensorObject *d = asFloat(dy, allocator);
    TensorObject *dw =
        TensorObject::create(TypeCode::Float32, kernel->shapeSpan(), allocator, true);
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

TensorObject *conv2dBiasGrad(const TensorObject *dy, mm::IAllocator &allocator) {
    if (dy->rank() != 4) {
        throw ShapeError("conv2d bias gradient expects a rank-4 dy, got " + dims(dy->shapeSpan()));
    }
    const TensorObject *d = asFloat(dy, allocator);
    const int64_t N = dy->dim(0), O = dy->dim(1), S = dy->dim(2) * dy->dim(3);
    const int64_t shape[] = {O};
    TensorObject *db      = TensorObject::create(TypeCode::Float32, shape, allocator);
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

TensorObject *pool2d(
    PoolKind kind, const TensorObject *input, const Window2d &window, mm::IAllocator &allocator) {
    const Shape outShape  = pool2dShape(input->shapeSpan(), window);
    const TensorObject *x = asFloat(input, allocator);
    TensorObject *out     = TensorObject::create(TypeCode::Float32, outShape, allocator);
    const int64_t H = input->dim(2), W = input->dim(3), OH = outShape[2], OW = outShape[3];
    const int64_t planes = outShape[0] * outShape[1];
    parallelFor(planes, 4, [&](int64_t begin, int64_t end) {
        for (int64_t p = begin; p < end; ++p) {
            const float *src = x->dataAs<float>() + p * H * W;
            float *dst       = out->dataAs<float>() + p * OH * OW;
            for (int64_t oh = 0; oh < OH; ++oh) {
                for (int64_t ow = 0; ow < OW; ++ow) {
                    float best = -std::numeric_limits<float>::infinity();
                    double sum = 0.0;
                    for (int64_t kh = 0; kh < window.kernelH; ++kh) {
                        const int64_t ih = oh * window.strideH - window.padH + kh;
                        for (int64_t kw = 0; kw < window.kernelW; ++kw) {
                            const int64_t iw  = ow * window.strideW - window.padW + kw;
                            const bool inside = ih >= 0 && ih < H && iw >= 0 && iw < W;
                            const float v     = inside ? src[ih * W + iw] : 0.0f;
                            if (inside) {
                                best = std::max(best, v);
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

TensorObject *pool2dGrad(
    PoolKind kind, const TensorObject *input, const TensorObject *dy, const Window2d &window,
    mm::IAllocator &allocator) {
    const Shape outShape = pool2dShape(input->shapeSpan(), window);
    if (dy->rank() != 4 || !std::equal(outShape.begin(), outShape.end(), dy->shape())) {
        throw ShapeError(
            "pool2d gradient expects dy of shape " + dims(outShape) + ", got " +
            dims(dy->shapeSpan()));
    }
    const TensorObject *x = asFloat(input, allocator);
    const TensorObject *d = asFloat(dy, allocator);
    TensorObject *dx = TensorObject::create(TypeCode::Float32, input->shapeSpan(), allocator, true);
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

TensorObject *batchNorm(
    const TensorObject *input, const TensorObject *mean, const TensorObject *var,
    const TensorObject *gamma, const TensorObject *beta, double eps, mm::IAllocator &allocator) {
    if (input->rank() < 2) {
        throw ShapeError(
            "batch_norm expects input of rank >= 2 with channels on axis 1, got " +
            dims(input->shapeSpan()));
    }
    const int64_t C = input->dim(1);
    for (const TensorObject *p : {mean, var, gamma, beta}) {
        if (p->rank() != 1 || p->dim(0) != C) {
            throw ShapeError(
                "batch_norm parameters must have shape [" + std::to_string(C) + "], got " +
                dims(p->shapeSpan()));
        }
    }
    const TensorObject *x = asFloat(input, allocator);
    const TensorObject *m = asFloat(mean, allocator), *v = asFloat(var, allocator);
    const TensorObject *gm = asFloat(gamma, allocator), *bt = asFloat(beta, allocator);
    TensorObject *out = TensorObject::create(TypeCode::Float32, input->shapeSpan(), allocator);
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

} // namespace camel::tensor::kernels
