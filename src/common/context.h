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
 * Created: Aug. 18, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>

#include "module/main.h"

class Context : public std::enable_shared_from_this<Context> {
    module_ptr_t mainModule_;
    std::unordered_map<std::string, module_ptr_t> modules_;

    void setMainModule(const module_ptr_t &module) { mainModule_ = module; }
    public:
    Context() = default;
    virtual ~Context() = default;

    static context_ptr_t create() {
        auto ctx = std::make_shared<Context>();
        ctx->setMainModule(MainModule::create(ctx));
        return ctx;
    }
};

using context_ptr_t = std::shared_ptr<Context>;
