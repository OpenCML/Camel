// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Own a stream and its staging/storage until all submitted work has drained.

#pragma once

#include <memory>
#include <vector>
#ifdef CAMEL_TILES_CUDA
#include "tiles/internal/cuda_backend.h"

namespace camel::tiles::cuda_detail {
// Completion events retain their context independently of submitted resources.
// The owner records exactly once, then keeps the event alive for every consumer.
class CompletionEvent {
    std::shared_ptr<Backend> backend;
    CUevent event = nullptr;

  public:
    explicit CompletionEvent(std::shared_ptr<Backend> backend);
    ~CompletionEvent();
    CompletionEvent(const CompletionEvent &)            = delete;
    CompletionEvent &operator=(const CompletionEvent &) = delete;
    void record(CUstream stream);
    void waitOn(CUstream stream) const;
    void wait() const;
    bool ready() const;
};

class Resources {
    std::shared_ptr<Backend> backend;

  public:
    Api &api;
    CUstream stream = nullptr;
    // Scratch, table, fault and staging leases. An exported output shares the
    // same lease instead of moving it, so a block returns to its pool only after
    // this owner has drained and every exported share has been dropped.
    std::vector<std::shared_ptr<AllocationLease>> leases;
    explicit Resources(std::shared_ptr<Backend> backend);
    ~Resources() noexcept;
    Resources(const Resources &)            = delete;
    Resources &operator=(const Resources &) = delete;
    CUdeviceptr allocate(size_t size);
    void *allocateHost(size_t size);
    // Share (never move) the device lease for `handle`, so this owner and the
    // caller both keep the block alive. Returns null when this owner holds no
    // such lease, so a caller can never take ownership twice.
    std::shared_ptr<AllocationLease> shareDevice(CUdeviceptr handle);
};
} // namespace camel::tiles::cuda_detail
#endif
