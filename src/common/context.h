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
 * Created: Aug. 18, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, GIR::node_ptr_t>;
using node_scope_ptr_t = scope_ptr_t<std::string, GIR::node_ptr_t>;
using graph_scope_t = Scope<std::string, std::shared_ptr<GIR::graph_vec_t>>;
using graph_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<GIR::graph_vec_t>>;
using operator_scope_t = Scope<std::string, std::shared_ptr<operator_vec_t>>;
using operator_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<operator_vec_t>>;

class Context {

  public:
    Context() = default;
    virtual ~Context() = default;
};

using context_ptr_t = std::shared_ptr<Context>;
