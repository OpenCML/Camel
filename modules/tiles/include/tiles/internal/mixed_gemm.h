// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
