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
 * Created: Mar. 12, 2026
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/mm.h"

#include <memory>
#include <utility>

namespace camel::compile::gir {

// =============================================================================
// GraphArena：为 GIR 的 frozen 产物（FrameMeta、frozen static area 等）提供
// 独立的内存分配域。
//
// 根图在创建时分配一个 arena，子图默认继承外层图的 arena，从而保证：
//   - 同一图族的 frozen 产物具有内存局部性；
//   - GIR 可以脱离通用 metaspace 的长期驻留语义，在图被销毁时批量回收。
//
// 当前 Node 仍使用 std::unique_ptr 由 Graph::ownedNodes_ 管理，
// 未来可选择让 frozen Node 也进入 arena 以进一步提升地址稳定性和局部性。
// =============================================================================
class GraphArena {
  public:
    static constexpr size_t kDefaultCapacity = 8 * camel::core::mm::MB;

    explicit GraphArena(size_t capacity = kDefaultCapacity, const char *debugRegion = "graph_arena")
        : allocator_(capacity, debugRegion) {}

    camel::core::mm::IAllocator &allocator() { return allocator_; }
    const camel::core::mm::IAllocator &allocator() const { return allocator_; }

    template <typename T, typename... Args> T *construct(Args &&...args) {
        return camel::core::mm::constructAt<T>(allocator_, std::forward<Args>(args)...);
    }

  private:
    camel::core::mm::FreeListAllocator allocator_;
};

using graph_arena_ptr_t = std::shared_ptr<GraphArena>;

} // namespace camel::compile::gir
