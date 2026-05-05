/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Minimal CPU conv2d primitives for Phase 2 NN autograd.
 *
 * The supported layout is NCHW input `[N, C, H, W]`, kernel
 * `[O, C, KH, KW]`, rank-1 bias `[O]`, stride 1, valid padding. The backward
 * helpers emit dense gradients for input, kernel, and bias so ordinary VJP
 * traversal and Parameter SGD can train tiny CNN models without special cases.
 */

#include "operators.h"

#include "../tensor/runtime.h"
#include "../tensor/type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"

#include <span>
#include <stdexcept>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

namespace tensor = camel::tensor;

struct Conv2DShape {
    int64_t batch;
    int64_t inChannels;
    int64_t inHeight;
    int64_t inWidth;
    int64_t outChannels;
    int64_t kernelHeight;
    int64_t kernelWidth;
    int64_t outHeight;
    int64_t outWidth;
};

bool isTensorType(Type *type) { return type && type->code() == tensor::TensorType::typeCode(); }

tensor::TensorObject *requireTensor(ArgsView &norm, size_t index, const char *what) {
    if (!isTensorType(norm.type(index))) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "{} expects Tensor arguments", what);
    }
    auto *value = norm.get<tensor::TensorObject *>(index);
    if (!value) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "{} received a null Tensor", what);
    }
    return value;
}

void requireFloating(const tensor::TensorObject *tensor, const char *what) {
    if (!tensor || !tensor::isFloatingTensorType(tensor->dtype())) {
        throw std::invalid_argument(std::string(what) + " requires floating-point tensors");
    }
}

Conv2DShape convShape(
    const tensor::TensorObject *input, const tensor::TensorObject *kernel,
    const tensor::TensorObject *bias, const char *what) {
    requireFloating(input, what);
    requireFloating(kernel, what);
    requireFloating(bias, what);
    if (input->rank() != 4) {
        throw std::invalid_argument(std::string(what) + " expects NCHW rank-4 input");
    }
    if (kernel->rank() != 4) {
        throw std::invalid_argument(std::string(what) + " expects rank-4 kernel [O, C, KH, KW]");
    }
    if (bias->rank() != 1) {
        throw std::invalid_argument(std::string(what) + " expects rank-1 bias [O]");
    }
    if (input->dim(1) != kernel->dim(1)) {
        throw std::invalid_argument("conv2d input channels must match kernel channels");
    }
    if (bias->dim(0) != kernel->dim(0)) {
        throw std::invalid_argument("conv2d bias size must match output channels");
    }
    if (kernel->dim(2) <= 0 || kernel->dim(3) <= 0 || input->dim(2) < kernel->dim(2) ||
        input->dim(3) < kernel->dim(3)) {
        throw std::invalid_argument("conv2d kernel spatial dimensions must fit inside input");
    }

    return Conv2DShape{
        .batch        = input->dim(0),
        .inChannels   = input->dim(1),
        .inHeight     = input->dim(2),
        .inWidth      = input->dim(3),
        .outChannels  = kernel->dim(0),
        .kernelHeight = kernel->dim(2),
        .kernelWidth  = kernel->dim(3),
        .outHeight    = input->dim(2) - kernel->dim(2) + 1,
        .outWidth     = input->dim(3) - kernel->dim(3) + 1,
    };
}

void requireDyShape(const tensor::TensorObject *dy, const Conv2DShape &shape, const char *what) {
    requireFloating(dy, what);
    if (dy->rank() != 4 || dy->dim(0) != shape.batch || dy->dim(1) != shape.outChannels ||
        dy->dim(2) != shape.outHeight || dy->dim(3) != shape.outWidth) {
        throw std::invalid_argument(std::string(what) + " dy shape must match conv2d output");
    }
}

uint64_t
offset4(int64_t a, int64_t b, int64_t c, int64_t d, int64_t dim1, int64_t dim2, int64_t dim3) {
    return static_cast<uint64_t>(((a * dim1 + b) * dim2 + c) * dim3 + d);
}

