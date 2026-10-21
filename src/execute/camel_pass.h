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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Passes written in Camel (the graph module).
 *
 * A pass is a function `(p: Program) => Program!` exported by a module written in Camel. It is
 * named on the pipeline as `<module>::<function>`, like the passes of the C++ modules. The
 * module is compiled as a program of its own whose entry is that function, the tool program,
 * which the context keeps: its root graph holds the module's top-level values — computed before
 * the function runs, as a program computes them before `main` (design principles 10.1) — and
 * calls the function, whose captures are the closure of the function value that call makes. The
 * program being processed is the argument, a value of the tool program's run. The tool program
 * runs on NodeVM.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/range.h"

#include <optional>
#include <string>

namespace camel::execute {

/// Registers the pass `<module>::<function>` when the module `module` exports a function of that
/// name with the signature of a pass. Nothing is registered when the module is not there or
/// exports no such function (`problem` is empty: the pass is unknown), or when it is there and
/// refused (it does not compile, it is not a pass), and then `problem` says why; `errorLocation`,
/// when given, is the library's first error as a secondary location when it has one.
void registerCamelPass(
    const camel::core::context::context_ptr_t &ctx, const std::string &module,
    const std::string &function, std::string &problem,
    std::optional<camel::core::error::DiagLabel> *errorLocation = nullptr);

} // namespace camel::execute
