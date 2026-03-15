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

#include "camel/core/mm/alloc/allocator.h"

#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace camel::compile::gir {

// =============================================================================
// BumpAllocator：轻量级线性分配器，只支持 alloc 和整体 destroy。
//
// 为 frozen graph 的 Node / 邻接数组 / FrameMeta / static area 等平凡析构对象
// 提供整块连续内存。分配只做指针 bump，不安装 ObjectHeader，无逐个 free 语义。
// Arena 销毁时直接释放整块缓冲区，所有分配物随之回收。
//
// 实现 IAllocator 接口以便与 Tuple::create 等需要 IAllocator 的 API 兼容。
// free() 为空操作——arena 中的对象只在 arena 销毁时整体回收。
// =============================================================================
class BumpAllocator : public camel::core::mm::IAllocator {
  public:
    explicit BumpAllocator(size_t capacity) : capacity_(capacity) {
        buffer_ = static_cast<std::byte *>(std::malloc(capacity_));
        if (!buffer_) {
            throw std::bad_alloc();
        }
        cursor_ = buffer_;
        end_    = buffer_ + capacity_;
    }

    ~BumpAllocator() override { std::free(buffer_); }

    BumpAllocator(const BumpAllocator &)            = delete;
    BumpAllocator &operator=(const BumpAllocator &) = delete;
    BumpAllocator(BumpAllocator &&)                 = delete;
    BumpAllocator &operator=(BumpAllocator &&)      = delete;

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        uintptr_t cur     = reinterpret_cast<uintptr_t>(cursor_);
        uintptr_t aligned = (cur + align - 1) & ~(align - 1);
        std::byte *next   = reinterpret_cast<std::byte *>(aligned) + size;
        ASSERT(next <= end_, "BumpAllocator out of memory");
        cursor_ = next;
        return reinterpret_cast<void *>(aligned);
    }

    void free(void * /*ptr*/) override {}
    void reset() override { cursor_ = buffer_; }
    size_t available() const override { return static_cast<size_t>(end_ - cursor_); }

    bool contains(void *ptr) const override {
        auto *p = static_cast<std::byte *>(ptr);
        return p >= buffer_ && p < end_;
    }

    template <typename T, typename... Args> T *construct(Args &&...args) {
        void *ptr = alloc(sizeof(T), alignof(T));
        return ::new (ptr) T(std::forward<Args>(args)...);
    }

    size_t used() const { return static_cast<size_t>(cursor_ - buffer_); }
    size_t capacity() const { return capacity_; }

  private:
    size_t capacity_;
    std::byte *buffer_;
    std::byte *cursor_;
    std::byte *end_;
};

// =============================================================================
// GraphArena：为 GIR 的 frozen 产物提供独立的内存分配域。
//
// 职责：
//   - 持有一个 BumpAllocator，为 frozen Node、邻接数组、FrameMeta 等提供
//     连续内存，所有分配物在 arena 销毁时整块回收。
//   - 在 sealGraph / exportRoot 时按需创建（计算所需大小后一次性分配）。
//   - Graph 析构时 shared_ptr 自动销毁 arena，整块回收 BumpAllocator 的缓冲区。
// =============================================================================
class GraphArena {
  public:
    explicit GraphArena(size_t capacity) : allocator_(capacity) {}

    BumpAllocator &allocator() { return allocator_; }
    const BumpAllocator &allocator() const { return allocator_; }

    void *alloc(size_t size, size_t align = alignof(std::max_align_t)) {
        return allocator_.alloc(size, align);
    }

    template <typename T, typename... Args> T *construct(Args &&...args) {
        return allocator_.construct<T>(std::forward<Args>(args)...);
    }

    size_t used() const { return allocator_.used(); }
    size_t capacity() const { return allocator_.capacity(); }

  private:
    BumpAllocator allocator_;
};

using graph_arena_ptr_t = std::shared_ptr<GraphArena>;

} // namespace camel::compile::gir
