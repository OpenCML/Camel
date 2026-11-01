// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Compile-time static function carrier implementation.
 *
 * Runtime `::Function` now models only executable GCGraph closures. GIR still
 * needs a GC-traced object form for static function values during rewrite and
 * validation, so this file keeps that representation entirely on the
 * compile-time side.
 */

#include "camel/compile/gir/static_function.h"

#include "camel/core/type/composite/tuple.h"

namespace camel::compile::gir {

using camel::core::rtdata::isOfSameCls;
namespace type = camel::core::type;

const type::TupleType *StaticFunction::tupleType() const { return tupleType_; }

StaticFunction *StaticFunction::create(
    const std::shared_ptr<DraftGraphBuilder> &graph, const type::Type *tupleType,
    camel::core::mm::IAllocator &allocator) {
    GUARD(graph != nullptr, "Compile-time static function graph cannot be null.");
    GUARD(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
    const auto *tt = static_cast<const type::TupleType *>(tupleType);
    GUARD(
        tt->size() == graph->view().closureNodes().size(),
        "Compile-time function closure tuple size mismatch.");

    void *mem = allocator.alloc(sizeof(StaticFunction), alignof(StaticFunction));
    if (!mem) {
        throw std::bad_alloc();
    }

    auto *fn     = new (mem) StaticFunction(graph, tt);
    fn->closure_ = ::Tuple::create(tt->size(), allocator);
    return fn;
}

bool StaticFunction::equals(
    const camel::core::rtdata::Object *other, const type::Type *type, bool deep) const {
    if (this == other) {
        return true;
    }
    if (!isOfSameCls(this, other)) {
        return false;
    }

    const auto *fnOther = reinterpret_cast<const StaticFunction *>(other);
    if (graph_.get() != fnOther->graph_.get()) {
        return false;
    }

    const type::TupleType *tupleTypePtr = tupleType();
    if (!closure_ && !fnOther->closure_) {
        return true;
    }
    if (!closure_ || !fnOther->closure_) {
        return false;
    }

    return closure_->equals(fnOther->closure_, tupleTypePtr, deep);
}

camel::core::rtdata::Object *StaticFunction::clone(
    camel::core::mm::IAllocator &allocator, const type::Type *type, bool deep) const {
    (void)type;
    void *mem = allocator.alloc(sizeof(StaticFunction), alignof(StaticFunction));
    if (!mem) {
        throw std::bad_alloc();
    }

    auto *fnNew = new (mem) StaticFunction(graph_, tupleType_);
    if (closure_) {
        const type::TupleType *tupleTypePtr = tupleType();
        fnNew->closure_ = static_cast<::Tuple *>(closure_->clone(allocator, tupleTypePtr, deep));
    }
    return reinterpret_cast<camel::core::rtdata::Object *>(fnNew);
}

void StaticFunction::print(std::ostream &os, const type::Type *type) const {
    (void)type;
    GUARD(graph_ != nullptr, "Compile-time static function print requires graph identity.");
    os << "StaticFunction(graph=" << graph_->name() << ", tupleSlots=";
    os << (closure_ ? std::to_string(closure_->size()) : "null");
    os << ")";
}

void StaticFunction::updateRefs(
    const camel::core::rtdata::Object::RefRelocator &relocate, const type::Type *type) {
    (void)type;
    if (!closure_) {
        return;
    }
    const type::TupleType *tupleTypePtr = tupleType();
    closure_                            = static_cast<::Tuple *>(relocate(
        closure_,
        tupleTypePtr,
        camel::core::rtdata::RefTraceInfo{
            .owner     = this,
            .ownerType = type,
            .slotType  = tupleTypePtr,
            .ownerKind = "StaticFunction",
            .slotName  = "closure",
            .slotIndex = camel::core::rtdata::RefTraceInfo::npos,
        }));
}

StaticProgramFunction *StaticProgramFunction::create(
    const camel::runtime::Program *program, camel::runtime::GraphId graph,
    const type::FunctionType *type, camel::core::mm::IAllocator &allocator) {
    GUARD(program != nullptr, "A program function value needs the program its graph is in.");
    GUARD(
        camel::runtime::isValid(graph), "A program function value needs the graph it names.");
    GUARD(type != nullptr, "A program function value needs the type of the value.");
    void *mem = allocator.alloc(sizeof(StaticProgramFunction), alignof(StaticProgramFunction));
    if (!mem) {
        throw std::bad_alloc();
    }
    return new (mem) StaticProgramFunction(program, graph, type);
}

const type::TupleType *StaticProgramFunction::tupleType() const { return type::TupleType::create(); }

bool StaticProgramFunction::equals(
    const camel::core::rtdata::Object *other, const type::Type *type, bool deep) const {
    (void)deep;
    if (this == other) {
        return true;
    }
    if (!isOfSameCls(this, other)) {
        return false;
    }
    const auto *otherFunction = reinterpret_cast<const StaticProgramFunction *>(other);
    // Two values of one graph are one value: the graph is what the value is of, and a program
    // never holds two graphs under one id. The type is interned, so it is compared as one.
    (void)type;
    return program_ == otherFunction->program_ && graph_ == otherFunction->graph_ &&
           type_ == otherFunction->type_;
}

camel::core::rtdata::Object *StaticProgramFunction::clone(
    camel::core::mm::IAllocator &allocator, const type::Type *type,
    bool deep) const {
    (void)type;
    (void)deep;
    return reinterpret_cast<camel::core::rtdata::Object *>(
        create(program_, graph_, type_, allocator));
}

void StaticProgramFunction::print(std::ostream &os, const type::Type *type) const {
    (void)type;
    os << "StaticProgramFunction(graph=" << camel::runtime::toValue(graph_) << ")";
}

void StaticProgramFunction::updateRefs(
    const camel::core::rtdata::Object::RefRelocator &relocate, const type::Type *type) {
    // The program and the graph are not heap objects this value owns: the program is the
    // context's (the transform asked for it and kept it) and the graph is its member. Nothing to
    // relocate.
    (void)relocate;
    (void)type;
}

bool isStaticFunction(const camel::core::rtdata::Object *object) {
    return object != nullptr && typeid(*object) == typeid(StaticFunction);
}

bool isStaticProgramFunction(const camel::core::rtdata::Object *object) {
    return object != nullptr && typeid(*object) == typeid(StaticProgramFunction);
}

} // namespace camel::compile::gir
