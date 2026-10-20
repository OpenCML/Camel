/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Native acceptance for the backend-private CUDA memory pools: exact reuse,
// idle budgets, injected driver failures and lease lifetime. Every case uses the
// real driver; only the private backend's function table is ever instrumented.
#include "tiles/device.h"
#include "tiles/internal/cuda_backend.h"
#include "tiles/internal/cuda_resources.h"
#include "tiles/internal/submission.h"
#include "tiles/program.h"
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace camel::tiles;
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

void testBookkeeping() {
    auto backend = std::make_shared<Backend>();
    Current current(backend->api, backend->context);
    MemoryPool &pool = backend->pool(PoolKind::Device);
    const auto fresh = pool.stats();
    require(
        fresh.idleEntries == 0 && fresh.idleBytes == 0 && fresh.checkouts == 0 &&
            fresh.freed == 0,
        "a fresh pool was not empty");

    CUdeviceptr cachedAddress = 0;
    {
        auto lease = AllocationLease::device(backend, 1000);
        require(lease && lease->valid(), "device checkout returned no lease");
        require(lease->capacity() == 1024, "1000 bytes did not round up to its size class");
        require(!pool.holdsIdle(deviceAddress(lease->devicePointer())), "active block reusable");
        require(lease->backend() == backend, "a lease lost its owning backend");
        cachedAddress = lease->devicePointer();
    }
    const auto cached = pool.stats();
    require(
        cached.idleEntries == 1 && cached.idleBytes == 1024 && cached.cached == 1 &&
            cached.allocations == 1 && cached.freed == 0,
        "release did not cache the block exactly once");
    {
        auto lease = AllocationLease::device(backend, 1000);
        require(lease->devicePointer() == cachedAddress, "cached address not reused");
        require(lease->capacity() == 1024, "a reused block changed its size class");
    }
    require(pool.stats().reuses == 1, "the reuse counter did not advance exactly once");

    // Best fit: the smallest cached class that still holds the request wins.
    pool.trim();
    CUdeviceptr small = 0, medium = 0, large = 0;
    {
        auto a = AllocationLease::device(backend, 256);
        auto b = AllocationLease::device(backend, 1024);
        auto c = AllocationLease::device(backend, 4096);
        small  = a->devicePointer();
        medium = b->devicePointer();
        large  = c->devicePointer();
    }
    require(pool.stats().idleEntries == 3, "three released classes were not all cached");
    {
        auto lease = AllocationLease::device(backend, 700); // 768: 1024 is the best fit
        require(lease->devicePointer() == medium, "best fit skipped the smallest sufficient block");
    }
    {
        auto lease = AllocationLease::device(backend, 3000); // 3072: only 4096 fits
        require(lease->devicePointer() == large, "best fit skipped the only sufficient block");
    }
    {
        auto lease = AllocationLease::device(backend, 100); // 256
        require(lease->devicePointer() == small, "best fit missed the smallest class");
    }

    // Rounding boundaries and the zero-byte floor.
    require(
        MemoryPool::capacityFor(0) == 256 && MemoryPool::capacityFor(1) == 256 &&
            MemoryPool::capacityFor(256) == 256 && MemoryPool::capacityFor(257) == 512,
        "capacity rounding or the zero-byte floor changed");
    {
        auto lease = AllocationLease::device(backend, 0);
        require(
            lease->valid() && lease->capacity() == 256 && lease->devicePointer() != 0,
            "a zero-byte request did not receive a usable block");
    }
    for (size_t request : {std::numeric_limits<size_t>::max(),
                           std::numeric_limits<size_t>::max() - 254}) {
        bool overflow = false;
        try {
            (void)MemoryPool::capacityFor(request);
        } catch (const std::overflow_error &) {
            overflow = true;
        }
        require(overflow, "an overflowing request was not rejected");
    }

    // Idle entry cap: overflow blocks are released instead of cached.
    pool.trim();
    const auto beforeCap = pool.stats();
    std::vector<std::shared_ptr<AllocationLease>> blocks;
    for (size_t i = 0; i < MemoryPool::kMaxIdleEntries + 8; ++i)
        blocks.push_back(AllocationLease::device(backend, 256));
    blocks.clear();
    const auto capped = pool.stats();
    require(
        capped.idleEntries == MemoryPool::kMaxIdleEntries, "the idle entry cap was not enforced");
    require(
        capped.idleBytes == MemoryPool::kMaxIdleEntries * 256, "idle bytes disagree");
    require(capped.freed == beforeCap.freed + 8, "entry-cap overflow blocks were not released");

    const auto beforeTrim = pool.stats();
    pool.trim();
    const auto afterTrim = pool.stats();
    require(
        afterTrim.idleEntries == 0 && afterTrim.idleBytes == 0 &&
            afterTrim.trims == beforeTrim.trims + 1 &&
            afterTrim.freed == beforeTrim.freed + MemoryPool::kMaxIdleEntries,
        "trim did not free exactly the idle blocks and advance its counter");
    std::cout << "tiles pool: rounding, best fit, caps and trim counters passed\n";
}

