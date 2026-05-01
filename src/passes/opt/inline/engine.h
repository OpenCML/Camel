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
 * Updated: May. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Shared runtime rewrite engine entrypoint.
 *
 * `std::inline` and `std::opt` both route
 * through the same GCGraph-native
 * rewrite substrate. The difference is purely configuration:
 * `std::inline`
 * enables only inlining, while `std::opt` may additionally enable
 *
 * devirtualization and specialization.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "config.h"

camel::runtime::GCGraph *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::ostream &os, const OptimizeRewriteConfig &config);
