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
 * Created: Nov. 07, 2025
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/type/composite/tuple.h"
#include "tuple.h"

// 前向声明
namespace GraphIR {
class Graph;
} // namespace GraphIR

class Function : public Object {
  public:
    Function(const Function &)            = delete;
    Function &operator=(const Function &) = delete;

    static Function *create(GraphIR::Graph *graph, const Type *tupleType, IAllocator &allocator) {
        ASSERT(tupleType && tupleType->code() == TypeCode::Tuple, "Type must be TupleType");
        const TupleType *tt = static_cast<const TupleType *>(tupleType);

        void *mem = allocator.alloc(sizeof(Function), alignof(Function));
        if (!mem)
            throw std::bad_alloc();

        auto *fn     = new (mem) Function(graph);
        fn->closure_ = Tuple::create(tt->size(), allocator);
        return fn;
    }

    GraphIR::Graph *graph() const { return graph_; }
    Tuple *tuple() { return closure_; }
    const Tuple *tuple() const { return closure_; }

    // 获取 tuple 的类型（从 graph 获取，实现在 .cpp 中）
    const TupleType *tupleType() const;

    virtual bool equals(const Object *other, const Type *type, bool deep = false) const override {
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        const Function *fnOther = reinterpret_cast<const Function *>(other);

        if (graph_ != fnOther->graph_)
            return false;

        const TupleType *tupleTypePtr = tupleType();
        if (!closure_ && !fnOther->closure_)
            return true;
        if (!closure_ || !fnOther->closure_)
            return false;

        return closure_->equals(fnOther->closure_, tupleTypePtr, deep);
    }

    virtual Object *
    clone(IAllocator &allocator, const Type *type, bool deep = false) const override {
        void *mem = allocator.alloc(sizeof(Function), alignof(Function));
        if (!mem)
            throw std::bad_alloc();

        Function *fnNew = new (mem) Function(graph_);

        if (closure_) {
            const TupleType *tupleTypePtr = tupleType();
            fnNew->closure_ =
                deep ? static_cast<Tuple *>(closure_->clone(allocator, tupleTypePtr, true))
                     : closure_;
        } else {
            fnNew->closure_ = nullptr;
        }

        return reinterpret_cast<Object *>(fnNew);
    }

    virtual void print(std::ostream &os, const Type *type) const override;

    virtual void onMoved() override {
        // graph_ 是外部引用，不需要改动
        // tuple_ 指向的对象可能被 GC 移动，需要由 GC 更新
    }

    virtual void
    updateRefs(const std::function<Object *(Object *)> &relocate, const Type *type) override {
        if (closure_) {
            const TupleType *tupleTypePtr = tupleType();
            closure_->updateRefs(relocate, tupleTypePtr);
        }
    }

  private:
    explicit Function(GraphIR::Graph *g) : graph_(g), closure_(nullptr) {}

    GraphIR::Graph *graph_;
    Tuple *closure_;
};
