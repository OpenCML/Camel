/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
 *   top     rows of the operator table (0: every operator that ran)
 *   hints   report the element-wise loops that run on the host
 *   json    also write the report as JSON to this file
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "camel/profile/profile.h"

#include <string>

class ProfilePass : public RuntimeGraphRewritePass {
  public:
    ProfilePass(
        const camel::core::context::context_ptr_t &ctx, camel::profile::ReportOptions options,
        std::string jsonPath);

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    /// One hint per LOOP node of a reachable graph: what its body is, and why it stays on the
    /// host. The rounds the loop ran come from the session, not from here.
    void noteLoopsOf(const camel::runtime::Program &program);

    camel::profile::ReportOptions options_;
    std::string jsonPath_;
};