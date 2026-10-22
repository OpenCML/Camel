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
 * Author: Zhenjie Wei
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

// `std::cml`: print a runtime program as Camel source (handover H240, step S1).
#pragma once

#include "camel/execute/pass/params.h"
#include "camel/execute/pass/runtime_trans.h"
#include "camel/runtime/program.h"

#include <cstddef>

struct CmlOptions {
    /// The largest constant, in bytes of its storage, the printer writes as a literal. A larger
    /// one is refused and reported by name (the review's big-constant ruling).
    size_t maxLiteralBytes = 64 * 1024;
};

/// The parameters of `std::cml`.
PassDescriptor cmlDescriptor();

class CmlDumpPass : public RuntimeGraphTranslatePass {
  public:
    explicit CmlDumpPass(
        const camel::core::context::context_ptr_t &ctx, CmlOptions options = {})
        : RuntimeGraphTranslatePass(ctx), options_(options) {}
    const camel::runtime::Program *apply(const camel::runtime::Program *, std::ostream &) override;
    /// The printer reads graphs as they are and writes no writings.
    bool acceptsWritings() const override { return false; }

  private:
    CmlOptions options_;
};