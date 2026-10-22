// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/foreign.h"

#include "camel/core/mm/deferred.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <new>
#include <ostream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camel::core::rtdata {

namespace {

struct ForeignResourceCounters {
    std::atomic<uint64_t> createdControlBlocks{0};
    std::atomic<uint64_t> disposedResources{0};
    std::atomic<uint64_t> finalizedWrappers{0};
    std::atomic<uint64_t> releasedControlBlocks{0};
    std::atomic<uint64_t> liveControlBlocks{0};
    std::atomic<uint64_t> createdRootedHandles{0};
    std::atomic<uint64_t> createdPinnedHandles{0};
    std::atomic<uint64_t> activeRootedHandles{0};
    std::atomic<uint64_t> activePinnedHandles{0};
    std::atomic<uint64_t> nextDebugId{1};
};

ForeignResourceCounters &counters() {
    static ForeignResourceCounters value;
    return value;
}

// Every control block that exists, so that the resources still held when a run ends can be
// released (disposeAllForeignResources). Never destroyed: blocks are released by finalizers
// that may run while statics are torn down.
struct LiveControlBlocks {
    std::mutex mutex;
    std::unordered_set<ForeignResourceControlBlock *> blocks;
};

LiveControlBlocks &liveControlBlocks() {
    static auto *value = new LiveControlBlocks();
    return *value;
}

uint64_t loadCounter(const std::atomic<uint64_t> &value) {
    return value.load(std::memory_order_acquire);
}

// The collector's deferred-release queue (mm/deferred.h) calls this after the collection.
void releaseControlBlockAfterCollection(void *context) noexcept {
    static_cast<ForeignResourceControlBlock *>(context)->releaseFromWrapperFinalizer();
}

} // namespace

ForeignResourceControlBlock::ForeignResourceControlBlock(
    ForeignResourceDescriptor descriptor, void *resource)
    : descriptor_(std::move(descriptor)), resource_(resource),
      debugId_(counters().nextDebugId.fetch_add(1, std::memory_order_acq_rel)) {
    counters().createdControlBlocks.fetch_add(1, std::memory_order_acq_rel);
    counters().liveControlBlocks.fetch_add(1, std::memory_order_acq_rel);
    auto &live = liveControlBlocks();
    std::lock_guard<std::mutex> lock(live.mutex);
    live.blocks.insert(this);
}

void ForeignResourceControlBlock::retain() noexcept {
    refCount_.fetch_add(1, std::memory_order_acq_rel);
}

void ForeignResourceControlBlock::releaseReference() noexcept { release(); }

void ForeignResourceControlBlock::releaseFromWrapperFinalizer() noexcept {
    counters().finalizedWrappers.fetch_add(1, std::memory_order_acq_rel);
    release();
}

void ForeignResourceControlBlock::release() noexcept {
    {
        // Under the registry's lock, so that disposeAllForeignResources never retains a block
        // whose last reference is being dropped.
        auto &live = liveControlBlocks();
        std::lock_guard<std::mutex> lock(live.mutex);
        size_t oldCount = refCount_.fetch_sub(1, std::memory_order_acq_rel);
        if (oldCount != 1) {
            return;
        }
        live.blocks.erase(this);
    }

    dispose();
    counters().releasedControlBlocks.fetch_add(1, std::memory_order_acq_rel);
    counters().liveControlBlocks.fetch_sub(1, std::memory_order_acq_rel);
    delete this;
}

void ForeignResourceControlBlock::dispose() noexcept {
    bool expected = false;
    if (!disposed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    void *resource = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resource  = resource_;
        resource_ = nullptr;
    }

    if (resource && descriptor_.finalizable && descriptor_.deleter) {
        try {
            descriptor_.deleter(resource);
        } catch (...) {
            // Finalization must not escape a noexcept disposal path. Descriptors should report
            // cleanup failures before handing ownership to the control block.
        }
    }
    counters().disposedResources.fetch_add(1, std::memory_order_acq_rel);
}

