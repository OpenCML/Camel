// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Context-independent lifetime management for pending CUDA execution resources.

#include "tiles/internal/cuda_resources.h"

#include <memory>
#ifdef CAMEL_TILES_CUDA
#include <utility>

namespace camel::tiles::cuda_detail {
CompletionEvent::CompletionEvent(std::shared_ptr<Backend> owner) : backend(std::move(owner)) {
    Current current(backend->api, backend->context);
    backend->api.check(
        backend->api.cuEventCreate(&event, CU_EVENT_DISABLE_TIMING),
        "create completion event");
}
CompletionEvent::~CompletionEvent() {
    if (event && backend->api.cuCtxPushCurrent(backend->context) == CUDA_SUCCESS) {
        backend->api.cuEventDestroy(event);
        CUcontext popped;
        backend->api.cuCtxPopCurrent(&popped);
    }
}
void CompletionEvent::record(CUstream stream) {
    Current current(backend->api, backend->context);
    backend->api.check(backend->api.cuEventRecord(event, stream), "record completion event");
}
void CompletionEvent::waitOn(CUstream stream) const {
    Current current(backend->api, backend->context);
    backend->api.check(
        backend->api.cuStreamWaitEvent(stream, event, 0),
        "depend on completion event");
}
void CompletionEvent::wait() const {
    Current current(backend->api, backend->context);
    backend->api.check(backend->api.cuEventSynchronize(event), "wait for completion event");
}
bool CompletionEvent::ready() const {
    Current current(backend->api, backend->context);
    const auto result = backend->api.cuEventQuery(event);
    if (result == CUDA_ERROR_NOT_READY)
        return false;
    backend->api.check(result, "query completion event");
    return true;
}

Resources::Resources(std::shared_ptr<Backend> owner)
    : backend(std::move(owner)), api(backend->api) {
    Current current(api, backend->context);
    api.check(api.cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING), "create stream");
}
Resources::~Resources() noexcept {
    // Destruction may run on a different thread with no current context. Never
    // throw from cleanup or release handles in an unrelated context.
    const bool active = api.cuCtxPushCurrent(backend->context) == CUDA_SUCCESS;
    bool drained      = !stream;
    if (active && stream) {
        drained = api.cuStreamSynchronize(stream) == CUDA_SUCCESS;
        api.cuStreamDestroy(stream);
    }
    // A failed context activation or drain means kernel or DMA work may still be
    // pending. Reuse must stop for the whole backend before any owner drops a
    // share, so every outstanding block is abandoned by its pool rather than
    // cached or freed underneath the driver. The leases themselves are shared,
    // so exported values and borrowed inputs are covered by the same decision.
    if (!active || !drained)
        backend->disableReuse();
    leases.clear();
    if (active) {
        CUcontext popped = nullptr;
        api.cuCtxPopCurrent(&popped);
    }
}
CUdeviceptr Resources::allocate(size_t size) {
    auto lease                = AllocationLease::device(backend, size);
    const CUdeviceptr pointer = lease->devicePointer();
    // A throwing push_back destroys the shared lease, which returns the block,
    // so a failed owner-vector growth cannot leak it.
    leases.push_back(std::move(lease));
    return pointer;
}
void *Resources::allocateHost(size_t size) {
    auto lease    = AllocationLease::host(backend, size);
    void *pointer = lease->hostPointer();
    leases.push_back(std::move(lease));
    return pointer;
}
std::shared_ptr<AllocationLease> Resources::shareDevice(CUdeviceptr handle) {
    for (auto &lease : leases)
        if (lease && lease->kind() == PoolKind::Device && lease->devicePointer() == handle)
            return lease;
    return {};
}
} // namespace camel::tiles::cuda_detail
#endif
