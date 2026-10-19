/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Finalize CUDA work exactly once, retaining executable, inputs and faults until completion.
#include "tiles/internal/cuda_submission.h"

#include <memory>
#include <mutex>
#ifdef CAMEL_TILES_CUDA
#include <climits>
#include <stdexcept>
#include <utility>

namespace camel::tiles::cuda_detail {
Submission::Submission(
    std::shared_ptr<Backend> backend, std::unique_ptr<Resources> resources,
    std::shared_ptr<const Program> program, ModuleLease module)
    : backend_(std::move(backend)), program_(std::move(program)),
      module_(std::in_place, std::move(module)), resources_(std::move(resources)),
      stream_(resources_ ? resources_->stream : nullptr) {
    try {
        if (!backend_ || !resources_ || &resources_->api != &backend_->api || !program_ ||
            module_->backend.get() != backend_.get())
            throw std::invalid_argument("inconsistent CUDA submission ownership");
        event_ = std::make_unique<CompletionEvent>(backend_);
    } catch (...) {
        release();
        throw;
    }
}
Submission::~Submission() {
    try {
        if (recorded_)
            wait();
    } catch (...) {
        // Public completion reports faults. Abandoning native work still drains
        // it, but a destructor cannot report an execution or driver exception.
    }
    release();
}
void Submission::release() const noexcept {
    // Resources drains even after a failed record/query. Module and input/output
    // owners remain live until that drain has completed or the driver has failed.
    resources_.reset();
    owners_.clear();
    dependencies_.clear();
    program_.reset();
    module_.reset();
}
Resources &Submission::resources() {
    std::lock_guard lock(mutex_);
    if (state_ != State::Open)
        throw std::logic_error("CUDA submission is already finished");
    return *resources_;
}
CUstream Submission::stream() const noexcept { return stream_; }
void Submission::keepAlive(std::shared_ptr<void> owner) {
    std::lock_guard lock(mutex_);
    if (state_ != State::Open)
        throw std::logic_error("cannot retain an owner after CUDA submission finish");
    if (owner)
        owners_.push_back(std::move(owner));
}
void Submission::depend(std::shared_ptr<DeviceCompletion> dependency) {
    {
        std::lock_guard lock(mutex_);
        if (state_ != State::Open)
            throw std::logic_error("cannot add a dependency after CUDA submission finish");
    }
    if (!dependency)
        return;
    if (dependency.get() == this)
        throw std::logic_error("CUDA submission cannot depend on itself");
    auto producer = std::dynamic_pointer_cast<Submission>(dependency);
    if (producer) {
        {
            std::lock_guard lock(producer->mutex_);
            if (!producer->recorded_)
                throw std::logic_error("CUDA dependency has not recorded completion");
        }
        // Reclaim completed ancestors during long native chains. A recorded
        // failure stays on the dependency and is reported at completion, rather
        // than changing the fault boundary of an otherwise asynchronous submit.
        try {
            (void)producer->ready();
        } catch (...) {
        }
    }
    // A foreign completion has no event usable by this context. Resolve that
    // boundary before locking this submission; never recurse under its mutex.
    const bool streamDependency = producer && producer->backend_->context == backend_->context;
    if (!streamDependency)
        dependency->wait();
    std::lock_guard lock(mutex_);
    if (state_ != State::Open)
        throw std::logic_error("cannot add a dependency after CUDA submission finish");
    // Retain before enqueueing a wait, so even an exception preserves its event
    // and pending resources. Completed producers may already have released
    // staging; input allocations are owned by this program or keepAlive owners.
    dependencies_.push_back(std::move(dependency));
    if (streamDependency)
        producer->event_->waitOn(resources_->stream);
}
void Submission::finish(CUdeviceptr fault) {
    std::unique_lock lock(mutex_);
    if (state_ != State::Open)
        throw std::logic_error("CUDA submission finish called twice");
    try {
        if (fault) {
            faultHost_ = static_cast<unsigned *>(resources_->allocateHost(sizeof(unsigned)));
            Current current(backend_->api, backend_->context);
            backend_->api.check(
                backend_->api
                    .cuMemcpyDtoHAsync(faultHost_, fault, sizeof(unsigned), resources_->stream),
                "read scalar fault asynchronously");
        }
        // Record even when there are no launches or no output elements: stream
        // dependencies and the fault copy are part of completion in every case.
        event_->record(resources_->stream);
        recorded_ = true;
        state_    = State::Recorded;
    } catch (...) {
        const auto failure = std::current_exception();
        state_             = State::Completing;
        lock.unlock();
        release();
        lock.lock();
        failure_ = failure;
        state_   = State::Complete;
        lock.unlock();
        changed_.notify_all();
        std::rethrow_exception(failure);
    }
}
bool Submission::settle(bool blocking) const {
    std::unique_lock lock(mutex_);
    for (;;) {
        if (state_ == State::Complete) {
            if (failure_)
                std::rethrow_exception(failure_);
            return true;
        }
        if (state_ == State::Open)
            throw std::logic_error("CUDA submission has not recorded completion");
        if (state_ != State::Completing)
            break;
        if (!blocking)
            return false;
        changed_.wait(lock);
    }
    state_ = State::Completing;
    lock.unlock();
    bool complete = true;
    std::exception_ptr failure;
    try {
        if (blocking)
            event_->wait();
        else
            complete = event_->ready();
        if (complete) {
            for (const auto &dependency : dependencies_) {
                if (blocking)
                    dependency->wait();
                else if (!dependency->ready()) {
                    complete = false;
                    break;
                }
            }
        }
        if (complete && faultHost_ && *faultHost_ != UINT_MAX)
            throw std::runtime_error(
                "division by zero at Tiles node " + std::to_string(*faultHost_));
    } catch (...) {
        failure = std::current_exception();
    }
    if (complete || failure)
        release();
    lock.lock();
    if (complete || failure) {
        failure_ = failure;
        state_   = State::Complete;
    } else {
        state_ = State::Recorded;
    }
    lock.unlock();
    changed_.notify_all();
    if (failure)
        std::rethrow_exception(failure);
    return complete;
}
void Submission::wait() const { (void)settle(true); }
bool Submission::ready() const { return settle(false); }
} // namespace camel::tiles::cuda_detail
#endif