void *ForeignResourceControlBlock::resource() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (disposed_.load(std::memory_order_acquire) || !resource_) {
        throw std::runtime_error("foreign resource has been disposed");
    }
    return resource_;
}

const void *ForeignResourceControlBlock::resource() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (disposed_.load(std::memory_order_acquire) || !resource_) {
        throw std::runtime_error("foreign resource has been disposed");
    }
    return resource_;
}

ForeignResourceControlBlock *ForeignResourceControlBlock::cloneOrRetain() {
    if (descriptor_.clone) {
        return new ForeignResourceControlBlock(descriptor_, descriptor_.clone(resource()));
    }

    retain();
    return this;
}

void ForeignResourceControlBlock::trace(
    const Object::RefRelocator &relocate, const camel::core::type::Type *typeInfo) {
    if (!descriptor_.containsCamelReferences || !descriptor_.trace || disposed()) {
        return;
    }
    descriptor_.trace(resource(), relocate, typeInfo);
}

void ForeignResourceControlBlock::onWrapperMoved() noexcept {
    if (!descriptor_.onMoved || disposed()) {
        return;
    }

    void *resource = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resource = resource_;
    }
    if (!resource) {
        return;
    }

    try {
        descriptor_.onMoved(resource);
    } catch (...) {
        // Movement callbacks are observational hooks and cannot abort GC relocation.
    }
}

ForeignResourceObject *ForeignResourceObject::create(
    camel::core::mm::IAllocator &allocator, ForeignResourceDescriptor descriptor, void *resource) {
    auto *control = new ForeignResourceControlBlock(std::move(descriptor), resource);
    return createWithControlBlock(allocator, control);
}

ForeignResourceObject *ForeignResourceObject::createWithControlBlock(
    camel::core::mm::IAllocator &allocator, ForeignResourceControlBlock *control) {
    void *mem = allocator.alloc(sizeof(ForeignResourceObject), alignof(ForeignResourceObject));
    if (!mem) {
        if (control) {
            control->releaseReference();
        }
        throw std::bad_alloc();
    }
    auto *wrapper = new (mem) ForeignResourceObject(control);
    // The collector does not look at dead young objects one by one; it must be told which
    // of them have something to release.
    allocator.noteFinalizable(wrapper);
    return wrapper;
}

void ForeignResourceObject::dispose() noexcept {
    if (control_) {
        control_->dispose();
    }
}

void *ForeignResourceObject::resource() {
    if (!control_) {
        throw std::runtime_error("foreign resource wrapper has no control block");
    }
    return control_->resource();
}

const void *ForeignResourceObject::resource() const {
    if (!control_) {
        throw std::runtime_error("foreign resource wrapper has no control block");
    }
    return control_->resource();
}

std::string_view ForeignResourceObject::kind() const noexcept {
    return control_ ? control_->kind() : std::string_view{};
}

uint64_t ForeignResourceObject::debugId() const noexcept {
    return control_ ? control_->debugId() : 0;
}

bool ForeignResourceObject::equals(
    const Object *other, const camel::core::type::Type *type, bool deep) const {
    (void)type;
    (void)deep;
    if (this == other) {
        return true;
    }
    auto *rhs = dynamic_cast<const ForeignResourceObject *>(other);
    return rhs && control_ == rhs->control_;
}

Object *ForeignResourceObject::clone(
    camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type, bool deep) const {
    (void)type;
    (void)deep;
    if (!control_) {
        return createWithControlBlock(allocator, nullptr);
    }
    return createWithControlBlock(allocator, control_->cloneOrRetain());
}

void ForeignResourceObject::print(std::ostream &os, const camel::core::type::Type *type) const {
    (void)type;
    os << "<foreign " << (kind().empty() ? "resource" : kind()) << "#" << debugId()
       << (disposed() ? " disposed" : " live") << ">";
}

