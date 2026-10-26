// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/module/builtin.h"

class PyplotModule : public camel::core::module::BuiltinModule {
  public:
    PyplotModule(camel::core::context::context_ptr_t ctx);
    ~PyplotModule() override = default;

    bool load() override;

    static camel::core::module::module_ptr_t create(camel::core::context::context_ptr_t ctx);
};

/// Called at the top of `plot`, before it takes the GIL. The first call creates the process's
/// embedded interpreter (or attaches to the one `python` created) and every later call is a
/// no-op; a fault is thrown here when CPython cannot be initialised. `import pyplot` no longer
/// runs any of this (design principles 3.1).
void ensurePyplotInterpreter();
