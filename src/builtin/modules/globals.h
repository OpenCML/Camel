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
 * Created: Jul. 29, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/module/builtin.h"

class GlobalsBuiltinModule : public camel::core::module::BuiltinModule {
  public:
    GlobalsBuiltinModule(camel::core::context::context_ptr_t ctx);
    virtual ~GlobalsBuiltinModule() = default;

    virtual bool load() override;

    void exportBinaryOp(const std::string &name, const std::string &uri);
    void exportAssnOp(const std::string &name, const std::string &uri);

    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};
