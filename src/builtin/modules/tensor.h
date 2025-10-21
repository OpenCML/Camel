/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2025
 * Updated: Oct. 21, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/module/builtin.h"

class TensorBuiltinModule : public BuiltinModule {
  public:
    TensorBuiltinModule(context_ptr_t ctx);
    virtual ~TensorBuiltinModule() = default;

    virtual bool load() override;

    static module_ptr_t create(context_ptr_t ctx) {
        return std::make_shared<TensorBuiltinModule>(ctx);
    }
};