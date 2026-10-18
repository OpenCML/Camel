/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Native acceptance for the pool's failure paths: allocation-failure bounds and
// the disabled-reuse contract. Driver calls are injected only through a private
// backend's function table, and every injection is restored by construction.
#include "tiles/internal/cuda_backend.h"
#include "tiles/internal/cuda_resources.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace camel::tiles::cuda_detail;

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
// The pool's `holdsIdle` key is the raw block handle, so addresses round-trip
// through the same pointer representation the pool stores.
uintptr_t deviceAddress(CUdeviceptr pointer) { return static_cast<uintptr_t>(pointer); }
uintptr_t hostAddress(void *pointer) { return reinterpret_cast<uintptr_t>(pointer); }

// Injection applies only to a private backend's function table, and only on the
// thread that armed it. The original pointers are restored by construction RAII.
// `attempts*` counts every raw driver call the pool makes, so the retry bound is
// observed directly instead of being inferred from the pool's own counters.
struct AllocProbe {
    Api &api;
    decltype(Api::cuMemAlloc) allocDevice;
    decltype(Api::cuMemHostAlloc) allocHost;
    int failDevice = 0, failHost = 0;
    int attemptsDevice = 0, attemptsHost = 0;
    CUresult failure = CUDA_ERROR_OUT_OF_MEMORY;
    inline static thread_local AllocProbe *active = nullptr;
    explicit AllocProbe(Api &a) : api(a), allocDevice(a.cuMemAlloc), allocHost(a.cuMemHostAlloc) {
        active             = this;
        api.cuMemAlloc     = device;
        api.cuMemHostAlloc = host;
    }
    ~AllocProbe() {
        api.cuMemAlloc     = allocDevice;
        api.cuMemHostAlloc = allocHost;
        active             = nullptr;
    }
    static CUresult CUDAAPI device(CUdeviceptr *pointer, size_t bytes) {
        auto &probe = *active;
        ++probe.attemptsDevice;
        if (probe.failDevice > 0) {
            --probe.failDevice;
            return probe.failure;
        }
        return probe.allocDevice(pointer, bytes);
    }
    static CUresult CUDAAPI host(void **pointer, size_t bytes, unsigned flags) {
        auto &probe = *active;
        ++probe.attemptsHost;
        if (probe.failHost > 0) {
            --probe.failHost;
            return probe.failure;
        }
        return probe.allocHost(pointer, bytes, flags);
    }
};

