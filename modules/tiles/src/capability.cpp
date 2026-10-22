/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// The device `--devices` selected. Explicit target configuration is a requirement, never a
// capability miss: only a selection that was not given may fall back to the host.
#include <exception>

#include "tiles/internal/capability.h"
#include "camel/execute/devices.h"
#ifdef CAMEL_TILES_CUDA
#include "tiles/internal/cuda_backend.h"
#endif
namespace camel::tiles {
namespace {
#ifdef CAMEL_TILES_CUDA
std::string cudaFailureText(cuda_detail::Api &api, CUresult result) {
    const char *name    = nullptr;
    const char *message = nullptr;
    if (api.cuGetErrorName(result, &name) != CUDA_SUCCESS || !name)
        name = "CUDA error";
    if (api.cuGetErrorString(result, &message) != CUDA_SUCCESS || !message)
        message = "unknown error";
    return std::string(name) + " (" + message + ")";
}
#endif
} // namespace
std::optional<int> configuredCudaOrdinal() {
    const auto &selection = camel::execute::selectedDevices();
    if (selection.given) {
        if (selection.cpu || selection.cuda.empty())
            return std::nullopt;
        return selection.cuda.front();
    }
    // No --devices: the run prefers device 0 and the host runs the program when there is none.
    return 0;
}

CudaDeviceAvailability cudaDeviceAvailability(const std::vector<int> &named) {
    CudaDeviceAvailability availability;
#ifdef CAMEL_TILES_CUDA
    try {
        cuda_detail::Api api;
        const CUresult initialized = api.cuInit(0);
        if (initialized == CUDA_ERROR_NO_DEVICE) {
            availability.reason  = "this machine has no CUDA device";
            availability.missing = named;
            return availability;
        }
        if (initialized != CUDA_SUCCESS) {
            availability.reason =
                "initializing CUDA failed: " + cudaFailureText(api, initialized);
            availability.missing = named;
            return availability;
        }
        int count              = 0;
        const CUresult counted = api.cuDeviceGetCount(&count);
        if (counted != CUDA_SUCCESS) {
            availability.reason = "counting CUDA devices failed: " + cudaFailureText(api, counted);
            availability.missing = named;
            return availability;
        }
        availability.count = count;
        for (const int ordinal : named) {
            CUdevice device = 0;
            if (api.cuDeviceGet(&device, ordinal) != CUDA_SUCCESS) {
                availability.missing.push_back(ordinal);
            }
        }
    } catch (const std::exception &e) {
        // No driver, no NVRTC, or a back end this build cannot load: no device can be used, so
        // every named ordinal is missing and the reason says why.
        availability.count   = 0;
        availability.reason  = e.what();
        availability.missing = named;
    }
#else
    availability.reason  = "this build of Camel has no CUDA support";
    availability.missing = named;
#endif
    return availability;
}
} // namespace camel::tiles