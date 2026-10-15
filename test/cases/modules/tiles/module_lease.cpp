/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Exercise a module after cache eviction and verify backend lifetime ordering.
#include "tiles/internal/cuda_backend.h"
#include "tiles/internal/cuda_resources.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace camel::tiles::cuda_detail;

namespace {
void testPrecision() {
    auto backend = std::make_shared<Backend>();
    auto &api = backend->api;
    Current current(api, backend->context);
    auto lease = backend->module(R"cuda(
extern "C" __global__ void precision(const float* x, unsigned* y) {
    y[0] = __float_as_uint(x[0] * x[1]);
    y[1] = __float_as_uint(x[0] + x[0]);
    y[2] = __float_as_uint(x[1] / x[2]);
    y[3] = __float_as_uint(sqrtf(x[3]));
    float product = x[4] * x[5];
    y[4] = __float_as_uint(product + x[6]);
    y[5] = __float_as_uint(fmaf(x[4], x[5], x[6]));
}
)cuda");
    Resources resources(backend);
    // Raw input bits prevent host rounding and device constant folding from
    // hiding FTZ or contraction. Expected results follow binary32 arithmetic.
    const unsigned input[] = {1U, 0x3f800000U, 0x41200000U, 0x40000000U,
                              0x3f800001U, 0x3f7ffffeU, 0xbf800000U};
    const unsigned expected[] = {1U, 2U, 0x3dcccccdU, 0x3fb504f3U, 0U, 0xa8800000U};
    unsigned result[6] = {};
    auto source = resources.allocate(sizeof(input));
    auto output = resources.allocate(sizeof(result));
    api.check(api.cuMemcpyHtoD(source, input, sizeof(input)), "upload precision probe");
    CUfunction kernel;
    api.check(api.cuModuleGetFunction(&kernel, lease.compiled->module, "precision"), "resolve precision probe");
    void *args[] = {&source, &output};
    api.check(api.cuLaunchKernel(kernel, 1, 1, 1, 1, 1, 1, 0, resources.stream, args, nullptr), "launch precision probe");
    api.check(api.cuStreamSynchronize(resources.stream), "complete precision probe");
    api.check(api.cuMemcpyDtoH(result, output, sizeof(result)), "read precision probe");
    for (size_t i = 0; i < 6; ++i)
        if (result[i] != expected[i])
            throw std::runtime_error("precision probe mismatch at index " + std::to_string(i));
    std::cout << "tiles precision: subnormals, rounding and explicit FMA bits passed\n";
}

decltype(&::cuMemFree) releaseDevice;
decltype(&::cuMemFreeHost) releaseHost;
decltype(&::cuStreamDestroy) releaseStream;
decltype(&::cuStreamSynchronize) drainStream;
int releasedDevice = 0, releasedHost = 0, releasedStream = 0;
bool drained = false;
CUresult CUDAAPI checkedDrain(CUstream stream) {
    auto result = drainStream(stream);
    drained = result == CUDA_SUCCESS;
    return result;
}
CUresult CUDAAPI checkedDevice(CUdeviceptr pointer) {
    auto result = releaseDevice(pointer);
    if (drained && result == CUDA_SUCCESS) ++releasedDevice;
    return result;
}
CUresult CUDAAPI checkedHost(void *pointer) {
    auto result = releaseHost(pointer);
    if (drained && result == CUDA_SUCCESS) ++releasedHost;
    return result;
}
CUresult CUDAAPI checkedStream(CUstream stream) {
    auto result = releaseStream(stream);
    if (drained && result == CUDA_SUCCESS) ++releasedStream;
    return result;
}
void testResourceThread() {
    auto backend = std::make_shared<Backend>();
    std::weak_ptr<Backend> weak = backend;
    auto resources = std::make_unique<Resources>(backend);
    constexpr size_t count = 4 * 1024 * 1024;
    auto pointer = resources->allocate(count);
    auto host = resources->allocateHost(count);
    std::memset(host, 17, count);
    auto &api = backend->api;
    {
        Current current(api, backend->context);
        api.check(api.cuMemcpyHtoDAsync(pointer, host, count, resources->stream), "queue resource test upload");
    }
    auto getCurrent = api.driver.symbol<decltype(&::cuCtxGetCurrent)>("cuCtxGetCurrent");
    releaseDevice = api.cuMemFree;
    releaseHost = api.cuMemFreeHost;
    releaseStream = api.cuStreamDestroy;
    drainStream = api.cuStreamSynchronize;
    api.cuMemFree = checkedDevice;
    api.cuMemFreeHost = checkedHost;
    api.cuStreamDestroy = checkedStream;
    api.cuStreamSynchronize = checkedDrain;
    backend.reset();
    if (weak.expired()) throw std::runtime_error("resources lost their backend");
    bool contextsRestored = false;
    std::thread worker([owned = std::move(resources), getCurrent, weak, &contextsRestored]() mutable {
        auto observer = weak.lock(); // Keep the dynamically loaded query function alive.
        CUcontext before = nullptr, after = nullptr;
        auto first = getCurrent(&before);
        owned.reset();
        auto last = getCurrent(&after);
        contextsRestored = first == CUDA_SUCCESS && last == CUDA_SUCCESS && before == after;
    });
    worker.join();
    if (!contextsRestored || !weak.expired() || releasedDevice != 1 || releasedHost != 1 || releasedStream != 1)
        throw std::runtime_error("cross-thread resource teardown did not drain, free and restore context");
    std::cout << "tiles resources: cross-thread drain, release and context restoration passed\n";
}
}

