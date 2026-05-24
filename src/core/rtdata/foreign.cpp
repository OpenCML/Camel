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

#include "camel/core/rtdata/foreign.h"

#include <new>
#include <ostream>
#include <utility>

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

uint64_t loadCounter(const std::atomic<uint64_t> &value) {
    return value.load(std::memory_order_acquire);
}

} // namespace

ForeignResourceControlBlock::ForeignResourceControlBlock(
    ForeignResourceDescriptor descriptor, void *resource)
    : descriptor_(std::move(descriptor)), resource_(resource),
      debugId_(counters().nextDebugId.fetch_add(1, std::memory_order_acq_rel)) {
    counters().createdControlBlocks.fetch_add(1, std::memory_order_acq_rel);
    counters().liveControlBlocks.fetch_add(1, std::memory_order_acq_rel);
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
    size_t oldCount = refCount_.fetch_sub(1, std::memory_order_acq_rel);
    if (oldCount != 1) {
        return;
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
    return new (mem) ForeignResourceObject(control);
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
    ForeignResourceControlBlock *control = control_;
    control_                             = nullptr;
    if (control) {
        control->releaseFromWrapperFinalizer();
    }
}

void ForeignResourceObject::updateRefs(
    const RefRelocator &relocate, const camel::core::type::Type *type) {
    if (control_) {
        control_->trace(relocate, type);
    }
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
