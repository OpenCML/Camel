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
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../data.h"

namespace GraphIntermediateRepresentation {
class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIntermediateRepresentation

namespace GIR = GraphIntermediateRepresentation;

class FunctionData : public Data {
    GIR::graph_ptr_t baseGraph_;
    GIR::graph_ptr_t thisGraph_;

  public:
    FunctionData(const type_ptr_t &type, GIR::graph_ptr_t graph, GIR::graph_ptr_t base = nullptr);
    virtual ~FunctionData() = default;

    GIR::graph_ptr_t graph() const;
    GIR::graph_ptr_t baseGraph() const;
    std::string name() const;
    func_type_ptr_t funcType() const;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
};

using func_ptr_t = std::shared_ptr<FunctionData>;
using func_wptr_t = std::weak_ptr<FunctionData>;
using func_lst_t = std::list<func_ptr_t>;
using func_vec_t = std::vector<func_ptr_t>;
using func_list_t = std::initializer_list<func_ptr_t>;