void testBudgetLimits() {
    MemoryPolicy policy;
    policy.device.idleBytes  = 4096;
    policy.device.maxEntries = MemoryPool::kMaxIdleEntries;
    policy.host.idleBytes    = 2048;
    policy.host.maxEntries   = 4;
    auto backend = std::make_shared<Backend>(policy);
    Current current(backend->api, backend->context);
    MemoryPool &device = backend->pool(PoolKind::Device);
    MemoryPool &host   = backend->pool(PoolKind::Host);

    std::vector<std::shared_ptr<AllocationLease>> blocks;
    for (int i = 0; i < 8; ++i)
        blocks.push_back(AllocationLease::device(backend, 1024));
    blocks.clear();
    const auto deviceCapped = device.stats();
    require(
        deviceCapped.idleEntries == 4 && deviceCapped.idleBytes == 4096 &&
            deviceCapped.freed == 4,
        "the device idle byte budget was not enforced");

    blocks.push_back(AllocationLease::device(backend, 8192));
    const auto allocated = device.stats();
    blocks.clear();
    const auto afterOversize = device.stats();
    require(
        afterOversize.idleEntries == allocated.idleEntries &&
            afterOversize.idleBytes == allocated.idleBytes &&
            afterOversize.freed == allocated.freed + 1,
        "an oversize device block was cached or leaked");

    std::vector<std::shared_ptr<AllocationLease>> pinned;
    for (int i = 0; i < 6; ++i)
        pinned.push_back(AllocationLease::host(backend, 512));
    pinned.clear();
    const auto hostCapped = host.stats();
    require(
        hostCapped.idleEntries == 4 && hostCapped.idleBytes == 2048 && hostCapped.freed == 2,
        "the pinned idle byte budget was not enforced");
    std::cout << "tiles pool: device and pinned byte budgets passed\n";
}

void testUncachedPolicy() {
    auto backend = std::make_shared<Backend>(MemoryPolicy::uncached());
    Current current(backend->api, backend->context);
    {
        auto lease = AllocationLease::device(backend, 1024);
        require(lease->valid(), "an uncached device checkout failed");
    }
    {
        auto lease = AllocationLease::host(backend, 512);
        require(lease->valid(), "an uncached pinned checkout failed");
    }
    const auto device = backend->pool(PoolKind::Device).stats();
    const auto host   = backend->pool(PoolKind::Host).stats();
    require(
        device.cached == 0 && device.idleEntries == 0 && device.freed == 1 &&
            host.cached == 0 && host.idleEntries == 0 && host.freed == 1,
        "the uncached policy reused or failed to release a block");
    std::cout << "tiles pool: uncached policy releases every block immediately passed\n";
}

