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
 * Fused neural-network loss primitives.
 *
 * These operators are deliberately small and stable. They provide the Phase 2
 * classification-loss path without expanding the tensor module into a complete
 * softmax/log/gather stack before Camel has first-class axis semantics.
 */

#include "operators.h"

#include "../tensor/runtime.h"
#include "../tensor/type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

namespace tensor = camel::tensor;

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

void requireRank2LogitsAndTarget(
    const tensor::TensorObject *logits, const tensor::TensorObject *target, const char *what) {
    if (!logits || logits->rank() != 2) {
        throw std::invalid_argument(std::string(what) + " expects rank-2 logits [batch, classes]");
    }
    if (!target || target->rank() != 2) {
        throw std::invalid_argument(std::string(what) + " expects rank-2 target [batch, classes]");
    }
    if (!logits->sameShape(target)) {
        throw std::invalid_argument(
            std::string(what) + " requires logits and target shapes to match");
    }
    if (!tensor::isFloatingTensorType(logits->dtype()) ||
        !tensor::isFloatingTensorType(target->dtype())) {
        throw std::invalid_argument(std::string(what) + " requires floating Tensor inputs");
    }
    if (logits->dim(0) <= 0 || logits->dim(1) <= 0) {
        throw std::invalid_argument(
            std::string(what) + " requires non-empty batch and class dimensions");
    }
}

double softmaxCrossEntropy(const tensor::TensorObject *logits, const tensor::TensorObject *target) {
    constexpr const char *kWhat = "softmax_cross_entropy";
    requireRank2LogitsAndTarget(logits, target, kWhat);

    const int64_t rows = logits->dim(0);
    const int64_t cols = logits->dim(1);
    double loss        = 0.0;

    for (int64_t row = 0; row < rows; ++row) {
        const uint64_t rowOffset = static_cast<uint64_t>(row * cols);
        double rowMax            = logits->getAsDouble(rowOffset);
        for (int64_t col = 1; col < cols; ++col) {
            rowMax = std::max(rowMax, logits->getAsDouble(rowOffset + static_cast<uint64_t>(col)));
        }

        double expSum = 0.0;
        for (int64_t col = 0; col < cols; ++col) {
            expSum +=
                std::exp(logits->getAsDouble(rowOffset + static_cast<uint64_t>(col)) - rowMax);
        }
        const double logZ = rowMax + std::log(expSum);

        for (int64_t col = 0; col < cols; ++col) {
            const uint64_t index = rowOffset + static_cast<uint64_t>(col);
            const double label   = target->getAsDouble(index);
            if (label < 0.0) {
                throw std::invalid_argument(
                    "softmax_cross_entropy target values must be non-negative");
            }
            loss -= label * (logits->getAsDouble(index) - logZ);
        }
    }

    return loss / static_cast<double>(rows);
}

tensor::TensorObject *softmaxCrossEntropyGradLogits(
    const tensor::TensorObject *logits, const tensor::TensorObject *target, double dy,
    mm::IAllocator &allocator) {
    constexpr const char *kWhat = "softmax_cross_entropy_grad";
    requireRank2LogitsAndTarget(logits, target, kWhat);

    const int64_t rows         = logits->dim(0);
    const int64_t cols         = logits->dim(1);
    tensor::TensorObject *grad = tensor::TensorObject::create(
        TypeCode::Float32,
        std::span<const int64_t>(logits->shape(), logits->rank()),
        allocator,
        false);

    const double scale = dy / static_cast<double>(rows);
    std::vector<double> exps(static_cast<size_t>(cols));
    for (int64_t row = 0; row < rows; ++row) {
        const uint64_t rowOffset = static_cast<uint64_t>(row * cols);
        double rowMax            = logits->getAsDouble(rowOffset);
        for (int64_t col = 1; col < cols; ++col) {
            rowMax = std::max(rowMax, logits->getAsDouble(rowOffset + static_cast<uint64_t>(col)));
        }

        double expSum    = 0.0;
        double targetSum = 0.0;
        for (int64_t col = 0; col < cols; ++col) {
            const uint64_t index = rowOffset + static_cast<uint64_t>(col);
            const double label   = target->getAsDouble(index);
            if (label < 0.0) {
                throw std::invalid_argument(
                    "softmax_cross_entropy target values must be non-negative");
            }
            const double expValue          = std::exp(logits->getAsDouble(index) - rowMax);
            exps[static_cast<size_t>(col)] = expValue;
            expSum += expValue;
            targetSum += label;
        }
        if (targetSum <= 0.0) {
            throw std::invalid_argument(
                "softmax_cross_entropy target rows must have positive mass");
        }

        for (int64_t col = 0; col < cols; ++col) {
            const uint64_t index = rowOffset + static_cast<uint64_t>(col);
            const double prob    = exps[static_cast<size_t>(col)] / expSum;
            const double label   = target->getAsDouble(index);
            grad->setFromDouble(index, scale * (prob * targetSum - label));
        }
    }

    return grad;
}

template <typename Fn> slot_t withLossErrors(Fn &&fn) {
    try {
        return fn();
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, e.what());
    }
}

} // namespace

slot_t __nn_softmax_cross_entropy__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withLossErrors([&]() -> slot_t {
        return toSlot(softmaxCrossEntropy(
            requireTensor(norm, 0, "softmax_cross_entropy"),
            requireTensor(norm, 1, "softmax_cross_entropy")));
    });
}

slot_t __nn_softmax_cross_entropy_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withLossErrors([&]() -> slot_t {
        tensor::TensorObject *grad = softmaxCrossEntropyGradLogits(
            requireTensor(norm, 0, "softmax_cross_entropy_grad"),
            requireTensor(norm, 1, "softmax_cross_entropy_grad"),
            norm.get<Float64>(2),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(grad));
    });
}
