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
 * Shared optimizer entrypoint used by currently wired optimization wrappers.
 *
 * The runtime-native inline phase is complete and routed through
 * InlineRewritePass. The remaining optimization phases still need their own
 * GCGraph-native implementations, so this bridge intentionally degrades to the
 * inline subset instead of keeping the old GIR rewrite session alive here.
 */

#include "engine.h"

#include "camel/utils/log.h"
#include "inline.h"

camel::runtime::GCGraph *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    std::ostream &os, const OptimizeRewriteConfig &config) {
    if (config.enableDevirtualization || config.enableSpecialization) {
        CAMEL_LOG_WARN_S(
            "OptimizePass",
            "Runtime optimize bridge currently implements inline only; "
            "devirtualization={}, specialization={} are ignored.",
            config.enableDevirtualization ? "on" : "off",
            config.enableSpecialization ? "on" : "off");
    }
    InlineRewritePass pass(context, config);
    return pass.apply(graph, os);
}
