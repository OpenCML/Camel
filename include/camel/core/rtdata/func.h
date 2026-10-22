// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

    /// The closure layout this function was created with. Unlike tupleType() it does not
    /// require the graph to be bound in the program: a draft group reserves a graph id and
    /// binds it when it encodes the group, while a function value it puts in a static slot
    /// already names that id (runtime/draft_session.cpp collects those references before the
    /// bind). A live function has both; only the encoder is between the two.
    const type::TupleType *closureType() const { return tupleType_; }

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
