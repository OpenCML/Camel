/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * GIR 懒加载 JSON 序列化：按图 ID 返回摘要或单图展开（nodes/edges），
 * 供前端 dagre + Vue Flow 渲染。ID 为 graph/node 指针地址字符串。
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

/// 返回节点的稳定 ID（graphId_nIndex），与 JSON 中 stableId 一致，用于跨 Run 断点匹配。
std::string getStableNodeId(const GIR::Node *node);

} // namespace debugger