void testReuseWithData() {
    constexpr size_t count = 64;
    const size_t size      = count * sizeof(float);
    auto backend           = std::make_shared<Backend>();
    Current current(backend->api, backend->context);
    auto &api          = backend->api;
    MemoryPool &device = backend->pool(PoolKind::Device);
    MemoryPool &host   = backend->pool(PoolKind::Host);
    float first[count];
    float second[count];
    for (size_t i = 0; i < count; ++i) {
        first[i]  = float(i) * 0.5f;
        second[i] = -float(i) * 0.25f;
    }

    CUdeviceptr deviceBlock = 0;
    void *pinnedBlock       = nullptr;
    {
        Resources staging(backend);
        deviceBlock = staging.allocate(size);
        pinnedBlock = staging.allocateHost(size);
        std::memcpy(pinnedBlock, first, size);
        api.check(
            api.cuMemcpyHtoDAsync(deviceBlock, pinnedBlock, size, staging.stream),
            "upload reuse probe");
        api.check(api.cuStreamSynchronize(staging.stream), "complete reuse probe upload");
    }
    require(
        device.holdsIdle(deviceAddress(deviceBlock)) && host.holdsIdle(hostAddress(pinnedBlock)),
        "drained staging did not return its blocks to the pool");

    {
        Resources staging(backend);
        const CUdeviceptr reusedDevice = staging.allocate(size);
        void *const reusedPinned       = staging.allocateHost(size);
        require(
            reusedDevice == deviceBlock && reusedPinned == pinnedBlock,
            "a second drained staging did not reuse the cached addresses");
        std::memcpy(reusedPinned, second, size);
        api.check(
            api.cuMemcpyHtoDAsync(reusedDevice, reusedPinned, size, staging.stream),
            "upload reused probe");
        api.check(api.cuStreamSynchronize(staging.stream), "complete reused probe upload");
        float readback[count];
        api.check(api.cuMemcpyDtoH(readback, reusedDevice, size), "download reused probe");
        require(std::memcmp(readback, second, size) == 0, "reused block changed payload");
    }
    const auto deviceStats = device.stats();
    const auto hostStats   = host.stats();
    require(
        deviceStats.reuses == 1 && hostStats.reuses == 1,
        "reuse counters did not record the device and pinned recheckout");
    std::cout << "tiles pool: pinned/device address reuse with verified payloads passed\n";
}

void testSharedAliasesHoldBlock() {
    auto backend = std::make_shared<Backend>();
    Current current(backend->api, backend->context);
    MemoryPool &pool   = backend->pool(PoolKind::Device);
    CUdeviceptr handle = 0;
    std::shared_ptr<AllocationLease> alias;
    {
        auto resources = std::make_unique<Resources>(backend);
        handle         = resources->allocate(512);
        alias          = resources->shareDevice(handle);
        require(bool(alias) && alias->valid(), "shareDevice did not share the checked-out lease");
        require(!resources->shareDevice(handle + 1), "shareDevice invented an unknown lease");
    }
    require(!pool.holdsIdle(deviceAddress(handle)), "drained owner dropped an aliased block");
    const auto before = pool.stats();
    alias.reset();
    const auto after = pool.stats();
    require(pool.holdsIdle(deviceAddress(handle)), "the last share did not return the block");
    require(
        after.cached == before.cached + 1 && after.freed == before.freed,
        "the last share freed a block that the pool could reuse");
    std::cout << "tiles pool: shared aliases keep a drained block out of the idle list passed\n";
}

void testCrossThreadContext() {
    auto backend   = std::make_shared<Backend>();
    auto &api      = backend->api;
    auto resources = std::make_unique<Resources>(backend);
    (void)resources->allocate(1024);
    (void)resources->allocateHost(512);
    const auto query        = api.driver.symbol<decltype(&::cuCtxGetCurrent)>("cuCtxGetCurrent");
    const CUcontext context = backend->context;
    bool restored           = false;
    std::thread worker(
        [owned = std::move(resources), &api, context, query, &restored]() mutable {
            CUcontext entry = nullptr, exit = nullptr;
            const bool pushed = api.cuCtxPushCurrent(context) == CUDA_SUCCESS;
            const bool before = query(&entry) == CUDA_SUCCESS && entry == context;
            // Teardown on a foreign thread must drain and restore this caller's
            // current context instead of popping an unrelated one.
            owned.reset();
            const bool after = query(&exit) == CUDA_SUCCESS && exit == entry;
            if (pushed) {
                CUcontext popped = nullptr;
                api.cuCtxPopCurrent(&popped);
            }
            restored = pushed && before && after;
        });
    worker.join();
    require(restored, "cross-thread resource teardown did not restore the caller context");
    require(
        backend->pool(PoolKind::Device).stats().idleEntries == 1 &&
            backend->pool(PoolKind::Host).stats().idleEntries == 1,
        "cross-thread teardown did not return both blocks to their own pools");
    std::cout << "tiles pool: cross-thread teardown restored the caller context passed\n";
}

