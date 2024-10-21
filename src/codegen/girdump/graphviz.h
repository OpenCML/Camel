/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 21, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "common/context.h"
#include "common/pass.h"

class GraphVizPass : public GraphPass {
    context_ptr_t context_;
    size_t depth_ = 0;
    size_t tabSize_ = 4;
    std::string indent_;

    void pushIndent();
    void popIndent();

  public:
    GraphVizPass(const context_ptr_t &context) : context_(context) {};
    virtual ~GraphVizPass() = default;

    void reset() override;
    void reset(context_ptr_t &context);
    std::any apply(gir::graph_ptr_t &graph) override;
};