// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Internal CUDA loader, compiled-module cache and backend-private memory pools.
// Not a public module API.

#pragma once
#ifdef CAMEL_TILES_CUDA
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cuda.h>
#include <deque>
#include <memory>
#include <mutex>
#include <nvrtc.h>
#include <string>
#include <vector>

namespace camel::tiles::cuda_detail {
class Library {
    void *handle_   = nullptr;
    void *builtins_ = nullptr;
    void *findSymbol(const char *name);

  public:
    explicit Library(const std::vector<std::string> &paths);
    ~Library();
    Library(const Library &) = delete;
    template <class T> T symbol(const char *name) { return reinterpret_cast<T>(findSymbol(name)); }
};
struct Api {
    Library driver, rtc;
    Api();
    decltype(&::cuInit) cuInit;
    decltype(&::cuDriverGetVersion) cuDriverGetVersion;
    decltype(&::cuDeviceGet) cuDeviceGet;
    decltype(&::cuDeviceGetCount) cuDeviceGetCount;
    decltype(&::cuDeviceGetName) cuDeviceGetName;
    decltype(&::cuDeviceGetAttribute) cuDeviceGetAttribute;
    decltype(&::cuDevicePrimaryCtxRetain) cuDevicePrimaryCtxRetain;
    decltype(&::cuDevicePrimaryCtxRelease) cuDevicePrimaryCtxRelease;
    decltype(&::cuCtxPushCurrent) cuCtxPushCurrent;
    decltype(&::cuCtxPopCurrent) cuCtxPopCurrent;
    decltype(&::cuGetErrorName) cuGetErrorName;
    decltype(&::cuGetErrorString) cuGetErrorString;
    decltype(&::cuModuleLoadData) cuModuleLoadData;
    decltype(&::cuModuleUnload) cuModuleUnload;
    decltype(&::cuModuleGetFunction) cuModuleGetFunction;
    decltype(&::cuMemAlloc) cuMemAlloc;
    decltype(&::cuMemFree) cuMemFree;
    decltype(&::cuMemHostAlloc) cuMemHostAlloc;
    decltype(&::cuMemFreeHost) cuMemFreeHost;
    decltype(&::cuMemcpyHtoD) cuMemcpyHtoD;
    decltype(&::cuMemcpyHtoDAsync) cuMemcpyHtoDAsync;
    decltype(&::cuMemcpyDtoH) cuMemcpyDtoH;
    decltype(&::cuMemcpyDtoHAsync) cuMemcpyDtoHAsync;
    decltype(&::cuStreamCreate) cuStreamCreate;
    decltype(&::cuStreamSynchronize) cuStreamSynchronize;
    decltype(&::cuStreamDestroy) cuStreamDestroy;
    decltype(&::cuStreamWaitEvent) cuStreamWaitEvent;
    decltype(&::cuLaunchKernel) cuLaunchKernel;
    decltype(&::cuEventCreate) cuEventCreate;
    decltype(&::cuEventDestroy) cuEventDestroy;
    decltype(&::cuEventRecord) cuEventRecord;
    decltype(&::cuEventSynchronize) cuEventSynchronize;
    decltype(&::cuEventQuery) cuEventQuery;
    decltype(&::cuEventElapsedTime) cuEventElapsedTime;
    decltype(&::nvrtcCreateProgram) nvrtcCreateProgram;
    decltype(&::nvrtcVersion) nvrtcVersion;
    decltype(&::nvrtcCompileProgram) nvrtcCompileProgram;
    decltype(&::nvrtcDestroyProgram) nvrtcDestroyProgram;
    decltype(&::nvrtcGetPTXSize) nvrtcGetPTXSize;
    decltype(&::nvrtcGetPTX) nvrtcGetPTX;
    decltype(&::nvrtcGetProgramLogSize) nvrtcGetProgramLogSize;
    decltype(&::nvrtcGetProgramLog) nvrtcGetProgramLog;
    decltype(&::nvrtcGetErrorString) nvrtcGetErrorString;
    decltype(&::nvrtcGetNumSupportedArchs) nvrtcGetNumSupportedArchs;
    decltype(&::nvrtcGetSupportedArchs) nvrtcGetSupportedArchs;
    void check(CUresult result, const char *action);
    void check(nvrtcResult result, const char *action);
};
struct Current {
    Api &api;
    Current(Api &a, CUcontext context);
    ~Current();
};
struct Compiled {
    Api &api;
    CUcontext context;
    std::string source;
    std::vector<std::string> options;
    CUmodule module = nullptr;
    Compiled(
        Api &api, CUcontext context, const std::string &source,
        const std::vector<std::string> &options);
    ~Compiled();
    Compiled(const Compiled &) = delete;
};
class Backend;

// Which backend-private free list a block belongs to. Device and pinned-host
// blocks are never interchangeable, so a pool is also isolated by kind.
enum class PoolKind : uint8_t { Device, Host };

// Idle budget and slot count for one pool. A zero budget or zero slot count
// disables caching; live allocations keep their ordinary allocation failure
// behavior either way. This is a native constructor policy, not a Camel API.
struct PoolPolicy {
    size_t idleBytes  = 0;
    size_t maxEntries = 0;
};
struct MemoryPolicy {
    // Idle-only ceilings. They bound cached blocks, not concurrent live usage.
    PoolPolicy device{64u * 1024u * 1024u, 32};
    PoolPolicy host{16u * 1024u * 1024u, 32};
    static MemoryPolicy uncached();
};

// Native observability for tests. Never part of the graph, a public API or a
// cache key.
struct PoolStats {
    size_t idleEntries   = 0;
    size_t idleBytes     = 0;
    uint64_t checkouts   = 0;
    uint64_t reuses      = 0;
    uint64_t allocations = 0;
    uint64_t cached      = 0;
    uint64_t freed       = 0;
    uint64_t trims       = 0;
    uint64_t oomRetries  = 0;
    uint64_t abandoned   = 0;
    bool disabled        = false;
};

// A ready-only free list of raw blocks of one kind. An idle entry holds only a
// raw handle and its capacity, never a Backend reference: a backend's own pool
// must not keep that backend alive (Backend -> pool -> lease -> Backend).
// The mutex protects bookkeeping only; allocation, freeing and context
// manipulation always happen outside it.
class MemoryPool {
  public:
    static constexpr size_t kMaxIdleEntries = 32;
    struct Block {
        void *handle    = nullptr;
        size_t capacity = 0;
    };