void testAllocationFailureRecovery() {
    auto backend = std::make_shared<Backend>();
    auto &api    = backend->api;
    Current current(api, backend->context);
    MemoryPool &device = backend->pool(PoolKind::Device);
    MemoryPool &host   = backend->pool(PoolKind::Host);

    // One OOM then a successful retry: exactly two raw attempts, one reclaim.
    device.trim();
    {
        auto fixture = AllocationLease::device(backend, 256);
    }
    const auto deviceArmed = device.stats();
    require(deviceArmed.idleEntries == 1, "the device OOM fixture did not cache a block");
    {
        AllocProbe probe(api);
        probe.failDevice  = 1;
        auto lease        = AllocationLease::device(backend, 4096);
        const auto during = device.stats();
        require(lease && lease->capacity() == 4096, "the device OOM retry produced no block");
        require(probe.attemptsDevice == 2, "the device OOM path did not attempt exactly one retry");
        require(
            during.oomRetries == deviceArmed.oomRetries + 1,
            "the device OOM retry count is wrong");
        require(
            during.trims == deviceArmed.trims + 1,
            "the device OOM path did not trim the same-kind idle list");
        require(
            during.freed == deviceArmed.freed + 1,
            "the device OOM reclaim did not free the same-kind idle block");
        require(
            during.idleEntries == 0 && during.idleBytes == 0,
            "the device OOM path left reusable blocks behind");
        require(
            during.allocations == deviceArmed.allocations + 1,
            "the failed device attempt was counted as an allocation");
    }
    // Two consecutive OOM failures: the initial attempt plus exactly one retry,
    // then the driver error must propagate without a phantom cache or allocation.
    device.trim();
    {
        auto fixture = AllocationLease::device(backend, 256);
    }
    const auto deviceStopped = device.stats();
    {
        AllocProbe probe(api);
        probe.failDevice = 2;
        bool propagated  = false;
        try {
            (void)AllocationLease::device(backend, 4096);
        } catch (const std::exception &) {
            propagated = true;
        }
        require(propagated, "two consecutive device OOM failures did not propagate");
        require(
            probe.attemptsDevice == 2, "the device OOM path did not stop after exactly one retry");
    }
    const auto deviceFailed = device.stats();
    require(
        deviceFailed.oomRetries == deviceStopped.oomRetries + 1,
        "the failed device retry was not counted exactly once");
    require(
        deviceFailed.trims == deviceStopped.trims + 1,
        "the failed device OOM path did not reclaim the same-kind idle list exactly once");
    require(
        deviceFailed.freed == deviceStopped.freed + 1,
        "the failed device OOM path did not free the same-kind idle block");
    require(
        deviceFailed.allocations == deviceStopped.allocations,
        "a failed device OOM attempt was counted as an allocation");
    require(
        deviceFailed.cached == deviceStopped.cached &&
            deviceFailed.abandoned == deviceStopped.abandoned,
        "a failed device OOM attempt cached or abandoned a phantom block");
    require(
        deviceFailed.idleEntries == 0 && deviceFailed.idleBytes == 0,
        "a failed device OOM attempt left a cached phantom");
    // A non-OOM driver error is final: one attempt, no retry, nothing counted.
    device.trim();
    const auto deviceClean = device.stats();
    {
        AllocProbe probe(api);
        probe.failDevice = 1;
        probe.failure    = CUDA_ERROR_INVALID_VALUE;
        bool rejected    = false;
        try {
            (void)AllocationLease::device(backend, 4096);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "a non-OOM device failure was not reported");
        require(probe.attemptsDevice == 1, "a non-OOM device failure was retried");
    }
    const auto deviceRejected = device.stats();
    require(
        deviceRejected.oomRetries == deviceClean.oomRetries &&
            deviceRejected.allocations == deviceClean.allocations &&
            deviceRejected.trims == deviceClean.trims && deviceRejected.freed == deviceClean.freed,
        "a non-OOM device failure was retried, counted or released something");

    // The pinned pool repeats the same bound independently of the device pool.
    host.trim();
    {
        auto fixture = AllocationLease::host(backend, 256);
    }
    const auto hostArmed = host.stats();
    require(hostArmed.idleEntries == 1, "the pinned OOM fixture did not cache a block");
    {
        AllocProbe probe(api);
        probe.failHost    = 1;
        auto lease        = AllocationLease::host(backend, 4096);
        const auto during = host.stats();
        require(lease && lease->capacity() == 4096, "the pinned OOM retry produced no block");
        require(probe.attemptsHost == 2, "the pinned OOM path did not attempt exactly one retry");
        require(
            during.oomRetries == hostArmed.oomRetries + 1, "the pinned OOM retry count is wrong");
        require(
            during.trims == hostArmed.trims + 1,
            "the pinned OOM path did not trim the same-kind idle list");
        require(
            during.freed == hostArmed.freed + 1,
            "the pinned OOM reclaim did not free the same-kind idle block");
        require(
            during.idleEntries == 0 && during.idleBytes == 0,
            "the pinned OOM path left reusable blocks behind");
        require(
            during.allocations == hostArmed.allocations + 1,
            "the failed pinned attempt was counted as an allocation");
    }
    host.trim();
    {
        auto fixture = AllocationLease::host(backend, 256);
    }
    const auto hostStopped = host.stats();
    {
        AllocProbe probe(api);
        probe.failHost  = 2;
        bool propagated = false;
        try {
            (void)AllocationLease::host(backend, 4096);
        } catch (const std::exception &) {
            propagated = true;
        }
        require(propagated, "two consecutive pinned OOM failures did not propagate");
        require(
            probe.attemptsHost == 2, "the pinned OOM path did not stop after exactly one retry");
    }
    const auto hostFailed = host.stats();
    require(
        hostFailed.oomRetries == hostStopped.oomRetries + 1,
        "the failed pinned retry was not counted exactly once");
    require(
        hostFailed.trims == hostStopped.trims + 1,
        "the failed pinned OOM path did not reclaim the same-kind idle list exactly once");
    require(
        hostFailed.freed == hostStopped.freed + 1,
        "the failed pinned OOM path did not free the same-kind idle block");
    require(
        hostFailed.allocations == hostStopped.allocations,
        "a failed pinned OOM attempt was counted as an allocation");
    require(
        hostFailed.cached == hostStopped.cached && hostFailed.abandoned == hostStopped.abandoned,
        "a failed pinned OOM attempt cached or abandoned a phantom block");
    require(
        hostFailed.idleEntries == 0 && hostFailed.idleBytes == 0,
        "a failed pinned OOM attempt left a cached phantom");
    host.trim();
    const auto hostClean = host.stats();
    {
        AllocProbe probe(api);
        probe.failHost = 1;
        probe.failure  = CUDA_ERROR_INVALID_VALUE;
        bool rejected  = false;
        try {
            (void)AllocationLease::host(backend, 4096);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "a non-OOM pinned failure was not reported");
        require(probe.attemptsHost == 1, "a non-OOM pinned failure was retried");
    }
    const auto hostRejected = host.stats();
    require(
        hostRejected.oomRetries == hostClean.oomRetries &&
            hostRejected.allocations == hostClean.allocations &&
            hostRejected.trims == hostClean.trims && hostRejected.freed == hostClean.freed,
        "a non-OOM pinned failure was retried, counted or released something");
    std::cout << "tiles pool: OOM trim-and-retry-once and non-OOM rejection passed\n";
}

struct SyncProbe {
    Api &api;
    decltype(Api::cuStreamSynchronize) drain;
    bool fail = false;
    inline static thread_local SyncProbe *active = nullptr;
    explicit SyncProbe(Api &a) : api(a), drain(a.cuStreamSynchronize) {
        active                  = this;
        api.cuStreamSynchronize = synchronized;
    }
    ~SyncProbe() {
        api.cuStreamSynchronize = drain;
        active                  = nullptr;
    }
    static CUresult CUDAAPI synchronized(CUstream stream) {
        auto &probe = *active;
        return probe.fail ? CUDA_ERROR_INVALID_VALUE : probe.drain(stream);
    }
};

