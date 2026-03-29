/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Feb. 22, 2026
 * Updated: Mar. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/compile/gir.h"

#include <string>
#include <utility>

namespace camel::compile::gir {
class Node;
}

namespace debugger {

/// 将 GIR 序列化为 JSON。
/// root: 根图；graphId: 空 = 仅根图摘要，非空 = 展开该图（nodes/edges + 直接子图摘要）。
/// 返回 (json 字符串, 错误信息)，错误为空表示成功。
std::pair<std::string, std::string>
getGirJson(const GIR::graph_ptr_t &root, const std::string &graphId);

/// 返回节点的调试实体 ID，与 JSON 中 `stableId` 一致（seal 后为 `gnode:{032x}` 内容寻址形式）。
std::string getDebugNodeId(const GIR::Node *node);

} // namespace debugger
