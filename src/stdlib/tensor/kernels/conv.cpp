// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Convolution, pooling, and batch-norm kernels (see conv.h).
 *
 * Each kernel is one source instantiated for the element type its call states: f32 and f64.
 * `computableElementType` reads that one type off the call's tensors (a call whose tensors state
 * two element types has no arm to take -- there is no implicit conversion between numeric types,
 * design principles 5.1), refuses an element type no instance exists for, and `forElementType`
 * picks the instance. So an f64 convolution is not a second implementation; it is this one.
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
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
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

/// The element type a call computes in: the one every tensor of the call states.
///
/// A call whose tensors state two element types has no arm to take: a tensor operator computes in
/// one element type and there is no implicit conversion between numeric types (design principles
/// 5.1), so it is refused here rather than read at a type the program did not write. A null tensor
/// (an absent bias) states nothing.
TypeCode oneElementType(const char *op, std::initializer_list<const Array *> tensors) {
    std::optional<TypeCode> code;
    for (const Array *tensor : tensors) {
        if (tensor == nullptr) {
            continue;
        }
        if (!code.has_value()) {
            code = tensor->dtype();
        } else if (*code != tensor->dtype()) {
            throw std::invalid_argument(
                std::string(op) + ": the two operands have different element types ('" +
                elementTypeSpelling(*code) + "' and '" + elementTypeSpelling(tensor->dtype()) +
                "'): a tensor operator computes in one element type, and there is no implicit "
                "conversion between numeric types");
        }
    }
    return code.value_or(TypeCode::Float32);
}

/// The element type these kernels compute in, refusing every other one: the kernels are
/// instantiated for f32 and f64 (`forElementType`), and an integer, bool, bf16 or f16 tensor is
/// refused with the operator, the argument type and the supported set, never narrowed to f32
/// (design principles 5.1; 13.1). This is each kernel's declaration of what it computes; it is a
/// fault at the call site, not a conversion.
TypeCode computableElementType(const char *op, std::initializer_list<const Array *> tensors) {
    const TypeCode code = oneElementType(op, tensors);
    if (code == TypeCode::Float32 || code == TypeCode::Float64) {
        return code;
    }
    for (const Array *tensor : tensors) {
        if (tensor != nullptr) {
            refuseTensorElementType(op, tensor, {TypeCode::Float32, TypeCode::Float64});
        }
    }
    throw std::invalid_argument(noTensorElement(code, ElementSubject::Kernels));
}

/// Runs `fn` instantiated for the element type the call computes in. The two instances are the
/// storage types of the two element types these kernels state (dtype.h, `StorageOf`), so a kernel
/// body is written once and compiled twice. `dispatchDType` would instantiate all seven storage
/// types, which is the wrong question here: these kernels multiply, so only floating types have
/// an implementation at all.
template <typename Fn> decltype(auto) forElementType(TypeCode code, Fn &&fn) {
    switch (code) {
    case TypeCode::Float32:
        return fn.template operator()<float>();
    case TypeCode::Float64:
        return fn.template operator()<double>();
    default:
        break;
    }
    // computableElementType refused every other element type, so no call reaches this.
    throw std::invalid_argument(noTensorElement(code, ElementSubject::Kernels));
}

