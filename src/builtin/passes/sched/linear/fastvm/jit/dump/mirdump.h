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
 * Updated: Feb. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "execute/trans.h"

/**
 * JitMirDumpPass: 对所有图尝试 JIT 编译，仅输出优化后的 MIR（不生成机器码）。
 * 用于调试与查看字节码→MIR 的翻译结果。
 */
class JitMirDumpPass : public GraphTranslatePass {
  public:
    JitMirDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {}
    virtual ~JitMirDumpPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
