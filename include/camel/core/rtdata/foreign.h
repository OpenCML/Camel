/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "camel/core/mm/alloc/allocator.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// Foreign resources are represented by ordinary GC-managed wrapper objects. The wrapper
// participates in tracing and relocation; the native resource behind the control block follows
// explicit dispose/finalize and reference-counted ownership rules.

namespace camel::core::rtdata {

struct ForeignResourceStats {
    uint64_t createdControlBlocks  = 0;
    uint64_t disposedResources     = 0;
    uint64_t finalizedWrappers     = 0;
    uint64_t releasedControlBlocks = 0;
    uint64_t liveControlBlocks     = 0;
    uint64_t createdRootedHandles  = 0;
    uint64_t createdPinnedHandles  = 0;
    uint64_t activeRootedHandles   = 0;
    uint64_t activePinnedHandles   = 0;
};

struct ForeignResourceDescriptor {
    using Deleter = std::function<void(void *)>;
    using Cloner  = std::function<void *(void *)>;
    using Tracer =
        std::function<void(void *, const Object::RefRelocator &, const camel::core::type::Type *)>;
    using MoveObserver = std::function<void(void *)>;

    std::string kind;
    Deleter deleter;
    Cloner clone;
    Tracer trace;
    MoveObserver onMoved;
    bool containsCamelReferences = false;
    bool finalizable             = true;
    bool movable                 = true;
    bool pinned                  = false;
};

class ForeignResourceControlBlock final {
  public:
    ForeignResourceControlBlock(ForeignResourceDescriptor descriptor, void *resource);

    ForeignResourceControlBlock(const ForeignResourceControlBlock &)            = delete;
    ForeignResourceControlBlock &operator=(const ForeignResourceControlBlock &) = delete;

    void retain() noexcept;
    void releaseReference() noexcept;
    void releaseFromWrapperFinalizer() noexcept;
    void dispose() noexcept;

    void *resource();
    const void *resource() const;
    bool disposed() const noexcept { return disposed_.load(std::memory_order_acquire); }
    uint64_t debugId() const noexcept { return debugId_; }
    size_t refCount() const noexcept { return refCount_.load(std::memory_order_acquire); }
    std::string_view kind() const noexcept { return descriptor_.kind; }
    const ForeignResourceDescriptor &descriptor() const noexcept { return descriptor_; }

    ForeignResourceControlBlock *cloneOrRetain();

    void trace(const Object::RefRelocator &relocate, const camel::core::type::Type *typeInfo);
    void onWrapperMoved() noexcept;

  private:
    ~ForeignResourceControlBlock() = default;

    void release() noexcept;

    ForeignResourceDescriptor descriptor_;
    void *resource_   = nullptr;
    uint64_t debugId_ = 0;
    std::atomic<size_t> refCount_{1};
    std::atomic<bool> disposed_{false};
    mutable std::mutex mutex_;
};

class ForeignResourceObject final : public Object {
  public:
    ForeignResourceObject(const ForeignResourceObject &)            = delete;
    ForeignResourceObject &operator=(const ForeignResourceObject &) = delete;

    static ForeignResourceObject *create(
        camel::core::mm::IAllocator &allocator, ForeignResourceDescriptor descriptor,
        void *resource);

    static ForeignResourceObject *createWithControlBlock(
        camel::core::mm::IAllocator &allocator, ForeignResourceControlBlock *control);

    ForeignResourceControlBlock *control() noexcept { return control_; }
    const ForeignResourceControlBlock *control() const noexcept { return control_; }

    bool disposed() const noexcept { return !control_ || control_->disposed(); }
    void dispose() noexcept;
    void *resource();
    const void *resource() const;
    std::string_view kind() const noexcept;
    uint64_t debugId() const noexcept;

    template <typename T> T *resourceAs() { return static_cast<T *>(resource()); }
    template <typename T> const T *resourceAs() const { return static_cast<const T *>(resource()); }

    bool equals(const Object *other, const camel::core::type::Type *type, bool deep) const override;
    Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override;
    void finalize() noexcept override;
    void updateRefs(const RefRelocator &relocate, const camel::core::type::Type *type) override;

  private:
    explicit ForeignResourceObject(ForeignResourceControlBlock *control) : control_(control) {}

    ForeignResourceControlBlock *control_ = nullptr;
};

ForeignResourceStats foreignResourceStats();
void resetForeignResourceStatsForTests();
void noteForeignRootedHandleCreated() noexcept;
void noteForeignRootedHandleDestroyed() noexcept;
void noteForeignPinnedHandleCreated() noexcept;
void noteForeignPinnedHandleDestroyed() noexcept;

} // namespace camel::core::rtdata
