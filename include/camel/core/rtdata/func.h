/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Nov. 07, 2025
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/tuple.h"
#include "tuple.h"

// Forward declarations.
namespace camel::compile::gir {
class Graph;
} // namespace camel::compile::gir

namespace camel::runtime {
class GCGraph;
} // namespace camel::runtime

namespace GIR = camel::compile::gir;

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Function : public rtdata::Object {
  public:
    Function(const Function &)            = delete;
    Function &operator=(const Function &) = delete;

    static Function *
    create(GIR::Graph *graph, const type::Type *tupleType, camel::core::mm::IAllocator &allocator);
    static Function *create(
        camel::runtime::GCGraph *graph, const type::Type *tupleType,
        camel::core::mm::IAllocator &allocator);

    // Runtime identity always flows through the materialized GCGraph carrier.
    camel::runtime::GCGraph *graph() const { return runtimeGraph_; }
    camel::runtime::GCGraph *runtimeGraph() const { return runtimeGraph_; }
    bool hasRuntimeGraph() const { return runtimeGraph_ != nullptr; }
    // Compile GIR access is a cold metadata bridge. Runtime execution must
    // never depend on it for identity, and runtime-created Function objects
    // may legitimately leave it null.
    GIR::Graph *sourceGraph() const;
    void setSourceGraph(GIR::Graph *graph) {
        ASSERT(graph != nullptr, "Function source graph cannot be null.");
        sourceGraph_ = graph;
    }
    void setRuntimeGraph(camel::runtime::GCGraph *graph);
    Tuple *tuple() { return closure_; }
    const Tuple *tuple() const { return closure_; }

    // Resolve the closure tuple layout from the active graph carrier.
    const type::TupleType *tupleType() const;

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        const Function *fnOther = reinterpret_cast<const Function *>(other);

        if (runtimeGraph_ != fnOther->runtimeGraph_)
            return false;
        if (runtimeGraph_ == nullptr && sourceGraph_ != fnOther->sourceGraph_)
            return false;

        const type::TupleType *tupleTypePtr = tupleType();
        if (!closure_ && !fnOther->closure_)
            return true;
        if (!closure_ || !fnOther->closure_)
            return false;

        return closure_->equals(fnOther->closure_, tupleTypePtr, deep);
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        void *mem = allocator.alloc(sizeof(Function), alignof(Function));
        if (!mem)
            throw std::bad_alloc();

        Function *fnNew = new (mem) Function(sourceGraph_, runtimeGraph_);

        if (closure_) {
            const type::TupleType *tupleTypePtr = tupleType();
            fnNew->closure_ = static_cast<Tuple *>(closure_->clone(allocator, tupleTypePtr, deep));
        } else {
            fnNew->closure_ = nullptr;
        }

        return reinterpret_cast<rtdata::Object *>(fnNew);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override;

    virtual void onMoved() override {
        // Graph pointers are non-owning metadata references. Closure objects are
        // updated through updateRefs when the GC relocates traced objects.
    }

    virtual void updateRefs(
        const std::function<rtdata::Object *(rtdata::Object *)> &relocate,
        const type::Type *type) override {
        if (closure_) {
            const type::TupleType *tupleTypePtr = tupleType();
            closure_->updateRefs(relocate, tupleTypePtr);
        }
    }

  private:
    explicit Function(GIR::Graph *sourceGraph, camel::runtime::GCGraph *runtimeGraph = nullptr)
        : sourceGraph_(sourceGraph), runtimeGraph_(runtimeGraph), closure_(nullptr) {}

    // Compile-side static Function objects may only carry sourceGraph_.
    // Runtime Function objects must carry runtimeGraph_. They may optionally
    // keep sourceGraph_ as a cold metadata bridge, but runtime logic must not
    // require it.
    GIR::Graph *sourceGraph_;
    camel::runtime::GCGraph *runtimeGraph_;
    Tuple *closure_;
};
