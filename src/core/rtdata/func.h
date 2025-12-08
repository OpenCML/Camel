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
 * Updated: Dec. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "tuple.h"

namespace GraphIR {
class Graph;
} // namespace GraphIR

class GCFunction : public GCObject {
  public:
    GCFunction(const GCFunction &)            = delete;
    GCFunction &operator=(const GCFunction &) = delete;

    static GCFunction *create(
        GraphIR::Graph *graph, const TupleTypeLayout &layout,
        IAllocator &allocator = mm::autoSpace()) {
        void *mem = allocator.alloc(sizeof(GCFunction), alignof(GCFunction));
        if (!mem)
            throw std::bad_alloc();

        auto *fn = new (mem) GCFunction(graph);

        // 独立创建 tuple
        fn->tuple_ = GCTuple::create(layout, allocator);
        return fn;
    }

    GraphIR::Graph *graph() const { return graph_; }
    GCTuple *tuple() { return tuple_; }
    const GCTuple *tuple() const { return tuple_; }

    virtual GCObject *clone(IAllocator &allocator, bool deep) const override {
        void *mem = allocator.alloc(sizeof(GCFunction), alignof(GCFunction));
        if (!mem)
            throw std::bad_alloc();

        GCFunction *fn = new (mem) GCFunction(graph_);

        if (tuple_) {
            fn->tuple_ = deep ? static_cast<GCTuple *>(tuple_->clone(allocator, true)) : tuple_;
        } else {
            fn->tuple_ = nullptr;
        }

        return fn;
    }

    virtual void onMoved() override {
        // graph_ 是外部引用，不需要改动
        // tuple_ 指向的对象可能被 GC 移动，需要由 GC 更新
    }

    virtual void updateRefs(const std::function<GCRef(GCRef)> &relocate) override {
        if (tuple_) {
            tuple_->updateRefs(relocate);
        }
    }

  private:
    explicit GCFunction(GraphIR::Graph *g) : graph_(g), tuple_(nullptr) {}

    GraphIR::Graph *graph_;
    GCTuple *tuple_;
};
