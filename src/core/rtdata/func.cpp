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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "func.h"
#include "compile/gir.h"

const TupleType *Function::tupleType() const { return graph_->closureType(); }

void Function::print(std::ostream &os, const Type *type) const {
    os << "Function(graph=" << graph_->name() << ", tuple=";
    if (tuple_) {
        const TupleType *tupleTypePtr = tupleType();
        tuple_->print(os, tupleTypePtr);
    } else {
        os << "null";
    }
    os << ")";
}
