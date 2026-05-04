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
 * Created: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "runtime.h"

#include "../tensor/type.h"
#include "type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"

#include <stdexcept>

namespace camel::nn {

using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using namespace camel::core::rtdata;
using namespace camel::core::type;
namespace tensor = camel::tensor;

namespace {

void requireFloatingTensor(const tensor::TensorObject *tensor, const char *what) {
    if (!tensor || !tensor::isFloatingTensorType(tensor->dtype())) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "{} requires a floating Tensor", what);
    }
}

void requireSameShape(
    const tensor::TensorObject *lhs, const tensor::TensorObject *rhs, const char *what) {
    if (!lhs || !rhs || !lhs->sameShape(rhs)) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "{} requires matching Tensor shapes", what);
    }
}

void fillTensor(tensor::TensorObject *tensor, double value) {
    requireFloatingTensor(tensor, "fill");
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        tensor->setFromDouble(i, value);
    }
}

} // namespace

ParameterObject::ParameterObject(tensor::TensorObject *data, tensor::TensorObject *grad)
    : data_(data), grad_(grad) {}

ParameterObject *ParameterObject::create(tensor::TensorObject *data, mm::IAllocator &allocator) {
    requireFloatingTensor(data, "parameter");
    tensor::TensorObject *ownedData = static_cast<tensor::TensorObject *>(
        data->clone(allocator, camel::tensor::TensorType::Default(), false));
    tensor::TensorObject *grad = tensor::tensorZeros(
        ownedData->dtype(),
        std::span<const int64_t>(ownedData->shape(), ownedData->rank()),
        allocator);
    void *mem = allocator.alloc(sizeof(ParameterObject), alignof(ParameterObject));
    if (!mem) {
        throw std::bad_alloc();
    }
    return new (mem) ParameterObject(ownedData, grad);
}

void ParameterObject::zeroGrad() { fillTensor(grad_, 0.0); }

void ParameterObject::addGrad(const tensor::TensorObject *grad) {
    requireFloatingTensor(grad_, "add_grad");
    requireFloatingTensor(grad, "add_grad");
    requireSameShape(grad_, grad, "add_grad");
    for (uint64_t i = 0; i < grad_->numel(); ++i) {
        grad_->setFromDouble(i, grad_->getAsDouble(i) + grad->getAsDouble(i));
    }
}

void ParameterObject::sgdUpdate(double lr) {
    requireFloatingTensor(data_, "sgd");
    requireFloatingTensor(grad_, "sgd");
    requireSameShape(data_, grad_, "sgd");
    for (uint64_t i = 0; i < data_->numel(); ++i) {
        data_->setFromDouble(i, data_->getAsDouble(i) - lr * grad_->getAsDouble(i));
    }
}

bool ParameterObject::equals(const Object *other, const Type *type, bool deep) const {
    (void)type;
    if (this == other) {
        return true;
    }
    auto *rhs = dynamic_cast<const ParameterObject *>(other);
    if (!rhs) {
        return false;
    }
    if (!deep) {
        return data_ == rhs->data_ && grad_ == rhs->grad_;
    }
    return data_->equals(rhs->data_, camel::tensor::TensorType::Default(), true) &&
           grad_->equals(rhs->grad_, camel::tensor::TensorType::Default(), true);
}

Object *ParameterObject::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    (void)type;
    (void)deep;
    tensor::TensorObject *dataClone = static_cast<tensor::TensorObject *>(
        data_->clone(allocator, camel::tensor::TensorType::Default(), false));
    tensor::TensorObject *gradClone = static_cast<tensor::TensorObject *>(
        grad_->clone(allocator, camel::tensor::TensorType::Default(), false));
    void *mem = allocator.alloc(sizeof(ParameterObject), alignof(ParameterObject));
    if (!mem) {
        throw std::bad_alloc();
    }
    return new (mem) ParameterObject(dataClone, gradClone);
}

void ParameterObject::print(std::ostream &os, const Type *type) const {
    (void)type;
    os << "Parameter(data=";
    if (data_) {
        data_->print(os, camel::tensor::TensorType::Default());
    } else {
        os << "null";
    }
    os << ")";
}

void ParameterObject::updateRefs(
    const std::function<Object *(Object *)> &relocate, const Type *type) {
    (void)type;
    if (data_) {
        data_ = static_cast<tensor::TensorObject *>(relocate(data_));
    }
    if (grad_) {
        grad_ = static_cast<tensor::TensorObject *>(relocate(grad_));
    }
}

ParameterObject *requireParameter(const ArgsView &args, size_t index) {
    auto *param = args.get<ParameterObject *>(index);
    if (!param) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "Expected Parameter argument");
    }
    return param;
}

} // namespace camel::nn
