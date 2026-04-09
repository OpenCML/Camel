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
 * Created: Oct. 08, 2024
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/data/composite/composite.h"
#include "camel/core/type/composite/func.h"
#include <list>

namespace camel::compile::gir {
class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace camel::compile::gir

namespace GIR = camel::compile::gir;

namespace camel::core::data {

class FunctionData;

using func_ptr_t  = std::shared_ptr<FunctionData>;
using func_wptr_t = std::weak_ptr<FunctionData>;
using func_lst_t  = std::list<func_ptr_t>;
using func_vec_t  = std::vector<func_ptr_t>;
using func_list_t = std::initializer_list<func_ptr_t>;

class FunctionData : public CompositeData {
    // graph_ escape path: FunctionData holds a reference to the Graph.
    // Liveness constraint: FunctionData is indirectly held by a Graph as an element of
    // staticDataArr_, or remapped to the new graph by remapDataGraphRefs during cloneGraph. As long
    // as the Graph holding the FunctionData is alive, the referenced target graph is alive.
    GIR::Graph &graph_;
    data_vec_t closure_;

  public:
    FunctionData(GIR::Graph &graph);
    virtual ~FunctionData() = default;

    static func_ptr_t create(GIR::Graph &graph);

    std::string name() const;
    GIR::Graph &graph() const { return graph_; }
    type::FunctionType *funcType() const;
    bool isMacro() const { return funcType() && funcType()->modifiers().macro(); }
    const data_vec_t &closure() const { return closure_; }

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override;
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

} // namespace camel::core::data
