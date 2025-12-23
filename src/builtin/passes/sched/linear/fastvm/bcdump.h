
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
 * Created: Oct. 21, 2025
 * Updated: Dec. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "execute/trans.h"

class BytecodeDumpPass : public GraphTranslatePass {
  public:
    BytecodeDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {};
    virtual ~BytecodeDumpPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};

class LinkedBytecodeDumpPass : public GraphTranslatePass {
  public:
    LinkedBytecodeDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {};
    virtual ~LinkedBytecodeDumpPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
