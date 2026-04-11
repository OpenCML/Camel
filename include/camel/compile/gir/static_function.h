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

#pragma once

#include "camel/core/rtdata/tuple.h"

namespace camel::compile::gir {

class Graph;

/*
 * Compile-time static function carrier.
 *
 * This object is intentionally GIR-local. It exists only inside compile-time
 * graph static slots so rewrite and validation logic can keep using normal
 * GC-traced object graphs while runtime `::Function` stays a pure GCGraph
 * closure carrier.
 */
class StaticFunction : public camel::core::rtdata::Object {
  public:
    StaticFunction(const StaticFunction &)            = delete;
    StaticFunction &operator=(const StaticFunction &) = delete;

    static StaticFunction *create(
        Graph *graph, const camel::core::type::Type *tupleType,
        camel::core::mm::IAllocator &allocator);

    Graph *graph() const { return graph_; }
    void setGraph(Graph *graph) {
        ASSERT(graph != nullptr, "Compile-time static function graph cannot be null.");
        graph_ = graph;
    }
    ::Tuple *tuple() { return closure_; }
    const ::Tuple *tuple() const { return closure_; }
    const camel::core::type::TupleType *tupleType() const;

    bool equals(
        const camel::core::rtdata::Object *other, const camel::core::type::Type *type,
        bool deep = false) const override;
    camel::core::rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *type,
        bool deep = false) const override;
    void print(std::ostream &os, const camel::core::type::Type *type) const override;
    void onMoved() override {}
    void updateRefs(
        const std::function<camel::core::rtdata::Object *(camel::core::rtdata::Object *)> &relocate,
        const camel::core::type::Type *type) override;

  private:
    explicit StaticFunction(Graph *graph) : graph_(graph), closure_(nullptr) {}

    Graph *graph_     = nullptr;
    ::Tuple *closure_ = nullptr;
};

} // namespace camel::compile::gir
