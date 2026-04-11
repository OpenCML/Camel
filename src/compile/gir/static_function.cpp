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
 * Created: Apr. 11, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Compile-time static function carrier implementation.
 *
 * Runtime `::Function` now models only executable GCGraph closures. GIR still
 * needs a GC-traced object form for static function values during rewrite and
 * validation, so this file keeps that representation entirely on the
 * compile-time side.
 */

#include "camel/compile/gir/static_function.h"

#include "camel/compile/gir/graph.h"

namespace camel::compile::gir {

using camel::core::rtdata::isOfSameCls;
namespace type = camel::core::type;

const type::TupleType *StaticFunction::tupleType() const {
    ASSERT(graph_ != nullptr, "Compile-time static function graph cannot be null.");
    return graph_->closureType();
}

StaticFunction *StaticFunction::create(
    Graph *graph, const type::Type *tupleType, camel::core::mm::IAllocator &allocator) {
    ASSERT(graph != nullptr, "Compile-time static function graph cannot be null.");
    ASSERT(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
    const auto *tt = static_cast<const type::TupleType *>(tupleType);
    ASSERT(
        tt->size() == graph->closure().size(),
        "Compile-time function closure tuple size mismatch.");

    void *mem = allocator.alloc(sizeof(StaticFunction), alignof(StaticFunction));
    if (!mem) {
        throw std::bad_alloc();
    }

    auto *fn     = new (mem) StaticFunction(graph);
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
    if (graph_ != fnOther->graph_) {
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

    auto *fnNew = new (mem) StaticFunction(graph_);
    if (closure_) {
        const type::TupleType *tupleTypePtr = tupleType();
        fnNew->closure_ = static_cast<::Tuple *>(closure_->clone(allocator, tupleTypePtr, deep));
    }
    return reinterpret_cast<camel::core::rtdata::Object *>(fnNew);
}

void StaticFunction::print(std::ostream &os, const type::Type *type) const {
    (void)type;
    ASSERT(graph_ != nullptr, "Compile-time static function print requires graph identity.");
    os << "StaticFunction(graph=" << graph_->name() << ", tupleSlots=";
    os << (closure_ ? std::to_string(closure_->size()) : "null");
    os << ")";
}

void StaticFunction::updateRefs(
    const std::function<camel::core::rtdata::Object *(camel::core::rtdata::Object *)> &relocate,
    const type::Type *type) {
    (void)type;
    if (!closure_) {
        return;
    }
    const type::TupleType *tupleTypePtr = tupleType();
    closure_->updateRefs(relocate, tupleTypePtr);
}

} // namespace camel::compile::gir
