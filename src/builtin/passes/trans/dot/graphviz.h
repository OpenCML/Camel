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
 * Created: Oct. 21, 2024
 * Updated: Oct. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../trans.h"

#include <unordered_set>

class GraphVizDumpPass : public GraphTranslatePass {
    bool showRawPtr = false;
    std::unordered_map<std::string, size_t> ptrCnt_;
    std::unordered_map<std::string, std::unordered_map<uintptr_t, size_t>> ptrsMap_;
    std::unordered_set<GraphIR::graph_ptr_t> visitedGraphs_;

    size_t depth_ = 0;
    std::string baseIndent_;
    const std::string indent_ = "    ";

    void pushIndent();
    void popIndent();

    std::string pointerToIdent(const void *ptr, const char *prefix = "N");

    std::string dumpGraph(const GraphIR::graph_ptr_t &graph);

  public:
    GraphVizDumpPass(const context_ptr_t &context);
    virtual ~GraphVizDumpPass() = default;

    GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
