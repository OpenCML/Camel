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
 */

#pragma once
#include "camel/core/module/builtin.h"

class HashModule : public camel::core::module::BuiltinModule {
  public:
    HashModule(camel::core::context::context_ptr_t ctx);
    ~HashModule() override = default;
    bool load() override;
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
