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
 * Updated: Oct. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/rtdata/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

camel::runtime::GCGraph *Function::graph() const {
    return program_ != nullptr ? program_->graph(graphId_) : nullptr;
}

void Function::rebind(const camel::runtime::Program *program, camel::runtime::GraphId graph) {
    ASSERT(program != nullptr, "Runtime Function program cannot be null.");
    ASSERT(program->graph(graph) != nullptr, "Runtime Function graph must be in its program.");
    program_ = program;
    graphId_ = graph;
}

void Function::updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) {
    (void)type;
    if (camel::runtime::GCGraph *runtimeGraph = graph()) {
        // A runtime-created graph (e.g. an autodiff gradient graph built while the program
        // runs) is no graph-carrier root: it is only reachable through this Function. Trace it
        // so its static constants live exactly as long as the Function does. Program graphs
        // are carrier roots already; re-tracing them is a no-op via their mark bit. Graph
        // carriers live in graphSpace and never move, so the assignment is stable.
        runtimeGraph = static_cast<camel::runtime::GCGraph *>(relocate(
            static_cast<rtdata::Object *>(runtimeGraph),
            nullptr,
            rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = type,
                .slotType  = nullptr,
                .ownerKind = "Function",
                .slotName  = "graph",
                .slotIndex = rtdata::RefTraceInfo::npos,
            }));
    }
    if (closure_) {
        const type::TupleType *tupleTypePtr = tupleType();
        closure_                            = static_cast<Tuple *>(relocate(
            closure_,
            tupleTypePtr,
            rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = type,
                .slotType  = tupleTypePtr,
                .ownerKind = "Function",
                .slotName  = "closure",
                .slotIndex = rtdata::RefTraceInfo::npos,
            }));
    }
}

const type::TupleType *Function::tupleType() const {
    ASSERT(graph() != nullptr, "Runtime Function graph cannot be null.");
    return tupleType_;
}

Function *Function::create(
    const camel::runtime::Program *program, camel::runtime::GraphId graph,
    const type::Type *tupleType, camel::core::mm::IAllocator &allocator) {
    ASSERT(program != nullptr, "Runtime Function program cannot be null.");
    ASSERT(camel::runtime::isValid(graph), "Runtime Function graph cannot be null.");
    ASSERT(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
    const type::TupleType *tt = static_cast<const type::TupleType *>(tupleType);

    void *mem = allocator.alloc(sizeof(Function), alignof(Function));
    if (!mem)
        throw std::bad_alloc();

    auto *fn     = new (mem) Function(program, graph, tt);
    fn->closure_ = Tuple::create(tt->size(), allocator);
    camel::core::mm::writeBarrier(fn, nullptr, fn->closure_, tt);
    return fn;
}

void Function::print(std::ostream &os, const type::Type *type) const {
    (void)type;
    const camel::runtime::GCGraph *runtimeGraph = graph();
    ASSERT(runtimeGraph != nullptr, "Function print requires graph identity.");
    os << "Function(graph=" << runtimeGraph->name() << ", tupleSlots=";
    os << (closure_ ? std::to_string(closure_->size()) : "null");
    os << ")";
}
