/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Exercise a module after cache eviction and verify backend lifetime ordering.
#include "tiles/internal/cuda_backend.h"
#include <iostream>
#include <stdexcept>
using namespace camel::tiles::cuda_detail;

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
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
