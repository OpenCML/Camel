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
 * Created: Dec. 17, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/func.h"
#include "camel/compile/gir.h"

const type::TupleType *Function::tupleType() const { return graph_->closureType(); }

void Function::print(std::ostream &os, const type::Type *type) const {
    os << "Function(graph=" << graph_->name() << ", tuple=";
    if (closure_) {
        const type::TupleType *tupleTypePtr = tupleType();
        closure_->print(os, tupleTypePtr);
    } else {
        os << "null";
    }
    os << ")";
}
