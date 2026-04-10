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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/func.h"
#include "camel/compile/gir/graph.h"
#include "camel/runtime/graph.h"

GIR::Graph *Function::sourceGraph() const { return sourceGraph_; }

void Function::setRuntimeGraph(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime Function graph cannot be null.");
    runtimeGraph_ = graph;
}

const type::TupleType *Function::tupleType() const {
    if (runtimeGraph_) {
        return runtimeGraph_->closureType();
    }
    ASSERT(sourceGraph_ != nullptr, "Function source graph cannot be null.");
    return sourceGraph_->closureType();
}

Function *Function::create(
    GIR::Graph *graph, const type::Type *tupleType, camel::core::mm::IAllocator &allocator) {
    ASSERT(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
    const type::TupleType *tt = static_cast<const type::TupleType *>(tupleType);
    ASSERT(graph != nullptr, "Function graph cannot be null.");
    ASSERT(tt->size() == graph->closure().size(), "Function closure tuple size mismatch.");

    void *mem = allocator.alloc(sizeof(Function), alignof(Function));
    if (!mem)
        throw std::bad_alloc();

    auto *fn     = new (mem) Function(graph);
    fn->closure_ = Tuple::create(tt->size(), allocator);
    return fn;
}

Function *Function::create(
    camel::runtime::GCGraph *graph, const type::Type *tupleType,
    camel::core::mm::IAllocator &allocator) {
    ASSERT(graph != nullptr, "Runtime Function graph cannot be null.");
    ASSERT(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
    const type::TupleType *tt = static_cast<const type::TupleType *>(tupleType);
    ASSERT(
        tt->size() == graph->closureType()->size(),
        "Runtime Function closure tuple size mismatch.");

    void *mem = allocator.alloc(sizeof(Function), alignof(Function));
    if (!mem)
        throw std::bad_alloc();

    auto *fn     = new (mem) Function(nullptr, graph);
    fn->closure_ = Tuple::create(tt->size(), allocator);
    return fn;
}

void Function::print(std::ostream &os, const type::Type *type) const {
    (void)type;
    ASSERT(runtimeGraph_ || sourceGraph_, "Function print requires graph identity.");
    const std::string &graphName = runtimeGraph_ ? runtimeGraph_->name() : sourceGraph_->name();
    os << "Function(graph=" << graphName << ", tupleSlots=";
    os << (closure_ ? std::to_string(closure_->size()) : "null");
    os << ")";
}