    MemoryPool(Api &api, PoolKind kind, PoolPolicy policy) noexcept;
    ~MemoryPool() noexcept;
    MemoryPool(const MemoryPool &)            = delete;
    MemoryPool &operator=(const MemoryPool &) = delete;

    // Required before the first checkout. Device and pinned-host allocation and
    // release both run with this context active.
    void bindContext(CUcontext context) noexcept;
    // Rounded capacity for a request: never zero, always a size class, and
    // throws instead of overflowing.
    static size_t capacityFor(size_t bytes);
    // Take a cached block of at least `bytes`, or allocate (and on
    // CUDA_ERROR_OUT_OF_MEMORY reclaim this kind's idle blocks and retry once).
    // Throws once reuse has been disabled for this pool.
    Block checkout(size_t bytes);
    // Offer a ready block back for reuse; frees it outside the lock when it
    // cannot be cached and abandons it while reuse is disabled. Never throws and
    // never allocates.
    void release(Block block) noexcept;
    // Free every idle block. Called by the backend before it releases context.
    // Idle blocks are already drained, so this is safe after disableReuse.
    void trim() noexcept;
    // Permanently stop reuse after a drain or context failure. Outstanding
    // blocks are then abandoned by `release` instead of being cached or freed.
    void disableReuse() noexcept;
    PoolStats stats() const;
    // Test observability for "this exact block is not currently reusable".
    bool holdsIdle(uintptr_t handle) const noexcept;

  private:
    // Both run under `mutex_` and never touch the driver.
    bool absorbLocked(Block block) noexcept;
    Block takeIdleLocked(size_t wanted) noexcept;
    void freeBlock(Block block) noexcept;
    size_t drainIdle() noexcept;
    Block allocate(size_t wanted);
    CUresult allocateRaw(void **handle, size_t bytes) noexcept;

