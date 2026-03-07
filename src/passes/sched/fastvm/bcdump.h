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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/execute/pass/trans.h"

namespace ctx = camel::core::context;

class BytecodeDumpPass : public GraphTranslatePass {
  public:
    BytecodeDumpPass(const ctx::context_ptr_t &ctx) : GraphTranslatePass(ctx) {}
    virtual ~BytecodeDumpPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;
};

class LinkedBytecodeDumpPass : public GraphTranslatePass {
  public:
    LinkedBytecodeDumpPass(const ctx::context_ptr_t &ctx) : GraphTranslatePass(ctx) {}
    virtual ~LinkedBytecodeDumpPass() = default;

    virtual GIR::graph_ptr_t apply(GIR::graph_ptr_t &graph, std::ostream &os) override;
};
