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
 * Created: Feb. 08, 2026
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "execute/trans.h"

/**
 * JitRmirDumpPass (std::rmir): 字节码直接编译得到的、基于虚拟寄存器的 MIR，未做优化。
 */
class JitRmirDumpPass : public GraphTranslatePass {
  public:
    JitRmirDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {}
    virtual ~JitRmirDumpPass() = default;
    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};

/**
 * JitMirDumpPass (std::mir): 经过多遍优化后的最终 vreg MIR（优化遍入口在
 * runMirOptimizationPasses）。
 */
class JitMirDumpPass : public GraphTranslatePass {
  public:
    JitMirDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {}
    virtual ~JitMirDumpPass() = default;
    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
