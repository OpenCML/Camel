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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/tuple.h"
#include "tuple.h"

namespace camel::runtime {
class GCGraph;
} // namespace camel::runtime

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Function : public rtdata::Object {
  public:
    Function(const Function &)            = delete;
    Function &operator=(const Function &) = delete;

    static Function *create(
        camel::runtime::GCGraph *graph, const type::Type *tupleType,
        camel::core::mm::IAllocator &allocator);

    // Runtime identity always flows through the materialized GCGraph carrier.
    camel::runtime::GCGraph *graph() const { return runtimeGraph_; }
    camel::runtime::GCGraph *runtimeGraph() const { return runtimeGraph_; }
    bool hasRuntimeGraph() const { return runtimeGraph_ != nullptr; }
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

        Function *fnNew = new (mem) Function(runtimeGraph_);

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
    explicit Function(camel::runtime::GCGraph *runtimeGraph)
        : runtimeGraph_(runtimeGraph), closure_(nullptr) {}

    camel::runtime::GCGraph *runtimeGraph_;
    Tuple *closure_;
};
