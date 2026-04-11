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
 * Updated: Apr. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/func.h"
#include "camel/runtime/graph.h"

void Function::setRuntimeGraph(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime Function graph cannot be null.");
    runtimeGraph_ = graph;
}

const type::TupleType *Function::tupleType() const {
    ASSERT(runtimeGraph_ != nullptr, "Runtime Function graph cannot be null.");
    return runtimeGraph_->closureType();
}

Function *Function::create(
    camel::runtime::GCGraph *graph, const type::Type *tupleType,
    camel::core::mm::IAllocator &allocator) {
    ASSERT(graph != nullptr, "Runtime Function graph cannot be null.");
    ASSERT(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
    const type::TupleType *tt = static_cast<const type::TupleType *>(tupleType);

    void *mem = allocator.alloc(sizeof(Function), alignof(Function));
    if (!mem)
        throw std::bad_alloc();

    auto *fn     = new (mem) Function(graph);
    fn->closure_ = Tuple::create(tt->size(), allocator);
    return fn;
}

void Function::print(std::ostream &os, const type::Type *type) const {
    (void)type;
    ASSERT(runtimeGraph_ != nullptr, "Function print requires graph identity.");
    os << "Function(graph=" << runtimeGraph_->name() << ", tupleSlots=";
    os << (closure_ ? std::to_string(closure_->size()) : "null");
    os << ")";
}
