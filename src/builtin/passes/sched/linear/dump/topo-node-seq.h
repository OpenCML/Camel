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
 * Created: Sep. 05, 2025
 * Updated: Sep. 05, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../linear.h"
#include "ostream"

class TopoNodeSeqDumpPass : public LinearSchedPass {
    bool showRawPtr = false;
    std::unordered_map<std::string, size_t> ptrCnt_;
    std::unordered_map<std::string, std::unordered_map<uintptr_t, size_t>> ptrsMap_;
    std::string pointerToIdent(const void *ptr, const char *prefix = "N");

  public:
    TopoNodeSeqDumpPass(const context_ptr_t &ctx) : LinearSchedPass(ctx) {};
    virtual ~TopoNodeSeqDumpPass() = default;

    virtual std::any apply(GIR::graph_ptr_t &graph) override {
        return apply(const_cast<const GIR::graph_ptr_t &>(graph));
    }
    virtual std::any apply(const GIR::graph_ptr_t &graph) override;
};