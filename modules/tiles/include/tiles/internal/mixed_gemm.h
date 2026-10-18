/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Submission-owned cuBLAS BF16 compute, with FP32 graph boundaries and state.
#pragma once
#ifdef CAMEL_TILES_CUDA
#include "tiles/internal/cuda_backend.h"
#include "tiles/program.h"
#include <memory>
#include <vector>

namespace camel::tiles::cuda_detail {
class MixedGemm {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    MixedGemm(std::shared_ptr<Backend> backend, CUstream stream);
    ~MixedGemm();
    MixedGemm(const MixedGemm &) = delete;
    void run(const Program &program, const Node &node, const std::vector<CUdeviceptr> &pointers);
};
} // namespace camel::tiles::cuda_detail
#endif
