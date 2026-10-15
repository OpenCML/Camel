/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Own a stream and its staging/storage until all submitted work has drained.
#pragma once

#include <memory>
#include <vector>
#ifdef CAMEL_TILES_CUDA
#include "tiles/internal/cuda_backend.h"

namespace camel::tiles::cuda_detail {
class Resources {
    std::shared_ptr<Backend> backend;

  public:
    Api &api;
    CUstream stream = nullptr;
    std::vector<CUdeviceptr> buffers;
    std::vector<void *> hostBuffers;
    explicit Resources(std::shared_ptr<Backend> backend);
    ~Resources();
    Resources(const Resources &)            = delete;
    Resources &operator=(const Resources &) = delete;
    CUdeviceptr allocate(size_t size);
    void *allocateHost(size_t size);
};
} // namespace camel::tiles::cuda_detail
#endif
