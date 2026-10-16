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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Call-rewrite engine shared by the standalone std::opt::{inline,devirtualize,specialize}
 * stages and the integrated std::opt driver. The driver adds fold, cse and dce rounds;
 * this engine only performs the call rewrites selected by its configuration.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "config.h"

/// The growth budget in effect: CAMEL_OPT_GROWTH_BUDGET when it holds a number, else
/// `config.growthBudgetNodes`.
size_t optimizeGrowthBudget(const OptimizeRewriteConfig &config);

camel::runtime::GCGraph *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::ostream &os, const OptimizeRewriteConfig &config, bool *changed = nullptr);
