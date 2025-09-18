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
 * Created: Sep. 09, 2025
 * Updated: Sep. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "error/runtime.h"
#include "execute/executor/executor.h"

class BasicBuiltinExecutor : public Executor {
  public:
    BasicBuiltinExecutor(context_ptr_t ctx);
    virtual ~BasicBuiltinExecutor() = default;

    static executor_ptr_t create(context_ptr_t ctx) {
        return std::make_shared<BasicBuiltinExecutor>(ctx);
    }

    virtual EvalResultCode eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame) override;
};