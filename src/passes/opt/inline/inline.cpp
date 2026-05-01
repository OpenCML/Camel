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
 * Created: Oct. 25, 2025
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "inline.h"

#include "engine.h"

camel::runtime::GCGraph *
InlineRewritePass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    OptimizeRewriteConfig config{
        .enableDevirtualization = false,
        .enableSpecialization   = false,
        .enableInlining         = true,
        .inlineConfig           = config_,
    };
    return applyOptimizeRewritePass(context_, graph, os, config);
}