tensor::TensorObject *conv2d(
    const tensor::TensorObject *input, const tensor::TensorObject *kernel,
    const tensor::TensorObject *bias, mm::IAllocator &allocator) {
    constexpr const char *kWhat = "conv2d";
    const Conv2DShape s         = convShape(input, kernel, bias, kWhat);
    int64_t outShape[]          = {s.batch, s.outChannels, s.outHeight, s.outWidth};
    tensor::TensorObject *out =
        tensor::TensorObject::create(TypeCode::Float32, outShape, allocator, false);

    for (int64_t n = 0; n < s.batch; ++n) {
        for (int64_t oc = 0; oc < s.outChannels; ++oc) {
            for (int64_t oh = 0; oh < s.outHeight; ++oh) {
                for (int64_t ow = 0; ow < s.outWidth; ++ow) {
                    double acc = bias->getAsDouble(static_cast<uint64_t>(oc));
                    for (int64_t ic = 0; ic < s.inChannels; ++ic) {
                        for (int64_t kh = 0; kh < s.kernelHeight; ++kh) {
                            for (int64_t kw = 0; kw < s.kernelWidth; ++kw) {
                                const uint64_t inputIndex = offset4(
                                    n,
                                    ic,
                                    oh + kh,
                                    ow + kw,
                                    s.inChannels,
                                    s.inHeight,
                                    s.inWidth);
                                const uint64_t kernelIndex = offset4(
                                    oc,
                                    ic,
                                    kh,
                                    kw,
                                    s.inChannels,
                                    s.kernelHeight,
                                    s.kernelWidth);
                                acc += input->getAsDouble(inputIndex) *
                                       kernel->getAsDouble(kernelIndex);
                            }
                        }
                    }
                    out->setFromDouble(
                        offset4(n, oc, oh, ow, s.outChannels, s.outHeight, s.outWidth),
                        acc);
                }
            }
        }
    }
    return out;
}

tensor::TensorObject *conv2dInputGrad(
    const tensor::TensorObject *input, const tensor::TensorObject *kernel,
    const tensor::TensorObject *dy, mm::IAllocator &allocator) {
    requireFloating(kernel, "conv2d_input_grad");
    if (kernel->rank() != 4) {
        throw std::invalid_argument("conv2d_input_grad expects rank-4 kernel [O, C, KH, KW]");
    }
    int64_t dummyBiasShape[] = {kernel->dim(0)};
    tensor::TensorObject *dummyBias =
        tensor::tensorZeros(TypeCode::Float32, dummyBiasShape, allocator);
    const Conv2DShape s = convShape(input, kernel, dummyBias, "conv2d_input_grad");
    requireDyShape(dy, s, "conv2d_input_grad");
    tensor::TensorObject *grad = tensor::tensorZeros(
        input->dtype(),
        std::span<const int64_t>(input->shape(), input->rank()),
        allocator);

    for (int64_t n = 0; n < s.batch; ++n) {
        for (int64_t oc = 0; oc < s.outChannels; ++oc) {
            for (int64_t oh = 0; oh < s.outHeight; ++oh) {
                for (int64_t ow = 0; ow < s.outWidth; ++ow) {
                    const double dyValue = dy->getAsDouble(
                        offset4(n, oc, oh, ow, s.outChannels, s.outHeight, s.outWidth));
                    for (int64_t ic = 0; ic < s.inChannels; ++ic) {
                        for (int64_t kh = 0; kh < s.kernelHeight; ++kh) {
                            for (int64_t kw = 0; kw < s.kernelWidth; ++kw) {
                                const uint64_t inputIndex = offset4(
                                    n,
                                    ic,
                                    oh + kh,
                                    ow + kw,
                                    s.inChannels,
                                    s.inHeight,
                                    s.inWidth);
                                const uint64_t kernelIndex = offset4(
                                    oc,
                                    ic,
                                    kh,
                                    kw,
                                    s.inChannels,
                                    s.kernelHeight,
                                    s.kernelWidth);
                                grad->setFromDouble(
                                    inputIndex,
                                    grad->getAsDouble(inputIndex) +
                                        dyValue * kernel->getAsDouble(kernelIndex));
                            }
                        }
                    }
                }
            }
        }
    }
    return grad;
}

