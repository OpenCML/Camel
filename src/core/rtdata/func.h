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
 * Updated: Dec. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "tuple.h"

namespace GraphIR {
class Graph;
} // namespace GraphIR

class Function : public Object {
  public:
    Function(const Function &)            = delete;
    Function &operator=(const Function &) = delete;

    static Function *create(
        GraphIR::Graph *graph, const TupleTypeLayout &layout,
        IAllocator &allocator = mm::autoSpace()) {
        void *mem = allocator.alloc(sizeof(Function), alignof(Function));
        if (!mem)
            throw std::bad_alloc();

        auto *fn = new (mem) Function(graph);

        // 独立创建 tuple
        fn->tuple_ = Tuple::create(layout, allocator);
        return fn;
    }

    GraphIR::Graph *graph() const { return graph_; }
    Tuple *tuple() { return tuple_; }
    const Tuple *tuple() const { return tuple_; }

    virtual bool equals(const Object *other, bool deep = false) const override {
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        const Function *fnOther = reinterpret_cast<const Function *>(other);

        if (graph_ != fnOther->graph_)
            return false;

        return tuple_->equals(fnOther->tuple_, deep);
    }

    virtual Object *clone(IAllocator &allocator, bool deep = false) const override {
        void *mem = allocator.alloc(sizeof(Function), alignof(Function));
        if (!mem)
            throw std::bad_alloc();

        Function *fnNew = new (mem) Function(graph_);

        if (tuple_) {
            fnNew->tuple_ = deep ? static_cast<Tuple *>(tuple_->clone(allocator, true)) : tuple_;
        } else {
            fnNew->tuple_ = nullptr;
        }

        return reinterpret_cast<Object *>(fnNew);
    }

    virtual void onMoved() override {
        // graph_ 是外部引用，不需要改动
        // tuple_ 指向的对象可能被 GC 移动，需要由 GC 更新
    }

    virtual void updateRefs(const std::function<Object *(Object *)> &relocate) override {
        if (tuple_) {
            tuple_->updateRefs(relocate);
        }
    }

  private:
    explicit Function(GraphIR::Graph *g) : graph_(g), tuple_(nullptr) {}

    GraphIR::Graph *graph_;
    Tuple *tuple_;
};
