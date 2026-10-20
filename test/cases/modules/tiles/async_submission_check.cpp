/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Execute real asynchronous native chains without publishing pending Camel values.
#include "tiles/device.h"
#include "tiles/internal/cuda_backend.h"
#include "tiles/internal/cuda_submission.h"
#include "tiles/internal/submission.h"
#include "tiles/program.h"
#include <atomic>
#include <bit>
#include <climits>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace camel::tiles;
using namespace camel::tiles::cuda_detail;
void testAsyncFailuresAndConcurrency();
void runMemoryPoolChecks();

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::shared_ptr<const Program> generated(Program program) {
    generate(program);
    return std::make_shared<const Program>(std::move(program));
}

Program identity(std::shared_ptr<const DeviceValue> input) {
    Program p;
    p.nodes = {
        {.op       = "input",
         .shape    = input->shape,
         .dtype    = input->dtype,
         .resident = std::move(input)}};
    p.outputs = {0};
    return p;
}

Program twice(std::shared_ptr<const DeviceValue> input) {
    auto p = identity(std::move(input));
    p.nodes.push_back(
        {.op = "add", .shape = p.nodes[0].shape, .inputs = {0, 0}, .dtype = p.nodes[0].dtype});
    p.outputs = {1};
    return p;
}

std::vector<uint32_t> bits(const DeviceValue &value) {
    const auto bytes = downloadValue(value);
    require(bytes.size() % sizeof(uint32_t) == 0, "invalid result byte count");
    std::vector<uint32_t> result(bytes.size() / sizeof(uint32_t));
    if (!bytes.empty())
        std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

void testNativeChain() {
    Program p;
    p.nodes = {
        {.op = "input", .shape = {8}, .data = {0, -0.0f, 0.5f, -0.5f, 1, -1, 3, -7}},
        {.op = "add", .shape = {8}, .inputs = {0, 0}}};
    p.outputs                                   = {1};
    auto source                                 = generated(std::move(p));
    std::weak_ptr<const Program> sourceLifetime = source;
    auto producer                               = submitProgram(source);
    auto first = std::dynamic_pointer_cast<Submission>(producer.completion);
    require(bool(first), "CUDA submission completion missing");
    std::weak_ptr<DeviceCompletion> producerLifetime = first;
    const auto producerStream                        = first->stream();
    source.reset();
    auto consumer = submitProgram(generated(twice(producer.outputs.at(0))));
    auto second   = std::dynamic_pointer_cast<Submission>(consumer.completion);
    require(
        second && producerStream != second->stream(),
        "producer/consumer did not use distinct streams");
    producer.outputs.clear();
    producer.completion.reset();
    first.reset();
    second.reset();
    consumer.completion->wait();
    const std::vector<uint32_t> expected = {
        0,
        0x80000000U,
        0x40000000U,
        0xc0000000U,
        0x40800000U,
        0xc0800000U,
        0x41400000U,
        0xc1e00000U};
    require(bits(*consumer.outputs.at(0)) == expected, "cross-stream computation bits mismatch");
    require(sourceLifetime.expired(), "completed submission retained its program snapshot");
    require(producerLifetime.expired(), "completed consumer retained its producer submission");
    auto repeated    = identity(consumer.outputs.at(0));
    repeated.outputs = {0, 0};
    auto aliases     = submitProgram(generated(std::move(repeated)));
    consumer.outputs.clear();
    consumer.completion.reset();
    aliases.completion->wait();
    require(
        bits(*aliases.outputs.at(0)) == expected && bits(*aliases.outputs.at(1)) == expected,
        "zero-launch duplicate identity lost its producer");
    Program empty;
    empty.nodes      = {{.op = "input", .shape = {0, 3}}};
    empty.outputs    = {0};
    auto emptyResult = submitProgram(generated(std::move(empty)));
    emptyResult.completion->wait();
    require(downloadValue(*emptyResult.outputs.at(0)).empty(), "empty submission returned bytes");
    std::cout
        << "tiles async chain: distinct streams, exact bits, identities and empty outputs passed\n";
}

void expectFault(const std::shared_ptr<DeviceCompletion> &completion) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool failed = false;
        try {
            completion->wait();
        } catch (const std::exception &error) {
            failed = std::string(error.what()).find("division by zero at Tiles node") !=
                     std::string::npos;
        }
        require(failed, "ancestor scalar fault was lost or changed on repeat observation");
    }
    bool failed = false;
    try {
        (void)completion->ready();
    } catch (const std::exception &error) {
        failed =
            std::string(error.what()).find("division by zero at Tiles node") != std::string::npos;
    }
    require(failed, "ready did not preserve the cached scalar fault");
}

