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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "common/context.h"
#include "common/pass.h"

class GraphVizDumpPass : public GraphIRPass {
    context_ptr_t context_;

    uintptr_t ptrCnt = 0;
    bool showRawPtr = false;
    std::unordered_map<uintptr_t, uintptr_t> ptrsMap_;
    std::unordered_map<func_type_ptr_t, std::string> lambdaFuncIdents_;

    size_t depth_ = 0;
    std::string baseIndent_;
    const std::string indent_ = "    ";

    void pushIndent();
    void popIndent();

    std::string pointerToIdent(const void *ptr);

  public:
    GraphVizDumpPass(const context_ptr_t &context) : context_(context) {};
    virtual ~GraphVizDumpPass() = default;

    void reset() override;
    void reset(context_ptr_t &context);
    std::any apply(GIR::graph_ptr_t &graph) override;
};
