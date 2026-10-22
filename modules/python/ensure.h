/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/context.h"

/// Called at the top of every operator of the module, before it takes the GIL.
///
/// The first call checks the active venv, creates the process's embedded interpreter (or attaches
/// to the one `pyplot` created), adds the entry directory to `sys.path` and writes the
/// interpreter's metadata line; every later call is a no-op. A fault is thrown here when CPython
/// cannot be initialised or the venv does not match the build: `import python` no longer runs any
/// of this (design principles 3.1; the language author's ruling of 2026-10-22; handover H221).
void ensurePythonInterpreter(camel::core::context::Context &ctx);