void testFaultChain() {
    Program p;
    p.nodes = {
        {.op = "input", .scalarBits = std::bit_cast<uint64_t>(1.0), .dtype = DType::F64},
        {.op = "input", .scalarBits = std::bit_cast<uint64_t>(-0.0), .dtype = DType::F64},
        {.op = "scalar_div", .inputs = {0, 1}, .dtype = DType::F64}};
    p.outputs     = {2};
    auto producer = submitProgram(generated(std::move(p)));
    auto middle   = submitProgram(generated(identity(producer.outputs.at(0))));
    auto consumer = submitProgram(generated(identity(middle.outputs.at(0))));
    producer.outputs.clear();
    producer.completion.reset();
    middle.outputs.clear();
    middle.completion.reset();
    expectFault(consumer.completion);
    bool failed = false;
    try {
        (void)downloadValue(*consumer.outputs.at(0));
    } catch (const std::exception &error) {
        failed =
            std::string(error.what()).find("division by zero at Tiles node") != std::string::npos;
    }
    require(failed, "identity output download discarded its submission fault");
    std::cout << "tiles async faults: two identity consumers preserve ancestor failures passed\n";
}

// Function-pointer probes apply only to this test's private Backend. Each thread
// explicitly binds its probe; no process/shared-backend API is changed.
struct CleanupProbe {
    Api &api;
    decltype(Api::cuMemFree) freeDevice;
    decltype(Api::cuMemFreeHost) freeHost;
    decltype(Api::cuStreamDestroy) destroyStream;
    decltype(Api::cuEventDestroy) destroyEvent;
    std::atomic<int> devices{0}, hosts{0}, streams{0}, events{0};
    inline static thread_local CleanupProbe *active = nullptr;
    explicit CleanupProbe(Api &a)
        : api(a), freeDevice(a.cuMemFree), freeHost(a.cuMemFreeHost),
          destroyStream(a.cuStreamDestroy), destroyEvent(a.cuEventDestroy) {
        active              = this;
        api.cuMemFree       = device;
        api.cuMemFreeHost   = host;
        api.cuStreamDestroy = stream;
        api.cuEventDestroy  = event;
    }
    ~CleanupProbe() {
        api.cuMemFree       = freeDevice;
        api.cuMemFreeHost   = freeHost;
        api.cuStreamDestroy = destroyStream;
        api.cuEventDestroy  = destroyEvent;
        active              = nullptr;
    }
    static CUresult CUDAAPI device(CUdeviceptr pointer) {
        auto &p     = *active;
        auto result = p.freeDevice(pointer);
        if (result == CUDA_SUCCESS)
            ++p.devices;
        return result;
    }
    static CUresult CUDAAPI host(void *pointer) {
        auto &p     = *active;
        auto result = p.freeHost(pointer);
        if (result == CUDA_SUCCESS)
            ++p.hosts;
        return result;
    }
    static CUresult CUDAAPI stream(CUstream handle) {
        auto &p     = *active;
        auto result = p.destroyStream(handle);
        if (result == CUDA_SUCCESS)
            ++p.streams;
        return result;
    }
    static CUresult CUDAAPI event(CUevent handle) {
        auto &p     = *active;
        auto result = p.destroyEvent(handle);
        if (result == CUDA_SUCCESS)
            ++p.events;
        return result;
    }
};

