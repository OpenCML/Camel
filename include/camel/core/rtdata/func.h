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
 * Updated: Oct. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/tuple.h"
#include "tuple.h"

#include <new>

#include "camel/runtime/graph_id.h"

namespace camel::runtime {
class GCGraph;
class Program;
} // namespace camel::runtime

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Function : public rtdata::Object {
  public:
    Function(const Function &)            = delete;
    Function &operator=(const Function &) = delete;

    /// A function value of the graph `graph` of `program` with an empty closure of the layout
    /// `tupleType`.
    static Function *create(
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        const type::Type *tupleType, camel::core::mm::IAllocator &allocator);

    /// The graph the function value is of: the version `program()` holds of the graph
    /// `graphId()`. Two function values are of the same graph when their ids are equal.
    camel::runtime::GCGraph *graph() const;
    camel::runtime::GraphId graphId() const { return graphId_; }
    const camel::runtime::Program *program() const { return program_; }
    /// Makes the function value one of the graph `graph` of `program`.
    void rebind(const camel::runtime::Program *program, camel::runtime::GraphId graph);
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

        if (graphId_ != fnOther->graphId_)
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

        Function *fnNew = new (mem) Function(program_, graphId_, tupleType_);

        if (closure_) {
            const type::TupleType *tupleTypePtr = tupleType();
            fnNew->closure_ = static_cast<Tuple *>(closure_->clone(allocator, tupleTypePtr, deep));
            camel::core::mm::writeBarrier(fnNew, type, fnNew->closure_, tupleTypePtr);
        } else {
            fnNew->closure_ = nullptr;
        }

        return reinterpret_cast<rtdata::Object *>(fnNew);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override;

    virtual void onMoved() override {
        // The program is a non-owning reference. Closure objects are updated through
        // updateRefs when the GC relocates traced objects.
    }

    virtual void
    updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;

  private:
    Function(
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        const type::TupleType *tupleType)
        : program_(program), graphId_(graph), closure_(nullptr), tupleType_(tupleType) {}

    const camel::runtime::Program *program_;
    camel::runtime::GraphId graphId_;
    Tuple *closure_;
    const type::TupleType *tupleType_ = nullptr;
};
