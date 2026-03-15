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
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/tuple.h"
#include "tuple.h"

// 前向声明
namespace camel::compile::gir {
class Graph;
} // namespace camel::compile::gir

namespace GIR = camel::compile::gir;

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Function : public rtdata::Object {
  public:
    Function(const Function &)            = delete;
    Function &operator=(const Function &) = delete;

    static Function *
    create(GIR::Graph *graph, const type::Type *tupleType, camel::core::mm::IAllocator &allocator) {
        ASSERT(tupleType && tupleType->code() == type::TypeCode::Tuple, "Type must be TupleType");
        const type::TupleType *tt = static_cast<const type::TupleType *>(tupleType);

        void *mem = allocator.alloc(sizeof(Function), alignof(Function));
        if (!mem)
            throw std::bad_alloc();

        auto *fn     = new (mem) Function(graph);
        fn->closure_ = Tuple::create(tt->size(), allocator);
        return fn;
    }

    GIR::Graph *graph() const { return graph_; }
    Tuple *tuple() { return closure_; }
    const Tuple *tuple() const { return closure_; }

    // 获取 tuple 的类型（从 graph 获取，实现在 .cpp 中）
    const type::TupleType *tupleType() const;

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        const Function *fnOther = reinterpret_cast<const Function *>(other);

        if (graph_ != fnOther->graph_)
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

        Function *fnNew = new (mem) Function(graph_);

        if (closure_) {
            const type::TupleType *tupleTypePtr = tupleType();
            fnNew->closure_ =
                deep ? static_cast<Tuple *>(closure_->clone(allocator, tupleTypePtr, true))
                     : closure_;
        } else {
            fnNew->closure_ = nullptr;
        }

        return reinterpret_cast<rtdata::Object *>(fnNew);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override;

    virtual void onMoved() override {
        // graph_ 是外部引用，不需要改动
        // tuple_ 指向的对象可能被 GC 移动，需要由 GC 更新
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
    explicit Function(GIR::Graph *g) : graph_(g), closure_(nullptr) {}

    // graph_ 逃逸路径：运行时 Function 持有 Graph 裸指针。
    // 存活约束：Function 在 VM 栈帧中被创建，其生命周期不超过执行期间。
    // 图的 shared_ptr 由调度遍根持有，保证执行期间 Graph 不被销毁。
    GIR::Graph *graph_;
    Tuple *closure_;
};