tensor::TensorObject *conv2dKernelGrad(
    const tensor::TensorObject *input, const tensor::TensorObject *kernel,
    const tensor::TensorObject *dy, mm::IAllocator &allocator) {
    requireFloating(kernel, "conv2d_kernel_grad");
    if (kernel->rank() != 4) {
        throw std::invalid_argument("conv2d_kernel_grad expects rank-4 kernel [O, C, KH, KW]");
    }
    int64_t dummyBiasShape[] = {kernel->dim(0)};
    tensor::TensorObject *dummyBias =
        tensor::tensorZeros(TypeCode::Float32, dummyBiasShape, allocator);
    const Conv2DShape s = convShape(input, kernel, dummyBias, "conv2d_kernel_grad");
    requireDyShape(dy, s, "conv2d_kernel_grad");
    tensor::TensorObject *grad = tensor::tensorZeros(
        kernel->dtype(),
        std::span<const int64_t>(kernel->shape(), kernel->rank()),
        allocator);

    for (int64_t oc = 0; oc < s.outChannels; ++oc) {
        for (int64_t ic = 0; ic < s.inChannels; ++ic) {
            for (int64_t kh = 0; kh < s.kernelHeight; ++kh) {
                for (int64_t kw = 0; kw < s.kernelWidth; ++kw) {
                    double acc = 0.0;
                    for (int64_t n = 0; n < s.batch; ++n) {
                        for (int64_t oh = 0; oh < s.outHeight; ++oh) {
                            for (int64_t ow = 0; ow < s.outWidth; ++ow) {
                                acc += input->getAsDouble(offset4(
                                           n,
                                           ic,
                                           oh + kh,
                                           ow + kw,
                                           s.inChannels,
                                           s.inHeight,
                                           s.inWidth)) *
                                       dy->getAsDouble(offset4(
                                           n,
                                           oc,
                                           oh,
                                           ow,
                                           s.outChannels,
                                           s.outHeight,
                                           s.outWidth));
                            }
                        }
                    }
                    grad->setFromDouble(
                        offset4(oc, ic, kh, kw, s.inChannels, s.kernelHeight, s.kernelWidth),
                        acc);
                }
            }
        }
    }
    return grad;
}

tensor::TensorObject *conv2dBiasGrad(
    const tensor::TensorObject *bias, const tensor::TensorObject *dy, mm::IAllocator &allocator) {
    requireFloating(bias, "conv2d_bias_grad");
    requireFloating(dy, "conv2d_bias_grad");
    if (bias->rank() != 1 || dy->rank() != 4 || dy->dim(1) != bias->dim(0)) {
        throw std::invalid_argument("conv2d_bias_grad requires bias [O] and dy [N, O, H, W]");
    }
    tensor::TensorObject *grad = tensor::tensorZeros(
        bias->dtype(),
        std::span<const int64_t>(bias->shape(), bias->rank()),
        allocator);
    for (int64_t n = 0; n < dy->dim(0); ++n) {
        for (int64_t oc = 0; oc < dy->dim(1); ++oc) {
            double acc = grad->getAsDouble(static_cast<uint64_t>(oc));
            for (int64_t oh = 0; oh < dy->dim(2); ++oh) {
                for (int64_t ow = 0; ow < dy->dim(3); ++ow) {
                    acc +=
                        dy->getAsDouble(offset4(n, oc, oh, ow, dy->dim(1), dy->dim(2), dy->dim(3)));
                }
            }
            grad->setFromDouble(static_cast<uint64_t>(oc), acc);
        }
    }
    return grad;
}

template <typename Fn> slot_t withConvErrors(Fn &&fn) {
    try {
        return fn();
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, e.what());
    }
}

} // namespace

slot_t __nn_conv2d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withConvErrors([&]() -> slot_t {
        tensor::TensorObject *out = conv2d(
            requireTensor(norm, 0, "conv2d"),
            requireTensor(norm, 1, "conv2d"),
            requireTensor(norm, 2, "conv2d"),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(out));
    });
}

slot_t __nn_conv2d_input_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withConvErrors([&]() -> slot_t {
        tensor::TensorObject *grad = conv2dInputGrad(
            requireTensor(norm, 0, "conv2d_input_grad"),
            requireTensor(norm, 1, "conv2d_input_grad"),
            requireTensor(norm, 2, "conv2d_input_grad"),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(grad));
    });
}

slot_t __nn_conv2d_kernel_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withConvErrors([&]() -> slot_t {
        tensor::TensorObject *grad = conv2dKernelGrad(
            requireTensor(norm, 0, "conv2d_kernel_grad"),
            requireTensor(norm, 1, "conv2d_kernel_grad"),
            requireTensor(norm, 2, "conv2d_kernel_grad"),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(grad));
    });
}

slot_t __nn_conv2d_bias_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withConvErrors([&]() -> slot_t {
        tensor::TensorObject *grad = conv2dBiasGrad(
            requireTensor(norm, 0, "conv2d_bias_grad"),
            requireTensor(norm, 1, "conv2d_bias_grad"),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(grad));
    });
}
