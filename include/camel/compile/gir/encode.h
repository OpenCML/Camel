/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Compile-side Graph -> GCGraph export entrypoints.
 *
 * Runtime graph materialization is a compile-side responsibility. Keep the
 * API here rather than on `Graph` itself so the mutable compile graph object
 * does not own runtime-export behavior.
 */

#pragma once

#include "types.h"

namespace camel::runtime {
class GCGraph;
}

namespace camel::compile::gir {

camel::runtime::GCGraph *encodeToRuntimeGraph(const graph_ptr_t &rootGraph);

} // namespace camel::compile::gir
