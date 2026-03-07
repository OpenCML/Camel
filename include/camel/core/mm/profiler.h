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
 * Created: Feb. 22, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

namespace camel::core::mm {
namespace profiler {

// 采集所有内存区域快照，返回 JSON 字符串
std::string snapshotToJson();

// ---- Region 视图 API（供 Debugger Web UI 使用） ----

// 二进制视图：读取某 region 的原始内存区间 [offset, offset+limit)，返回原始字节数组（无格式化）。
// 仅支持连续内存的 region（bump: birthSpace/havenSpace/cacheSpace）。
// 返回 JSON：{ "region", "offset", "limit", "capacity", "used", "data": [0..255], "hasMore" }。
// limit 建议不超过 4096；格式化、分页由 Web UI 完成。
std::string regionMemoryRawToJson(const char *regionName, size_t offset, size_t limit);

// 对象视图：返回某 region 内对象列表，分页 [offset, offset+limit) 条。
// 返回 JSON：{ "region", "objects": [ { "addr", "size", "age", "region" }, ... ], "total",
// "hasMore" }。 当前仅对 bump 类型 region 实现；其他类型暂返回空列表（架构预留）。
std::string regionObjectsToJson(const char *regionName, size_t offset, size_t limit);

} // namespace profiler
} // namespace camel::core::mm
