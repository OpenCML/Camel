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
 * Created: Feb. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
/// runs any of this (design principles 3.1; the language author's ruling of 2026-10-22; handover
/// H221).
void ensurePyplotInterpreter();
