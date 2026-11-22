// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Running bytecode on behalf of another scheduler.
//
// FastVMSchedPass is a scheduler of its own: it compiles a whole program and runs it on one
// thread. A *host* (the parallel scheduler, src/passes/sched/taskflow) evaluates graphs itself
// and wants only some of them run as bytecode, on any of its threads. This file is the whole
// interface it needs:
//
//   FastVMHostImage            the bytecode of the graphs the host chose, compiled once
//   FastVMSchedPass(ctx, {}, &image.codes())
//                              an interpreter over the image; one per host thread
//   acquireHostedFrame / callHosted (fastvm.h)
//                              one call of a compiled graph
//   FastVmSafepointScope       routes the interpreter's safepoints to the host while a hosted
//                              call runs on the thread
//
// Lifetime and sharing: the host owns the image and the interpreters and destroys the
// interpreters first. The image is written by build() only, before any interpreter runs, and is
// read-only afterwards, so the threads need no lock for it. Everything else an interpreter has
// (frame pool, call stack, counters) belongs to its thread.

#pragma once

#include "fastvm.h"

#include <span>
#include <unordered_map>

class FastVMHostImage {
  public:
    FastVMHostImage() = default;
    FastVMHostImage(const FastVMHostImage &)            = delete;
    FastVMHostImage &operator=(const FastVMHostImage &) = delete;

    /// Compiles `graphs` with the strategy of std::fvm and links them. The list must be closed
    /// under direct calls: every graph a listed graph calls with FUNC or TAIL is listed. Called
    /// once, by one thread, before any interpreter over the image runs. Also registers the
    /// source origin of every pc in the context's debug map (fault positions).
    void build(
        const ctx::context_ptr_t &context, const camel::runtime::Program &program,
        std::span<camel::runtime::GCGraph *const> graphs);

    /// The entry pc of every compiled graph.
    const std::unordered_map<camel::runtime::GCGraph *, size_t> &entries() const {
        return entries_;
    }
    /// What an interpreter is constructed over.
    bytecode_vec_t &codes() { return codes_; }

  private:
    bytecode_vec_t codes_;
    std::unordered_map<camel::runtime::GCGraph *, size_t> entries_;
};

/// Installs `hook` as the calling thread's safepoint hook for its lifetime and puts back what
/// was installed before (hosted calls may nest on a thread).
class FastVmSafepointScope {
  public:
    explicit FastVmSafepointScope(FastVmSafepointHook hook) : saved_(fastVmSafepointHook()) {
        fastVmSafepointHook() = hook;
    }
    ~FastVmSafepointScope() { fastVmSafepointHook() = saved_; }
    FastVmSafepointScope(const FastVmSafepointScope &)            = delete;
    FastVmSafepointScope &operator=(const FastVmSafepointScope &) = delete;

  private:
    FastVmSafepointHook saved_;
};
