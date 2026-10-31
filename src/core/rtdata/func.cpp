// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/func.h"
#include "camel/core/mm.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

camel::runtime::GCGraph *Function::graph() const {
    return program_ != nullptr ? program_->graph(graphId_) : nullptr;
}

void Function::rebind(const camel::runtime::Program *program, camel::runtime::GraphId graph) {
    GUARD(program != nullptr, "Runtime Function program cannot be null, and the call passed none: a function value with no program has no graphs to name");
    GUARD(program->graph(graph) != nullptr, "Runtime Function graph must be in its program, and that graph is not one its program holds");
    program_ = program;
    graphId_ = graph;
}

void Function::updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) {
    (void)type;
    // The Program the function was made in lives as long as the function does. A function the
    // collector reaches after the program was freed (a dead one, not yet swept) names nothing.
    const bool programLive =
        program_ != nullptr &&
        camel::core::mm::graphSpace().isAllocated(const_cast<camel::runtime::Program *>(program_));
    if (programLive) {
        relocate(
            static_cast<rtdata::Object *>(const_cast<camel::runtime::Program *>(program_)),
            nullptr,
            rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = type,
                .slotType  = nullptr,
                .ownerKind = "Function",
                .slotName  = "program",
                .slotIndex = rtdata::RefTraceInfo::npos,
            });
    }
    if (camel::runtime::GCGraph *runtimeGraph = programLive ? graph() : nullptr) {
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
        // The closure's layout, not the one the program's graph would answer: this walk also
        // reaches function values whose program the collector already freed (that is what the
        // check above is for), and `tupleType()` reads `program_->graph(graphId_)` first -- through
        // a pointer this walk may no longer use.
        const type::TupleType *tupleTypePtr = closureType();
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
    GUARD(graph() != nullptr, "Runtime Function graph cannot be null: this function value names no graph, so there is nothing to run");
    return tupleType_;
}

Function *Function::create(
    const camel::runtime::Program *program, camel::runtime::GraphId graph,
    const type::Type *tupleType, camel::core::mm::IAllocator &allocator) {
    GUARD(program != nullptr, "Runtime Function program cannot be null, and the call passed none: a function value with no program has no graphs to name");
    GUARD(camel::runtime::isValid(graph), "Runtime Function graph cannot be null: this function value names a graph the program does not hold");
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
    GUARD(runtimeGraph != nullptr, "Function print requires graph identity, and this function value has no runtime graph to name");
    os << "Function(graph=" << runtimeGraph->name() << ", tupleSlots=";
    os << (closure_ ? std::to_string(closure_->size()) : "null");
    os << ")";
}