struct PushProbe {
    Api &api;
    decltype(Api::cuCtxPushCurrent) push;
    bool fail = false;
    inline static thread_local PushProbe *active = nullptr;
    explicit PushProbe(Api &a) : api(a), push(a.cuCtxPushCurrent) {
        active               = this;
        api.cuCtxPushCurrent = pushed;
    }
    ~PushProbe() {
        api.cuCtxPushCurrent = push;
        active               = nullptr;
    }
    static CUresult CUDAAPI pushed(CUcontext context) {
        auto &probe = *active;
        return probe.fail ? CUDA_ERROR_INVALID_VALUE : probe.push(context);
    }
};

enum class DisableCause { Drain, Context };

void testDisabledReuse(DisableCause cause) {
    auto backend = std::make_shared<Backend>();
    auto &api    = backend->api;
    Current current(api, backend->context);
    MemoryPool &device = backend->pool(PoolKind::Device);
    MemoryPool &host   = backend->pool(PoolKind::Host);

    CUdeviceptr exportedHandle = 0, inputHandle = 0;
    void *pinnedHandle = nullptr;
    CUstream stream    = nullptr;
    std::shared_ptr<AllocationLease> exportedShare, inputShare;
    {
        auto resources = std::make_unique<Resources>(backend);
        exportedHandle = resources->allocate(1024);
        inputHandle    = resources->allocate(2048);
        pinnedHandle   = resources->allocateHost(512);
        stream         = resources->stream;
        exportedShare  = resources->shareDevice(exportedHandle);
        inputShare     = resources->shareDevice(inputHandle);
        require(
            bool(exportedShare) && bool(inputShare), "the fixture did not share its leases");
        // Nothing is ever enqueued on this stream, so no fixture block has
        // pending device work and the abandoned handles can be freed by hand.
        if (cause == DisableCause::Drain) {
            SyncProbe probe(api);
            probe.fail = true;
            resources.reset();
            probe.fail = false;
        } else {
            PushProbe probe(api);
            probe.fail = true;
            resources.reset();
            probe.fail = false;
        }
    }
    require(
        device.stats().disabled && host.stats().disabled,
        "a failed drain or context activation did not disable both pools");
    require(
        !device.holdsIdle(deviceAddress(exportedHandle)) &&
            !device.holdsIdle(deviceAddress(inputHandle)) &&
            !host.holdsIdle(hostAddress(pinnedHandle)),
        "a disabled pool cached a block that may still be pending");
    const auto deviceArmed = device.stats();
    const auto hostArmed   = host.stats();
    require(
        deviceArmed.abandoned == 0 && hostArmed.abandoned == 1,
        "the disabled pools abandoned blocks before their shares dropped");
    exportedShare.reset();
    inputShare.reset();
    const auto deviceAbandoned = device.stats();
    const auto hostAbandoned   = host.stats();
    require(
        deviceAbandoned.abandoned == deviceArmed.abandoned + 2 &&
            deviceAbandoned.freed == deviceArmed.freed,
        "the disabled device pool cached or freed an outstanding block");
    require(
        hostAbandoned.freed == hostArmed.freed,
        "the disabled pinned pool freed an outstanding block");
    require(
        !device.holdsIdle(deviceAddress(exportedHandle)) &&
            !device.holdsIdle(deviceAddress(inputHandle)) &&
            !host.holdsIdle(hostAddress(pinnedHandle)),
        "a disabled pool cached an abandoned block");

    bool rejectedDevice = false, rejectedHost = false;
    try {
        (void)AllocationLease::device(backend, 256);
    } catch (const std::exception &) {
        rejectedDevice = true;
    }
    try {
        (void)AllocationLease::host(backend, 256);
    } catch (const std::exception &) {
        rejectedHost = true;
    }
    require(rejectedDevice && rejectedHost, "a disabled pool accepted a new checkout");

    // Only these test-owned, never-enqueued handles are released, and only after
    // injection is restored: no intentional leak and no driver call in the dark.
    if (cause == DisableCause::Context)
        api.cuStreamDestroy(stream);
    api.check(api.cuMemFree(exportedHandle), "release abandoned device fixture");
    api.check(api.cuMemFree(inputHandle), "release abandoned device fixture");
    api.check(api.cuMemFreeHost(pinnedHandle), "release abandoned pinned fixture");
    require(
        device.stats().freed == deviceAbandoned.freed,
        "a manual fixture release changed the pool's free counter");
    std::cout << "tiles pool: "
              << (cause == DisableCause::Drain ? "failed drain" : "failed context activation")
              << " disables reuse and abandons outstanding blocks passed\n";
}
} // namespace

void runMemoryPoolFailureChecks() {
    testAllocationFailureRecovery();
    testDisabledReuse(DisableCause::Drain);
    testDisabledReuse(DisableCause::Context);
}