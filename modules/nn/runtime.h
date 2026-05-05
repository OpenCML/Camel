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

/*
 * Mutable neural-network runtime objects.
 *
 * Parameter keeps data and gradient buffers stable so optimizers can update
 * tensor memory in place without rebuilding model structs.
 */

#pragma once

#include "../tensor/runtime.h"

#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"

namespace camel::nn {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class ParameterObject : public rtdata::Object {
  public:
    ParameterObject(const ParameterObject &)            = delete;
    ParameterObject &operator=(const ParameterObject &) = delete;

    static ParameterObject *
    create(camel::tensor::TensorObject *data, mm::IAllocator &allocator);

    camel::tensor::TensorObject *data() const { return data_; }
    camel::tensor::TensorObject *grad() const { return grad_; }

    void zeroGrad();
    void addGrad(const camel::tensor::TensorObject *grad);
    void sgdUpdate(double lr);

    bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const type::Type *type, bool deep = false) const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override;

  private:
    ParameterObject(
        camel::tensor::TensorObject *data, camel::tensor::TensorObject *grad);

    camel::tensor::TensorObject *data_;
    camel::tensor::TensorObject *grad_;
};

ParameterObject *requireParameter(const ArgsView &args, size_t index);

} // namespace camel::nn
