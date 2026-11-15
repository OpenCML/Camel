// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    /// Gives one reference back and says whether it was the last, so a caller that holds a value
    /// learns the count reached zero at that moment (design principles 10.5) instead of learning it
    /// from a later collection. `releaseReference` is the same call without the answer.
    bool releaseCountNow() noexcept;
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

    bool release() noexcept;

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

/// ---- The count a counted value is held by ------------------------------------------------
//
// Design principles 10.5: an immutable value is freed by counting alone, the count is the number of
// its holders, and the count comes from one last-use analysis on the execution plan that every back
// end follows. These two functions are the whole runtime surface for that count -- the author's
// ruling of 2026-11-15 is that the compiler emits the duplicate and the drop and the runtime
// provides nothing else (docs/handover/reviews/
// 2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md) -- and where they are
// called is the plan's business (camel::runtime::ExecPlan::refGiveBacksAt names the positions and
// the references).
//
// A `Ref` handle is an ordinary traced wrapper whose control block is the box, and the slot's type
// is what says a slot holds one (`TypeCode::Ref`), so a write path pays a comparison it already
// makes and not a cast per value. `retainCountNow` takes one reference for a new holder and answers
// false for a value that is not a counted block (every other foreign resource keeps its present
// lifetime: released by its wrapper's finalizer). `giveBackCountNow` gives one back and answers
// true when the count reached zero, so the caller is the one that knows the box was released at
// that moment -- which is the point of the count, and the reason the answer is returned rather than
// inferred from a collection later.
//
// Neither is called anywhere yet: the writes that will call them are one entry point shared by all
// four back ends (`camel::core::context::Frame::set` for slots, `Tuple::set` / `Struct::set` /
// `Array::set` for aggregates), so they are added here first and wired one back end at a time.
bool retainCountNow(Object *value) noexcept;
bool giveBackCountNow(Object *value) noexcept;

ForeignResourceStats foreignResourceStats();
/// Releases every external resource that is still held, newest first, and returns how many were
/// released. For the end of a run (design principles 3.9: before a program ends, however it
/// ends, its external resources are released): the collector releases a resource when it finds
/// its wrapper dead, which for the wrappers alive at the end would be never. The wrappers stay
/// valid objects; using one afterwards is the "disposed" error. Must not run while the program
/// is still running.
/// DESIGN-QUESTION(attempt-finalizers): a resource dropped inside a fault-isolation boundary is
/// released by the next collection that finds its wrapper dead, at the latest by this call.
size_t disposeAllForeignResources() noexcept;
void resetForeignResourceStatsForTests();
/// Releases the resources of the wrappers the collector found dead. The collector only
/// queues them (ForeignResourceObject::finalize runs inside the collection, with the allocator
/// locked); the allocator runs the queue at the end of the safepoint, after it has unlocked, on
/// the thread that reached the safepoint (mm/deferred.h; data blocks use the same
/// queue). A deleter therefore runs with no runtime lock held: it may take locks of its own
/// (the Python GIL) without ordering them against the allocator's.

void noteForeignRootedHandleCreated() noexcept;
void noteForeignRootedHandleDestroyed() noexcept;
void noteForeignPinnedHandleCreated() noexcept;
void noteForeignPinnedHandleDestroyed() noexcept;

} // namespace camel::core::rtdata