void testConcurrentCheckout() {
    constexpr int workers = 4;
    constexpr int rounds  = 24;
    auto backend          = std::make_shared<Backend>();
    std::barrier start(workers + 1);
    std::mutex mutex;
    std::unordered_set<uintptr_t> active;
    std::atomic<int> completed{0};
    std::exception_ptr failure;
    auto work = [&]() {
        start.arrive_and_wait();
        try {
            for (int round = 0; round < rounds; ++round) {
                const size_t bytes = round % 3 == 0 ? 256 : (round % 3 == 1 ? 512 : 1024);
                auto lease         = AllocationLease::device(backend, bytes);
                const uintptr_t address = deviceAddress(lease->devicePointer());
                {
                    std::lock_guard lock(mutex);
                    if (!active.insert(address).second)
                        throw std::runtime_error("two workers held the same active block");
                }
                std::this_thread::yield();
                {
                    std::lock_guard lock(mutex);
                    active.erase(address);
                }
                ++completed;
            }
        } catch (...) {
            std::lock_guard lock(mutex);
            if (!failure)
                failure = std::current_exception();
        }
    };
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i)
        threads.emplace_back(work);
    start.arrive_and_wait();
    for (auto &thread : threads)
        thread.join();
    if (failure)
        std::rethrow_exception(failure);
    require(completed == workers * rounds, "concurrent pool checkouts did not complete");
    {
        std::lock_guard lock(mutex);
        require(active.empty(), "the active-block set was not released");
    }
    require(
        backend->pool(PoolKind::Device).stats().reuses > 0,
        "concurrent checkouts never reused a block");
    std::cout << "tiles pool: concurrent workers never shared an active block passed\n";
}

void testBackendIsolationAndLifetime() {
    auto first  = std::make_shared<Backend>();
    auto second = std::make_shared<Backend>();
    require(
        first->context == second->context,
        "the isolation fixture did not share one primary context");
    const std::weak_ptr<Backend> weakFirst = first;
    CUdeviceptr handle                     = 0;
    {
        Current current(first->api, first->context);
        auto lease = AllocationLease::device(first, 1024);
        handle     = lease->devicePointer();
    }
    require(
        first->pool(PoolKind::Device).holdsIdle(deviceAddress(handle)),
        "the first backend did not cache its own block");
    require(
        !second->pool(PoolKind::Device).holdsIdle(deviceAddress(handle)),
        "a block leaked into another backend's idle list");
    const auto untouched = second->pool(PoolKind::Device).stats();
    require(
        untouched.checkouts == 0 && untouched.allocations == 0 && untouched.cached == 0,
        "the second backend's pool was touched");
    first.reset();
    require(weakFirst.expired(), "an idle pool kept its backend alive");

    auto third                             = std::make_shared<Backend>();
    const std::weak_ptr<Backend> weakThird = third;
    auto lease                             = AllocationLease::device(third, 256);
    third.reset();
    require(!weakThird.expired(), "an active lease did not retain its backend");
    lease.reset();
    require(weakThird.expired(), "the last lease did not release its backend");
    std::cout << "tiles pool: backend isolation and non-owning idle entries passed\n";
}

