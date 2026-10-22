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
 * Author: Camel Contributors
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY: reads the standard tag `independent` from every graph of the program through the
 * runtime query a scheduler would use (camel/core/tags.h, declaresIndependentElements), and
 * prints the graph's portable content hash (`TAGS hash <name> <hex>`) so a test can show that
 * a difference only in a region's facts is a difference in the hashed content.
 */

#include "tags.h"

#include "camel/core/tags.h"
#include "camel/runtime/fact_persist.h"
#include "camel/runtime/graph.h"

#include <algorithm>
#include <format>
#include <vector>

const camel::runtime::Program *
TagsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    std::vector<camel::runtime::GCGraph *> graphs = program->graphs();
    std::sort(graphs.begin(), graphs.end(), [](const auto *a, const auto *b) {
        return a->name() < b->name();
    });
    for (const camel::runtime::GCGraph *graph : graphs) {
        os << std::format(
            "TAGS independent {} {}\n",
            graph->name(),
            camel::core::tag::declaresIndependentElements(*graph) ? "yes" : "no");
        os << std::format(
            "TAGS hash {} {:016x}\n",
            graph->name(),
            camel::runtime::fact::portableGraphHash(*program, graph));
    }
    return program;
}