    Api &api_;
    PoolKind kind_;
    PoolPolicy policy_;
    CUcontext context_ = nullptr;
    mutable std::mutex mutex_;
    // Guarded by `mutex_` so a concurrent release either caches a block before
    // reuse is disabled or observes the disabled state and abandons it.
    bool disabled_ = false;
    std::array<Block, kMaxIdleEntries> idle_{};
    size_t idleCount_ = 0;
    size_t idleBytes_ = 0;
    std::atomic<uint64_t> checkouts_{0}, reuses_{0}, allocations_{0}, cached_{0}, freed_{0},
        trims_{0}, oomRetries_{0}, abandoned_{0};
};

// One shared block lease. Resources holds a lease for every block it checks out
// and an exported value holds a share of the same lease, so the block returns to
// its pool exactly when the last share drops. A live lease keeps its backend
// alive; an idle pool slot never does, which is why ownership lives here and not
// in the pool.
class AllocationLease {
  public:
    ~AllocationLease() noexcept;
    AllocationLease(const AllocationLease &)            = delete;
    AllocationLease &operator=(const AllocationLease &) = delete;

    static std::shared_ptr<AllocationLease>
    device(const std::shared_ptr<Backend> &backend, size_t bytes);
    static std::shared_ptr<AllocationLease>
    host(const std::shared_ptr<Backend> &backend, size_t bytes);

    bool valid() const noexcept { return backend_ && block_.handle; }
    PoolKind kind() const noexcept { return kind_; }
    CUdeviceptr devicePointer() const noexcept {
        return kind_ == PoolKind::Device ? reinterpret_cast<CUdeviceptr>(block_.handle) : 0;
    }
    void *hostPointer() const noexcept { return kind_ == PoolKind::Host ? block_.handle : nullptr; }
    // Physical capacity of the pooled block, rounded up to a size class.
    size_t capacity() const noexcept { return block_.capacity; }
    const std::shared_ptr<Backend> &backend() const noexcept { return backend_; }

  private:
    AllocationLease(
        std::shared_ptr<Backend> backend, PoolKind kind, MemoryPool::Block block) noexcept;
    static std::shared_ptr<AllocationLease>
    create(const std::shared_ptr<Backend> &backend, PoolKind kind, MemoryPool::Block block);

    std::shared_ptr<Backend> backend_;
    PoolKind kind_ = PoolKind::Device;
    MemoryPool::Block block_{};
};

// Destruction releases the module before its backend. Assignment is forbidden
// because memberwise assignment could release the old backend too early.
struct ModuleLease {
    std::shared_ptr<Backend> backend;
    std::shared_ptr<Compiled> compiled;
    ModuleLease(std::shared_ptr<Backend> backend, std::shared_ptr<Compiled> compiled);
    ModuleLease(const ModuleLease &)            = default;
    ModuleLease(ModuleLease &&)                 = default;
    ModuleLease &operator=(const ModuleLease &) = delete;
    ModuleLease &operator=(ModuleLease &&)      = delete;
};
class Backend : public std::enable_shared_from_this<Backend> {
  public:
    Api api;
    CUdevice device;
    CUcontext context = nullptr;
    std::string description, architecture;
    std::deque<std::shared_ptr<Compiled>> cache;
    std::mutex mutex;
    // `policy` is a native test/embedding knob; production uses the defaults.
    explicit Backend(MemoryPolicy policy = {});
    ~Backend();
    std::vector<std::string> compileOptions() const;
    bool cached(const std::string &source);
    ModuleLease module(const std::string &source);
    MemoryPool &pool(PoolKind kind) noexcept;
    // Stop reuse on both pools. Called before any owner drops a lease, so a
    // failed drain can never let a pending block be cached or freed.
    void disableReuse() noexcept;

  private:
    MemoryPool devicePool_;
    MemoryPool hostPool_;
};
std::shared_ptr<Backend> sharedBackend();
Backend &backend();
} // namespace camel::tiles::cuda_detail
#endif
