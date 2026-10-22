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
 * Author: Zhenjie Wei
 * Created: Jul. 29, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once
#include "camel/core/module/builtin.h"
class RandomModule : public camel::core::module::BuiltinModule {
  public:
    RandomModule(camel::core::context::context_ptr_t ctx);
    ~RandomModule() override = default;
    bool load() override;
    /// The operators of this module are named `rand:...`, not `random:...`, so the declaration
    /// module `stdlib/random.cml` may name them with a `#op` tag (handover H94, ruling 6, H202).
    std::vector<std::string> operatorProtocols() const override { return {"rand"}; }
    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