/// The product the convolution is built on, in the element type of its tensors: `sgemm` (the
/// canonical f32 kernel, gemm.h) for f32, and its f64 counterpart `dgemm` (the same signature and
/// summation order) for f64.
template <typename T>
void spatialGemm(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, T alpha, const T *A, int64_t lda,
    const T *B, int64_t ldb, T beta, T *C, int64_t ldc) {
    if constexpr (std::is_same_v<T, float>) {
        sgemm(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
    } else {
        static_assert(std::is_same_v<T, double>, "a spatial kernel multiplies in f32 or f64");
        dgemm(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
    }
}

/// Unfolds one image [C, H, W] into cols [C*KH*KW, OH*OW] (zero padding).
template <typename T> void im2col(const ConvGeometry &g, const T *image, T *cols) {
    for (int64_t c = 0; c < g.C; ++c) {
        for (int64_t kh = 0; kh < g.KH; ++kh) {
            for (int64_t kw = 0; kw < g.KW; ++kw) {
                T *row = cols + ((c * g.KH + kh) * g.KW + kw) * g.spatial();
                for (int64_t oh = 0; oh < g.OH; ++oh) {
                    const int64_t ih = oh * g.stride - g.pad + kh;
                    T *dst          = row + oh * g.OW;
                    if (ih < 0 || ih >= g.H) {
                        std::fill_n(dst, g.OW, T(0));
                        continue;
                    }
                    const T *src = image + (c * g.H + ih) * g.W;
                    for (int64_t ow = 0; ow < g.OW; ++ow) {
                        const int64_t iw = ow * g.stride - g.pad + kw;
                        dst[ow]          = (iw >= 0 && iw < g.W) ? src[iw] : T(0);
                    }
                }
            }
        }
    }
}

/// Adds cols [C*KH*KW, OH*OW] back into one image gradient [C, H, W].
template <typename T> void col2im(const ConvGeometry &g, const T *cols, T *image) {
    for (int64_t c = 0; c < g.C; ++c) {
        for (int64_t kh = 0; kh < g.KH; ++kh) {
            for (int64_t kw = 0; kw < g.KW; ++kw) {
                const T *row = cols + ((c * g.KH + kh) * g.KW + kw) * g.spatial();
                for (int64_t oh = 0; oh < g.OH; ++oh) {
                    const int64_t ih = oh * g.stride - g.pad + kh;
                    if (ih < 0 || ih >= g.H) {
                        continue;
                    }
                    T *dst = image + (c * g.H + ih) * g.W;
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

// Each kernel below is one instantiation per element type; the public entry point beside it reads
// the element type off the call and picks the instance.

template <typename T>
Array *conv2dIn(
    const ConvGeometry &g, const Array *input, const Array *kernel, const Array *bias,
    mm::IAllocator &allocator) {
    const int64_t outShape[] = {g.N, g.O, g.OH, g.OW};
    Array *out               = createTensor(dtypeOf<T>(), outShape, allocator);
    const T *px = input->dataAs<T>(), *pw = kernel->dataAs<T>();
    const T *pb = bias != nullptr ? bias->dataAs<T>() : nullptr;
    T *py       = out->dataAs<T>();
    parallelFor(g.N, 1, [&](int64_t begin, int64_t end) {
        std::vector<T> cols(static_cast<size_t>(g.ckk() * g.spatial()));
        for (int64_t n = begin; n < end; ++n) {
            im2col(g, px + n * g.C * g.H * g.W, cols.data());
            T *y   = py + n * g.O * g.spatial();
            T beta = T(0);
            if (pb != nullptr) {
                for (int64_t o = 0; o < g.O; ++o) {
                    std::fill_n(y + o * g.spatial(), g.spatial(), pb[o]);
                }
                beta = T(1);
            }
            spatialGemm(
                false,
                false,
                g.O,
                g.spatial(),
                g.ckk(),
                T(1),
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

// The two gradient kernels read **one** thing of the operand they answer: the extents of the tensor
// they hand back. `conv2dInputGradIn` never touches `input`'s values (`col2im` writes the whole
// output and reads only `g`), and `conv2dKernelGradIn` never touches `kernel`'s values (`pdw`
// accumulates over every element and starts at zero). So the operand arrives here as extents,
// which is what lets the statement carry them (see `conv2dInputGradStated` below).
template <typename T>
Array *conv2dInputGradIn(
    const ConvGeometry &g, std::span<const int64_t> inputExtents, const Array *kernel,
    const Array *dy, mm::IAllocator &allocator) {
    const T *pw = kernel->dataAs<T>(), *pdy = dy->dataAs<T>();
    Array *dx = createTensor(dtypeOf<T>(), inputExtents, allocator, true);
    T *pdx    = dx->dataAs<T>();
    parallelFor(g.N, 1, [&](int64_t begin, int64_t end) {
        std::vector<T> cols(static_cast<size_t>(g.ckk() * g.spatial()));
        for (int64_t n = begin; n < end; ++n) {
            // cols = W^T [CKK x O] @ dy[n] [O x OHOW]
            spatialGemm(
                true,
                false,
                g.ckk(),
                g.spatial(),
                g.O,
                T(1),
                pw,
                g.ckk(),
                pdy + n * g.O * g.spatial(),
                g.spatial(),
                T(0),
                cols.data(),
                g.spatial());
            col2im(g, cols.data(), pdx + n * g.C * g.H * g.W);
        }
    });
    return dx;
}

template <typename T>
Array *conv2dKernelGradIn(
    const ConvGeometry &g, const Array *input, std::span<const int64_t> kernelExtents,
    const Array *dy, mm::IAllocator &allocator) {
    const T *px = input->dataAs<T>(), *pdy = dy->dataAs<T>();
    Array *dw = createTensor(dtypeOf<T>(), kernelExtents, allocator, true);
    T *pdw    = dw->dataAs<T>();
    std::vector<T> cols(static_cast<size_t>(g.ckk() * g.spatial()));
    for (int64_t n = 0; n < g.N; ++n) {
        im2col(g, px + n * g.C * g.H * g.W, cols.data());
        // dW [O x CKK] += dy[n] [O x OHOW] @ cols^T [OHOW x CKK]
        spatialGemm(
            false,
            true,
            g.O,
            g.ckk(),
            g.spatial(),
            T(1),
            pdy + n * g.O * g.spatial(),
            g.spatial(),
            cols.data(),
            g.spatial(),
            T(1),
            pdw,
            g.ckk());
    }
    return dw;
}

template <typename T> Array *conv2dBiasGradIn(const Array *dy, mm::IAllocator &allocator) {
    const int64_t N = dy->dim(0), O = dy->dim(1), S = dy->dim(2) * dy->dim(3);
    const int64_t shape[] = {O};
    Array *db             = createTensor(dtypeOf<T>(), shape, allocator);
    T *pdb                = db->dataAs<T>();
    const T *pdy          = dy->dataAs<T>();
    for (int64_t o = 0; o < O; ++o) {
        // The sum over the batch and the spatial axes accumulates in double for both element
        // types and is rounded once when it is stored, the rule the reductions follow
        // (dtype.h: accumulate in f64, round once, answer in the element type).
        double acc = 0.0;
        for (int64_t n = 0; n < N; ++n) {
            const T *plane = pdy + (n * O + o) * S;
            for (int64_t s = 0; s < S; ++s) {
                acc += static_cast<double>(plane[s]);
            }
        }
        pdb[o] = static_cast<T>(acc);
    }
    return db;
}

template <typename T>
Array *pool2dIn(
    PoolKind kind, const Array *input, const Window2d &window, const Shape &outShape,
    mm::IAllocator &allocator) {
    Array *out    = createTensor(dtypeOf<T>(), outShape, allocator);
    const T *psrc = input->dataAs<T>();
    const int64_t H = input->dim(2), W = input->dim(3), OH = outShape[2], OW = outShape[3];
    const int64_t planes = outShape[0] * outShape[1];
    T *pdst              = out->dataAs<T>();
    parallelFor(planes, 4, [&](int64_t begin, int64_t end) {
        for (int64_t p = begin; p < end; ++p) {
            const T *src = psrc + p * H * W;
            T *dst       = pdst + p * OH * OW;
            for (int64_t oh = 0; oh < OH; ++oh) {
                for (int64_t ow = 0; ow < OW; ++ow) {
                    // Max pooling is a maximum reduction over the window's inside elements
                    // (IEEE maximum from its identity -inf: a NaN in the window gives NaN).
                    T best     = maximumIdentity<T>();
                    double sum = 0.0;
                    for (int64_t kh = 0; kh < window.kernelH; ++kh) {
                        const int64_t ih = oh * window.strideH - window.padH + kh;
                        for (int64_t kw = 0; kw < window.kernelW; ++kw) {
                            const int64_t iw  = ow * window.strideW - window.padW + kw;
                            const bool inside = ih >= 0 && ih < H && iw >= 0 && iw < W;
                            const T v         = inside ? src[ih * W + iw] : T(0);
                            if (inside) {
                                best = maximum(best, v);
                            }
                            sum += static_cast<double>(
                                v); // zero padding counts toward the average (count_include_pad)
                        }
                    }
                    dst[oh * OW + ow] =
                        kind == PoolKind::Max
                            ? best
                            : static_cast<T>(
                                  sum / static_cast<double>(window.kernelH * window.kernelW));
                }
            }
        }
    });
    return out;
}

template <typename T>
Array *pool2dGradIn(
    PoolKind kind, const Array *input, const Array *dy, const Window2d &window,
    const Shape &outShape, mm::IAllocator &allocator) {
    Array *dx     = createTensor(dtypeOf<T>(), input->shapeSpan(), allocator, true);
    const T *psrc = input->dataAs<T>();
    const T *pdy  = dy->dataAs<T>();
    T *pdx         = dx->dataAs<T>();
    const int64_t H = input->dim(2), W = input->dim(3), OH = outShape[2], OW = outShape[3];
    const int64_t planes = outShape[0] * outShape[1];
    const T scale = T(1) / static_cast<T>(window.kernelH * window.kernelW);
    parallelFor(planes, 4, [&](int64_t begin, int64_t end) {
        for (int64_t p = begin; p < end; ++p) {
            const T *src = psrc + p * H * W;
            const T *g   = pdy + p * OH * OW;
            T *dst       = pdx + p * H * W;
            for (int64_t oh = 0; oh < OH; ++oh) {
                for (int64_t ow = 0; ow < OW; ++ow) {
                    const T grad      = g[oh * OW + ow];
                    int64_t bestIndex = -1;
                    T best            = -std::numeric_limits<T>::infinity();
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

template <typename T>
Array *batchNormIn(
    const Array *input, const Array *mean, const Array *var, const Array *gamma,
    const Array *beta, double eps, mm::IAllocator &allocator) {
    Array *out = createTensor(dtypeOf<T>(), input->shapeSpan(), allocator);
    int64_t inner = 1;
    for (size_t d = 2; d < input->rank(); ++d) {
        inner *= input->dim(d);
    }
    const int64_t C = input->dim(1), N = input->dim(0);
    const T *px     = input->dataAs<T>();
    const T *pgamma = gamma->dataAs<T>(), *pbeta = beta->dataAs<T>();
    const T *pmean = mean->dataAs<T>(), *pvar = var->dataAs<T>();
    T *pdst        = out->dataAs<T>();
    parallelFor(
        N * C,
        std::max<int64_t>(1, 16384 / std::max<int64_t>(inner, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t plane = begin; plane < end; ++plane) {
                const int64_t c   = plane % C;
                const T scale     = pgamma[c] / std::sqrt(pvar[c] + static_cast<T>(eps));
                const T shift     = pbeta[c] - pmean[c] * scale;
                const T *src      = px + plane * inner;
                T *dst            = pdst + plane * inner;
                for (int64_t i = 0; i < inner; ++i) {
                    dst[i] = src[i] * scale + shift;
                }
            }
        });
    return out;
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
    const TypeCode code = computableElementType("conv2d", {input, kernel, bias});
    return forElementType(code, [&]<typename T>() {
        return conv2dIn<T>(g, input, kernel, bias, allocator);
    });
}

Array *conv2dInputGrad(
    const Array *input, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const TypeCode code = computableElementType("conv2d_input_grad", {kernel, dy});
    return forElementType(code, [&]<typename T>() {
        return conv2dInputGradIn<T>(g, input->shapeSpan(), kernel, dy, allocator);
    });
}

Array *conv2dKernelGrad(
    const Array *input, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const TypeCode code = computableElementType("conv2d_kernel_grad", {input, dy});
    return forElementType(code, [&]<typename T>() {
        return conv2dKernelGradIn<T>(g, input, kernel->shapeSpan(), dy, allocator);
    });
}

// The stated twins of the two gradients: the operand they answer is named by its extents instead of
// by a tensor, and every other operand is read exactly as the unstested entry point reads it. This is
// the same arithmetic, not a second reading of it -- the geometry, `requireDy`, the element type and
// the kernels are the ones above -- so a stated call and an unstated call of the same operands answer
// the same bits. The only thing the extents change is where the answer's shape comes from, which is
// the point: a reverse rule of `conv2d` is handed a cotangent for one operand and no value of it
// (`stdlib/rulesets/transpose.cml#conv2d_t`), and `shape` of that operand is a value the transposed
// half holds while the operand's own tensors are not.
Array *conv2dInputGradStated(
    std::span<const int64_t> inputExtents, const Array *kernel, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(inputExtents, kernel->shapeSpan(), stride, padding);
    requireDy(g, dy);
    const TypeCode code = computableElementType("conv2d_input_grad_stated", {kernel, dy});
    return forElementType(code, [&]<typename T>() {
        return conv2dInputGradIn<T>(g, inputExtents, kernel, dy, allocator);
    });
}

Array *conv2dKernelGradStated(
    const Array *input, std::span<const int64_t> kernelExtents, const Array *dy, int64_t stride,
    int64_t padding, mm::IAllocator &allocator) {
    const ConvGeometry g = geometry(input->shapeSpan(), kernelExtents, stride, padding);
    requireDy(g, dy);
    const TypeCode code = computableElementType("conv2d_kernel_grad_stated", {input, dy});
    return forElementType(code, [&]<typename T>() {
        return conv2dKernelGradIn<T>(g, input, kernelExtents, dy, allocator);
    });
}

Array *conv2dBiasGradStated(
    std::span<const int64_t> biasExtents, const Array *dy, mm::IAllocator &allocator) {
    if (biasExtents.size() != 1) {
        throw ShapeError(
            "conv2d bias gradient expects a one-dimension bias, got " + dims(biasExtents));
    }
    // `conv2dBiasGrad` reads `dy` alone and answers `[O]`: the extents are checked against that
    // answer before any work is done, and then it is the unstested reading that answers.
    if (dy->rank() != 4 || dy->dim(1) != biasExtents[0]) {
        throw ShapeError(
            "conv2d bias gradient: the stated bias " + dims(biasExtents) +
            " disagrees with the channels of dy, " + dims(dy->shapeSpan()));
    }
    return conv2dBiasGrad(dy, allocator);
}

Array *conv2dBiasGrad(const Array *dy, mm::IAllocator &allocator) {
    if (dy->rank() != 4) {
        throw ShapeError("conv2d bias gradient expects a rank-4 dy, got " + dims(dy->shapeSpan()));
    }
    const TypeCode code = computableElementType("conv2d_bias_grad", {dy});
    return forElementType(code, [&]<typename T>() { return conv2dBiasGradIn<T>(dy, allocator); });
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
    const Shape outShape = pool2dShape(input->shapeSpan(), window);
    const char *op       = kind == PoolKind::Max ? "max_pool2d" : "avg_pool2d";
    const TypeCode code  = computableElementType(op, {input});
    return forElementType(code, [&]<typename T>() {
        return pool2dIn<T>(kind, input, window, outShape, allocator);
    });
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
    const char *op      = kind == PoolKind::Max ? "max_pool2d_grad" : "avg_pool2d_grad";
    const TypeCode code = computableElementType(op, {input, dy});
    return forElementType(code, [&]<typename T>() {
        return pool2dGradIn<T>(kind, input, dy, window, outShape, allocator);
    });
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
    const TypeCode code = computableElementType("batch_norm", {input, mean, var, gamma, beta});
    return forElementType(code, [&]<typename T>() {
        return batchNormIn<T>(input, mean, var, gamma, beta, eps, allocator);
    });
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

template <typename T> BnTrainStats bnTrainStats(const T *x, const BnTrainGeometry &g, double eps) {
    BnTrainStats stats;
    const size_t C = static_cast<size_t>(g.C);
    stats.mean.assign(C, 0.0);
    stats.var.assign(C, 0.0);
    stats.invStd.assign(C, 0.0);
    parallelFor(g.C, 1, [&](int64_t begin, int64_t end) {
        for (int64_t c = begin; c < end; ++c) {
            double sum = 0.0;
            for (int64_t n = 0; n < g.N; ++n) {
                const T *plane = x + (n * g.C + c) * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    sum += static_cast<double>(plane[i]);
                }
            }
            const double mu = sum / static_cast<double>(g.perChannel());
            double sq       = 0.0;
            for (int64_t n = 0; n < g.N; ++n) {
                const T *plane = x + (n * g.C + c) * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    const double d = static_cast<double>(plane[i]) - mu;
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

template <typename T>
void batchNormTrainIn(
    const BnTrainGeometry &g, const Array *input, const Array *gamma, const Array *beta,
    const Array *runningMean, const Array *runningVar, double momentum, double eps,
    Array **outY, Array **outRunningMean, Array **outRunningVar, mm::IAllocator &allocator) {
    const Shape statShape{g.C};
    Array *y   = createTensor(dtypeOf<T>(), input->shapeSpan(), allocator);
    Array *nrm = createTensor(dtypeOf<T>(), statShape, allocator);
    Array *nrv = createTensor(dtypeOf<T>(), statShape, allocator);

    const BnTrainStats stats = bnTrainStats(input->dataAs<T>(), g, eps);

    const T keep = static_cast<T>(1.0 - momentum), mix = static_cast<T>(momentum);
    const double unbiased =
        static_cast<double>(g.perChannel()) / static_cast<double>(g.perChannel() - 1);
    const T *pgamma = gamma->dataAs<T>(), *pbeta = beta->dataAs<T>();
    const T *prmean = runningMean->dataAs<T>(), *prvar = runningVar->dataAs<T>();
    T *prm = nrm->dataAs<T>(), *prv = nrv->dataAs<T>();
    for (int64_t c = 0; c < g.C; ++c) {
        const size_t u = static_cast<size_t>(c);
        prm[c] = keep * prmean[c] + mix * static_cast<T>(stats.mean[u]);
        prv[c] = keep * prvar[c] + mix * static_cast<T>(stats.var[u] * unbiased);
    }
    const T *psrc = input->dataAs<T>();
    T *py         = y->dataAs<T>();
    parallelFor(
        g.N * g.C,
        std::max<int64_t>(1, 16384 / std::max<int64_t>(g.inner, 1)),
        [&](int64_t begin, int64_t end) {
            for (int64_t plane = begin; plane < end; ++plane) {
                const int64_t c = plane % g.C;
                const size_t u  = static_cast<size_t>(c);
                const T scale   = pgamma[c] * static_cast<T>(stats.invStd[u]);
                const T shift   = pbeta[c] - static_cast<T>(stats.mean[u]) * scale;
                const T *src    = psrc + plane * g.inner;
                T *dst          = py + plane * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    dst[i] = src[i] * scale + shift;
                }
            }
        });
    *outY           = y;
    *outRunningMean = nrm;
    *outRunningVar  = nrv;
}

template <typename T>
void batchNormTrainGradIn(
    const BnTrainGeometry &g, const Array *input, const Array *gamma, const Array *dy, double eps,
    Array **outDx, Array **outDgamma, Array **outDbeta, mm::IAllocator &allocator) {
    const Shape statShape{g.C};
    Array *dx = createTensor(dtypeOf<T>(), input->shapeSpan(), allocator);
    Array *dg = createTensor(dtypeOf<T>(), statShape, allocator);
    Array *db = createTensor(dtypeOf<T>(), statShape, allocator);

    const BnTrainStats stats = bnTrainStats(input->dataAs<T>(), g, eps);
    const T *pgamma          = gamma->dataAs<T>();
    const T *psrc            = input->dataAs<T>();
    const T *pdy             = dy->dataAs<T>();
    const double M           = static_cast<double>(g.perChannel());
    T *pdx = dx->dataAs<T>(), *pdg = dg->dataAs<T>(), *pdb = db->dataAs<T>();
    // Per-channel sums of dy and dy*xhat (double accumulators).
    parallelFor(g.C, 1, [&](int64_t begin, int64_t end) {
        for (int64_t c = begin; c < end; ++c) {
            const size_t u  = static_cast<size_t>(c);
            const double mu = stats.mean[u], inv = stats.invStd[u];
            double sumDy = 0.0, sumDyXhat = 0.0;
            for (int64_t n = 0; n < g.N; ++n) {
                const T *xplane = psrc + (n * g.C + c) * g.inner;
                const T *dplane = pdy + (n * g.C + c) * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    const double xhat = (static_cast<double>(xplane[i]) - mu) * inv;
                    sumDy += static_cast<double>(dplane[i]);
                    sumDyXhat += static_cast<double>(dplane[i]) * xhat;
                }
            }
            pdb[c] = static_cast<T>(sumDy);
            pdg[c] = static_cast<T>(sumDyXhat);
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
                const T ginv                        = pgamma[c] * static_cast<T>(inv);
                const T meanDy                      = pdb[c] / static_cast<T>(M);
                const T meanDxh                     = pdg[c] / static_cast<T>(M);
                const T *xplane                     = psrc + plane * g.inner;
                const T *dplane                     = pdy + plane * g.inner;
                T *dst                              = pdx + plane * g.inner;
                for (int64_t i = 0; i < g.inner; ++i) {
                    const T xhat = (xplane[i] - static_cast<T>(mu)) * static_cast<T>(inv);
                    dst[i]       = ginv * (dplane[i] - meanDy - xhat * meanDxh);
                }
            }
        });
    *outDx     = dx;
    *outDgamma = dg;
    *outDbeta  = db;
}

} // namespace

void batchNormTrain(
    const Array *input, const Array *gamma, const Array *beta,
    const Array *runningMean, const Array *runningVar, double momentum, double eps,
    Array **outY, Array **outRunningMean, Array **outRunningVar,
    mm::IAllocator &allocator) {
    const BnTrainGeometry g =
        bnTrainGeometry(input, {gamma, beta, runningMean, runningVar}, "batch_norm_train");
    const TypeCode code =
        computableElementType("batch_norm_train", {input, gamma, beta, runningMean, runningVar});
    return forElementType(code, [&]<typename T>() {
        batchNormTrainIn<T>(
            g, input, gamma, beta, runningMean, runningVar, momentum, eps, outY, outRunningMean,
            outRunningVar, allocator);
    });
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
    const TypeCode code = computableElementType("batch_norm_train_grad", {input, gamma, dy});
    return forElementType(code, [&]<typename T>() {
        batchNormTrainGradIn<T>(g, input, gamma, dy, eps, outDx, outDgamma, outDbeta, allocator);
    });
}

} // namespace camel::tensor::kernels