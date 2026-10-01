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
 * Native nn module.
 *
 * Neural-network building blocks on top of the tensor module: convolution,
 * pooling, embedding, batch norm and loss operators with their derivative
 * rules, and optimizers over parameter trees (see optim.h). Gradients come
 * from the autodiff module.
 */

#pragma once

#include "camel/core/module/builtin.h"

class NnModule : public camel::core::module::BuiltinModule {
  public:
    explicit NnModule(camel::core::context::context_ptr_t ctx);
    ~NnModule() override = default;

    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
