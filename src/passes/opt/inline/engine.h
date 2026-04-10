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
 * Created: Apr. 10, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Shared runtime optimization engine entrypoint.
 *
 * At the moment this bridge only drives the runtime-native inline phase.
 * Devirtualization and specialization will move onto the same GCGraph-native
 * substrate later, but they are intentionally not claimed as implemented here.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "config.h"

camel::runtime::GCGraph *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::ostream &os, const OptimizeRewriteConfig &config);
