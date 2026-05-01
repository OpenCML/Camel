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
 * Created: Apr. 12, 2026
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Internal helpers for bridging UserDefinedModule to compile/runtime graph
 * carriers. These APIs are intentionally kept out of the public module
 * surface so runtime-facing code does not learn about compile-graph details.
 */

#pragma once

#include "camel/core/module/userdef.h"

namespace camel::core::module::detail {

class UserDefinedModuleAccess {
  public:
    static const void *compileGraphOpaque(const UserDefinedModule &module);
    static camel::runtime::GCGraph *encodeRuntimeGraph(const UserDefinedModule &module);
};

} // namespace camel::core::module::detail