void testOwnershipAndFaultWithoutOutputs() {
    // Caching is disabled for this private backend so the exact native cleanup
    // counts below do not depend on pool budgets or trim timing. Pooled reuse is
    // covered separately by memory_pool_check.cpp.
    auto backend = std::make_shared<Backend>(MemoryPolicy::uncached());
    auto &api    = backend->api;
    CleanupProbe cleanup(api);
    ModuleLease module = [&] {
        Current current(api, backend->context);
        return backend->module("extern \"C\" __global__ void fault_probe(unsigned* p){*p=17;}");
    }();
    std::weak_ptr<Compiled> moduleLifetime       = module.compiled;
    auto program                                 = std::make_shared<const Program>();
    std::weak_ptr<const Program> programLifetime = program;
    auto marker                                  = std::make_shared<int>(42);
    std::weak_ptr<int> ownerLifetime             = marker;
    auto resources                               = std::make_unique<Resources>(backend);
    auto fault                                   = resources->allocate(sizeof(unsigned));
    auto initial = static_cast<unsigned *>(resources->allocateHost(sizeof(unsigned)));
    *initial     = UINT_MAX;
    {
        Current current(api, backend->context);
        api.check(
            api.cuMemcpyHtoDAsync(fault, initial, sizeof(unsigned), resources->stream),
            "initialize pending scalar fault");
        CUfunction kernel;
        api.check(
            api.cuModuleGetFunction(&kernel, module.compiled->module, "fault_probe"),
            "resolve pending fault probe");
        void *args[] = {&fault};
        api.check(
            api.cuLaunchKernel(kernel, 1, 1, 1, 1, 1, 1, 0, resources->stream, args, nullptr),
            "enqueue pending fault probe");
    }
    // Keep a lease for the two empty consumers; none has an arithmetic launch.
    auto consumerModule = module;
    auto producer =
        std::make_shared<Submission>(backend, std::move(resources), program, std::move(module));
    producer->keepAlive(marker);
    producer->finish(fault);
    auto middle = std::make_shared<Submission>(
        backend,
        std::make_unique<Resources>(backend),
        program,
        consumerModule);
    middle->depend(producer);
    middle->finish();
    auto consumer = std::make_shared<Submission>(
        backend,
        std::make_unique<Resources>(backend),
        program,
        std::move(consumerModule));
    consumer->depend(middle);
    consumer->finish();
    // Evict the actual compiled module while its recorded work remains owned.
    backend->cache.clear();
    marker.reset();
    program.reset();
    producer.reset();
    middle.reset();
    auto getCurrent = api.driver.symbol<decltype(&::cuCtxGetCurrent)>("cuCtxGetCurrent");
    bool restored   = false;
    std::exception_ptr failure;
    std::thread worker(
        [owned = std::move(consumer), &cleanup, getCurrent, &restored, &failure]() mutable {
            CleanupProbe::active = &cleanup;
            try {
                CUcontext before = nullptr, after = nullptr;
                require(getCurrent(&before) == CUDA_SUCCESS, "query worker CUDA context");
                expectFault(owned);
                owned.reset();
                require(getCurrent(&after) == CUDA_SUCCESS, "query restored worker CUDA context");
                restored = before == after;
            } catch (...) {
                failure = std::current_exception();
            }
            owned.reset();
            CleanupProbe::active = nullptr;
        });
    worker.join();
    if (failure)
        std::rethrow_exception(failure);
    require(restored, "submission completion did not restore the worker context");
    require(
        ownerLifetime.expired() && programLifetime.expired() && moduleLifetime.expired(),
        "completed dependency retained owners, program or evicted module");
    require(
        cleanup.devices == 1 && cleanup.hosts == 2 && cleanup.streams == 3 && cleanup.events == 3,
        "submission chain failed to release its device, pinned, stream or event resources");
    std::cout << "tiles async ownership: empty consumers, eviction, early drop and cross-thread "
                 "cleanup passed\n";
}
} // namespace

int main() {
    try {
        runMemoryPoolChecks();
        testNativeChain();
        testFaultChain();
        testOwnershipAndFaultWithoutOutputs();
        testAsyncFailuresAndConcurrency();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
