// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/context/context.h"

/// Called at the top of every operator of the module, before it takes the GIL.
///
/// The first call checks the active venv, creates the process's embedded interpreter (or attaches
/// to the one `pyplot` created), adds the entry directory to `sys.path` and writes the
/// interpreter's metadata line; every later call is a no-op. A fault is thrown here when CPython
/// cannot be initialised or the venv does not match the build: `import python` no longer runs any
/// of this (design principles 3.1).
void ensurePythonInterpreter(camel::core::context::Context &ctx);