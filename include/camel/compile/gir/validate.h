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
 * Created: Mar. 29, 2026
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "graph.h"

namespace camel::compile::gir::validate {

// 在 GraphBuilder::sealGraph() 之前必须满足的结构不变量（ASSERT，失败即中止）。
// 不做任何图改写；仅用于 draft → sealed 前的自检与 rewrite 会话 seal()。
void assertGraphSealingPreconditions(const Graph &graph);
// 对 root 及其 subGraphs()/dependencies() 做深度优先，逐图执行同上断言。
void assertGraphTreeSealingPreconditions(const graph_ptr_t &graph);

} // namespace camel::compile::gir::validate
