// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Native completion and ownership of one CUDA submission, independent of Camel's heap.

#pragma once

#include <memory>
#include <vector>
#ifdef CAMEL_TILES_CUDA
#include "tiles/device.h"
#include "tiles/internal/cuda_resources.h"
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>

namespace camel::tiles::cuda_detail {
class Submission final : public DeviceCompletion {
    enum class State { Open, Recorded, Completing, Complete };
    std::shared_ptr<Backend> backend_;
    // Reverse destruction must drain pending resources before module/input
    // owners, including when another member's constructor throws.
    mutable std::shared_ptr<const Program> program_;
    mutable std::optional<ModuleLease> module_;
    mutable std::unique_ptr<Resources> resources_;
    std::unique_ptr<CompletionEvent> event_;
    const CUstream stream_;
    mutable std::mutex mutex_;
    mutable std::condition_variable changed_;
    mutable std::vector<std::shared_ptr<DeviceCompletion>> dependencies_;
    mutable std::vector<std::shared_ptr<void>> owners_;
    mutable State state_ = State::Open;
    bool recorded_       = false;
    unsigned *faultHost_ = nullptr;
    mutable std::exception_ptr failure_;

    // Only one caller finalizes. No submission mutex is held while visiting
    // ancestors, draining a stream, or releasing owners with their own cleanup.
    bool settle(bool blocking) const;
    void release() const noexcept;

  public:
    Submission(
        std::shared_ptr<Backend> backend, std::unique_ptr<Resources> resources,
        std::shared_ptr<const Program> program, ModuleLease module);
    ~Submission() override;
    Submission(const Submission &)            = delete;
    Submission &operator=(const Submission &) = delete;

    // Construction is single-writer. All dependency producers must already have
    // recorded their final event; a finished submission can never gain an edge.
    Resources &resources();
    // Identity for diagnostics only. Completion releases the actual stream;
    // this historical handle must never be used to enqueue additional work.
    CUstream stream() const noexcept;
    // A completion edge retains events and faults, not producer output storage.
    // Inputs must be owned by program.resident or explicitly retained with
    // keepAlive; a raw native caller must not rely on the producer's staging.
    void depend(std::shared_ptr<DeviceCompletion> dependency);
    void keepAlive(std::shared_ptr<void> owner);
    void finish(CUdeviceptr fault = 0);

    // Concurrent observations are supported. ready never waits for an ancestor
    // being finalized by another thread; both methods rethrow cached faults.
    void wait() const override;
    bool ready() const override;
};
} // namespace camel::tiles::cuda_detail
#endif
