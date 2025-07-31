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

#include "module.h"

class Context;
using context_ptr_t = std::shared_ptr<Context>;

class UserDefinedModule : public Module {
    bool built_ = false;
    context_ptr_t context_;

  public:
    UserDefinedModule(const std::string &name, const std::string &path, context_ptr_t ctx)
        : Module(name, path), context_(ctx) {}
    virtual ~UserDefinedModule() = default;

    static module_ptr_t loadFromFile(const std::string &path, context_ptr_t ctx) {
        // Placeholder for loading module from file
        return nullptr;
    }

    bool ready() const { return built_; }
    void build();
};