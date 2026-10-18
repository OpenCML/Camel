/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Context-independent lifetime management for pending CUDA execution resources.
#include "tiles/internal/cuda_resources.h"

#include <memory>
#ifdef CAMEL_TILES_CUDA
#include <algorithm>
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
Resources::~Resources() {
    // Destruction may run on a different thread with no current context. Never
    // throw from cleanup or try to release handles in an unrelated context.
    if (api.cuCtxPushCurrent(backend->context) != CUDA_SUCCESS)
        return;
    if (stream)
        api.cuStreamSynchronize(stream);
    for (auto ptr : buffers)
        if (ptr)
            api.cuMemFree(ptr);
    for (auto ptr : hostBuffers)
        api.cuMemFreeHost(ptr);
    if (stream)
        api.cuStreamDestroy(stream);
    CUcontext popped;
    api.cuCtxPopCurrent(&popped);
}
CUdeviceptr Resources::allocate(size_t size) {
    Current current(api, backend->context);
    CUdeviceptr ptr = 0;
    api.check(api.cuMemAlloc(&ptr, std::max(size, size_t(1))), "allocate device memory");
    try {
        buffers.push_back(ptr);
    } catch (...) {
        api.cuMemFree(ptr);
        throw;
    }
    return ptr;
}
void *Resources::allocateHost(size_t size) {
    Current current(api, backend->context);
    void *ptr = nullptr;
    api.check(
        api.cuMemHostAlloc(&ptr, std::max(size, size_t(1)), 0),
        "allocate pinned host memory");
    try {
        hostBuffers.push_back(ptr);
    } catch (...) {
        api.cuMemFreeHost(ptr);
        throw;
    }
    return ptr;
}
} // namespace camel::tiles::cuda_detail
#endif