int main() {
    try {
        auto backend = std::make_shared<Backend>();
        std::weak_ptr<Backend> weakBackend = backend;
        std::unique_ptr<ModuleLease> lease;
        std::weak_ptr<Compiled> weakModule;
        {
            Current current(backend->api, backend->context);
            const std::string source = "extern \"C\" __global__ void retained(int* p){*p=42;}";
            lease = std::make_unique<ModuleLease>(backend->module(source));
            const std::vector<std::string> expectedOptions = {
                backend->architecture, "--std=c++14", "--fmad=false", "--prec-div=true",
                "--prec-sqrt=true", "--ftz=false"};
            if (lease->compiled->options != expectedOptions)
                throw std::runtime_error("missing explicit numerical compiler options");
            if (backend->module(source).compiled != lease->compiled)
                throw std::runtime_error("identical compilation did not reuse the cache");
            // A stale cache entry with a different numerical policy must miss.
            lease->compiled->options.back() = "--ftz=true";
            auto fresh = backend->module(source);
            if (fresh.compiled == lease->compiled || fresh.compiled->options != expectedOptions)
                throw std::runtime_error("cache reused an incompatible numerical policy");
            weakModule = lease->compiled;
            for (int i = 0; i < 34; ++i)
                (void)backend->module(source + "\n// variant " + std::to_string(i));
            if (backend->cache.size() != 32 || weakModule.expired())
                throw std::runtime_error("cache eviction destroyed an active lease");
            for (const auto &entry : backend->cache)
                if (entry == lease->compiled)
                    throw std::runtime_error("old module was not evicted from cache");
            auto &api = backend->api;
            CUdeviceptr memory;
            api.check(api.cuMemAlloc(&memory, sizeof(int)), "allocate test result");
            struct Guard {
                Api &api;
                CUdeviceptr pointer;
                ~Guard() { api.cuStreamSynchronize(nullptr); api.cuMemFree(pointer); }
            } guard{api, memory};
            CUfunction kernel;
            api.check(api.cuModuleGetFunction(&kernel, lease->compiled->module, "retained"), "resolve evicted kernel");
            void *args[] = {&memory};
            api.check(api.cuLaunchKernel(kernel, 1, 1, 1, 1, 1, 1, 0, nullptr, args, nullptr), "launch evicted kernel");
            api.check(api.cuStreamSynchronize(nullptr), "complete evicted kernel");
            int result = 0;
            api.check(api.cuMemcpyDtoH(&result, memory, sizeof(result)), "read evicted kernel");
            if (result != 42) throw std::runtime_error("evicted kernel result mismatch");
        }
        backend.reset();
        if (weakBackend.expired()) throw std::runtime_error("lease lost its backend");
        lease.reset();
        if (!weakModule.expired() || !weakBackend.expired())
            throw std::runtime_error("module/backend ownership cycle");
        std::cout << "tiles module lease: eviction, launch and teardown passed\n";
        testResourceThread();
        testPrecision();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
