/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 16, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, gir::node_ptr_t>;
using node_scope_ptr_t = scope_ptr_t<std::string, gir::node_ptr_t>;
using operator_scope_t = Scope<std::string, Operator>;
using operator_scope_ptr_t = scope_ptr_t<std::string, Operator>;

class Context {
    node_scope_ptr_t scope_;
    operator_scope_ptr_t opScope_;

  public:
    Context();
    virtual ~Context() = default;

    node_scope_t &scope() { return *scope_; }
    operator_scope_t &opScope() { return *opScope_; }

    void pushScope() {
        scope_ = scope_->push();
        opScope_ = opScope_->push();
    }

    void popScope() {
        scope_ = scope_->pop();
        opScope_ = opScope_->pop();
    }
};

using context_ptr_t = std::shared_ptr<Context>;
