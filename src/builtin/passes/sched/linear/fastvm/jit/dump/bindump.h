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
 * Created: Feb. 06, 2026
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "execute/trans.h"

/**
 * JitBinaryDumpPass: 对所有图尝试 JIT 编译，并将生成的机器码可视化打印。
 * 参照 LinkedBytecodeDumpPass 的格式，输出每个图的机器码（十六进制）。
 */
class JitBinaryDumpPass : public GraphTranslatePass {
  public:
    JitBinaryDumpPass(const context_ptr_t &ctx) : GraphTranslatePass(ctx) {}
    virtual ~JitBinaryDumpPass() = default;

    virtual GraphIR::graph_ptr_t apply(GraphIR::graph_ptr_t &graph, std::ostream &os) override;
};
