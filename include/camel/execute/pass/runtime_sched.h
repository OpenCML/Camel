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
 * Created: Apr. 08, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

class RuntimeGraphSchedulePass : public RuntimeGraphIRPass {
  public:
    RuntimeGraphSchedulePass(const camel::core::context::context_ptr_t &ctx)
        : RuntimeGraphIRPass(ctx) {};
    ~RuntimeGraphSchedulePass() override = default;

    virtual GIR::graph_ptr_t apply(camel::runtime::GCGraph *graph, std::ostream &os) override = 0;
};
