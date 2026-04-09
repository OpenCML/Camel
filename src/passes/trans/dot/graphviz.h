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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/execute/pass/runtime_trans.h"

#include <unordered_set>

class GraphVizDumpPass : public RuntimeGraphTranslatePass {
    bool showRawPtr = false;
    std::unordered_map<std::string, size_t> ptrCnt_;
    std::unordered_map<std::string, std::unordered_map<uintptr_t, size_t>> ptrsMap_;
    std::unordered_set<camel::runtime::GCGraph *> visitedGraphs_;

    size_t depth_ = 0;
    std::string baseIndent_;
    const std::string indent_ = "    ";

    void pushIndent();
    void popIndent();

    std::string pointerToIdent(const void *ptr, const char *prefix = "N");

    std::string dumpGraph(camel::runtime::GCGraph *graph);

  public:
    GraphVizDumpPass(const camel::core::context::context_ptr_t &context);
    virtual ~GraphVizDumpPass() = default;

    GIR::graph_ptr_t apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