void ForeignResourceObject::onMoved() {
    if (control_) {
        control_->onWrapperMoved();
    }
}

void ForeignResourceObject::finalize() noexcept {
    // Called by the collector, inside the collection and with the allocator locked. Running
    // the deleter here would run foreign code under that lock: a deleter that allocates
    // would deadlock on it, and the Python one takes the GIL, which every Python operator
    // holds while it allocates (the opposite order). So the wrapper only gives its reference
    // up; the allocator releases it once the collection is over and the lock is dropped.
    ForeignResourceControlBlock *control = control_;
    control_                             = nullptr;
    if (!control) {
        return;
    }
    mm::deferRelease(&releaseControlBlockAfterCollection, control);
}

void ForeignResourceObject::updateRefs(
    const RefRelocator &relocate, const camel::core::type::Type *type) {
    if (control_) {
        control_->trace(relocate, type);
    }
}

size_t disposeAllForeignResources() noexcept {
    std::vector<ForeignResourceControlBlock *> held;
    {
        auto &live = liveControlBlocks();
        std::lock_guard<std::mutex> lock(live.mutex);
        held.reserve(live.blocks.size());
        for (ForeignResourceControlBlock *block : live.blocks) {
            if (!block->disposed()) {
                block->retain();
                held.push_back(block);
            }
        }
    }
    // Newest first: a resource acquired later may depend on one acquired earlier.
    std::sort(held.begin(), held.end(), [](const auto *a, const auto *b) {
        return a->debugId() > b->debugId();
    });
    for (ForeignResourceControlBlock *block : held) {
        block->dispose();
        block->releaseReference();
    }
    return held.size();
}

ForeignResourceStats foreignResourceStats() {
    auto &c = counters();
    return ForeignResourceStats{
        .createdControlBlocks  = loadCounter(c.createdControlBlocks),
        .disposedResources     = loadCounter(c.disposedResources),
        .finalizedWrappers     = loadCounter(c.finalizedWrappers),
        .releasedControlBlocks = loadCounter(c.releasedControlBlocks),
        .liveControlBlocks     = loadCounter(c.liveControlBlocks),
        .createdRootedHandles  = loadCounter(c.createdRootedHandles),
        .createdPinnedHandles  = loadCounter(c.createdPinnedHandles),
        .activeRootedHandles   = loadCounter(c.activeRootedHandles),
        .activePinnedHandles   = loadCounter(c.activePinnedHandles),
    };
}

void resetForeignResourceStatsForTests() {
    auto &c = counters();
    c.createdControlBlocks.store(0, std::memory_order_release);
    c.disposedResources.store(0, std::memory_order_release);
    c.finalizedWrappers.store(0, std::memory_order_release);
    c.releasedControlBlocks.store(0, std::memory_order_release);
    c.liveControlBlocks.store(0, std::memory_order_release);
    c.createdRootedHandles.store(0, std::memory_order_release);
    c.createdPinnedHandles.store(0, std::memory_order_release);
    c.activeRootedHandles.store(0, std::memory_order_release);
    c.activePinnedHandles.store(0, std::memory_order_release);
    c.nextDebugId.store(1, std::memory_order_release);
}

void noteForeignRootedHandleCreated() noexcept {
    counters().createdRootedHandles.fetch_add(1, std::memory_order_acq_rel);
    counters().activeRootedHandles.fetch_add(1, std::memory_order_acq_rel);
}

void noteForeignRootedHandleDestroyed() noexcept {
    counters().activeRootedHandles.fetch_sub(1, std::memory_order_acq_rel);
}

void noteForeignPinnedHandleCreated() noexcept {
    counters().createdPinnedHandles.fetch_add(1, std::memory_order_acq_rel);
    counters().activePinnedHandles.fetch_add(1, std::memory_order_acq_rel);
}

void noteForeignPinnedHandleDestroyed() noexcept {
    counters().activePinnedHandles.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace camel::core::rtdata
