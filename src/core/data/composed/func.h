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
 * Updated: Oct. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composed.h"

class Frame;
using frame_ptr_t = std::shared_ptr<Frame>;

namespace GraphIR {
class Graph;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIR

class FunctionData;

using func_ptr_t = std::shared_ptr<FunctionData>;
using func_wptr_t = std::weak_ptr<FunctionData>;
using func_lst_t = std::list<func_ptr_t>;
using func_vec_t = std::vector<func_ptr_t>;
using func_list_t = std::initializer_list<func_ptr_t>;

class FunctionData : public ComposedData {
    GraphIR::Graph &graph_;
    data_vec_t closure_;

  public:
    FunctionData(GraphIR::Graph &graph);
    virtual ~FunctionData() = default;

    static func_ptr_t create(GraphIR::Graph &graph);

    std::string name() const;
    GraphIR::Graph &graph() const { return graph_; }
    // frame_ptr_t closure() const { return closure_; }
    func_type_ptr_t funcType() const;

    virtual std::vector<std::string> refs() const override;
    virtual bool resolved() const override;
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t convert(type_ptr_t target, bool inplace = false) override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual void print(std::ostream &os) const override;
};
