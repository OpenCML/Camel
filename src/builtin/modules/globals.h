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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/module/builtin.h"

class GlobalsBuiltinModule : public BuiltinModule {
  public:
    GlobalsBuiltinModule(context_ptr_t ctx);
    virtual ~GlobalsBuiltinModule() = default;

    virtual bool load() override;

    void exportBinaryOp(const std::string &name, const std::string &uri);
    void exportAssnOp(const std::string &name, const std::string &uri);

    static module_ptr_t create(context_ptr_t ctx) {
        return std::make_shared<GlobalsBuiltinModule>(ctx);
    }
};