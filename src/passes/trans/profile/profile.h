// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::profile: the profiling session of camel/profile/profile.h.
 *
 * The pass itself does nothing to the program: it turns the session on, records what the
 * pipeline gives it (the element-wise loops that will run on the host, and why), and passes the
 * program on unchanged. The report is written when the run ends, by the driver, because that is
 * the only place that has seen the whole run: `camel f.cml std::profile std::fvm` profiles the
 * bytecode VM, `... std::profile std::jit` the JIT, `... std::profile std::nvm` the node VM and
 * `... std::profile <a module's pass>` that pass. The compilation phases in the report come from
 * the phase recorder, which the driver turns on before the first module is parsed when the
 * pipeline names this pass.
 *
 * Parameters:
 *   top          rows of the operator table (0: every operator that ran)
 *   hints        report the element-wise loops that run on the host
 *   json         also write the report as JSON to this file
 *   report       write the text report when the run ends (false: only the diagnostics below)
 *   gc_stats     the allocator's statistics at exit: 1 one line, 2 also one line per collection
 *   type_stats   the type storage's statistics at exit
 *   effect_trace write the effect trace (camel/core/effect_trace.h) to this file
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/profile/profile.h"

#include <string>

class ProfilePass : public RuntimeGraphRewritePass {
  public:
    ProfilePass(
        const camel::core::context::context_ptr_t &ctx, camel::profile::ReportOptions options,
        std::string jsonPath, int gcStats, bool typeStats, std::string effectTracePath,
        bool opStats, bool variantTrace);

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    /// One hint per LOOP node of a reachable graph: what its body is, and why it stays on the
    /// host. The rounds the loop ran come from the session, not from here.
    void noteLoopsOf(const camel::runtime::Program &program);

    camel::profile::ReportOptions options_;
    std::string jsonPath_;
    int gcStats_;
    bool typeStats_;
    std::string effectTracePath_;
    bool opStats_;
    bool variantTrace_;
};