void testSharedBackendReuse() {
    auto backend     = sharedBackend();
    MemoryPool &pool = backend->pool(PoolKind::Device);
    const float values[4] = {1.0f, -2.0f, 3.5f, -4.25f};
    const auto payload    = std::as_bytes(std::span(values));

    // A pending consumer retains its borrowed input, and a duplicate output node
    // may share one whole value.
    auto sample             = uploadValue({4}, DType::F32, payload);
    const uintptr_t address = static_cast<uintptr_t>(sample->address);
    require(!pool.holdsIdle(address), "an active upload block was reusable");
    Program borrowed;
    borrowed.nodes   = {{.op = "input", .shape = {4}, .dtype = DType::F32, .resident = sample}};
    borrowed.outputs = {0, 0};
    generate(borrowed);
    auto consumer = submitProgram(std::make_shared<const Program>(std::move(borrowed)));
    sample.reset();
    require(!pool.holdsIdle(address), "a pending consumer released its borrowed input block");
    consumer.completion->wait();
    require(
        consumer.outputs.size() == 2 && consumer.outputs[0] == consumer.outputs[1],
        "duplicate identity outputs did not share one value");
    require(
        !pool.holdsIdle(address), "a completed consumer released a block its own output holds");
    const auto produced = downloadValue(*consumer.outputs.at(0));
    require(
        produced.size() == sizeof(values) &&
            std::memcmp(produced.data(), values, sizeof(values)) == 0,
        "the resident identity changed its payload");
    consumer.outputs.clear();
    consumer.completion.reset();
    require(pool.holdsIdle(address), "a released input block did not return to the pool");

    // Distinct nodes share storage and lease but never the whole value.
    auto shared = uploadValue({4}, DType::F32, payload);
    Program distinct;
    distinct.nodes = {
        {.op = "input", .shape = {4}, .dtype = DType::F32, .resident = shared},
        {.op = "input", .shape = {4}, .dtype = DType::F32, .resident = shared}};
    distinct.outputs = {0, 1};
    generate(distinct);
    auto aliased = submitProgram(std::make_shared<const Program>(std::move(distinct)));
    aliased.completion->wait();
    require(
        aliased.outputs.size() == 2 && aliased.outputs[0] != aliased.outputs[1] &&
            aliased.outputs[0]->address == aliased.outputs[1]->address &&
            aliased.outputs[0]->owner == aliased.outputs[1]->owner,
        "distinct resident outputs shared one value or lost their common storage");
    require(
        downloadValue(*aliased.outputs[0]) == downloadValue(*aliased.outputs[1]),
        "distinct resident outputs disagree about their payload");
    aliased.outputs.clear();
    aliased.completion.reset();

    // The rounded physical class is wider than the logical payload. A forged
    // shape inside the class but past the payload must still be rejected.
    auto base = uploadValue({4}, DType::F32, payload);
    require(
        MemoryPool::capacityFor(4 * sizeof(float)) == 256 &&
            bytes(Node{.shape = {60}, .dtype = DType::F32}) <= 256,
        "the forged-shape fixture no longer fits inside the rounded class");
    DeviceValue forged = *base;
    forged.shape       = {60};
    bool rejected      = false;
    try {
        (void)downloadValue(forged);
    } catch (const std::exception &error) {
        rejected = std::string(error.what()).find("metadata mismatch") != std::string::npos;
    }
    require(rejected, "a forged shape read past the logical byte bound");
    bool placementRejected = false;
    try {
        (void)placeOnDevice(std::make_shared<const DeviceValue>(forged));
    } catch (const std::exception &error) {
        placementRejected = std::string(error.what()).find("metadata mismatch") !=
                            std::string::npos;
    }
    require(placementRejected, "placeOnDevice accepted a forged shape past the logical byte bound");

    // Repeated end-to-end upload/run/download reuses pooled storage.
    const auto runsBefore = pool.stats();
    for (int i = 0; i < 3; ++i) {
        Program p;
        p.nodes = {
            {.op = "input", .shape = {4}, .data = {float(i), 1.0f, 2.0f, 3.0f}},
            {.op = "add", .shape = {4}, .inputs = {0, 0}}};
        p.outputs = {1};
        generate(p);
        const auto raw          = execute(std::make_shared<const Program>(std::move(p)));
        const float expected[4] = {2.0f * float(i), 2.0f, 4.0f, 6.0f};
        require(
            raw.size() == sizeof(expected) &&
                std::memcmp(raw.data(), expected, sizeof(expected)) == 0,
            "end-to-end execution changed its result bits");
    }
    const auto runsAfter = pool.stats();
    require(
        runsAfter.reuses > runsBefore.reuses,
        "repeated end-to-end execution did not reuse a pooled device block");
    std::cout << "tiles pool: shared-backend reuse, aliases and logical bounds passed\n";
}
} // namespace

// Defined by memory_pool_failures.cpp: failure injection stays in its own unit.
void runMemoryPoolFailureChecks();

void runMemoryPoolChecks() {
    testBookkeeping();
    testBudgetLimits();
    testUncachedPolicy();
    testReuseWithData();
    testSharedAliasesHoldBlock();
    testCrossThreadContext();
    testConcurrentCheckout();
    testBackendIsolationAndLifetime();
    runMemoryPoolFailureChecks();
    testSharedBackendReuse();
}
