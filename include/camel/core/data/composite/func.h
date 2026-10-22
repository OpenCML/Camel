// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/data/composite/composite.h"
#include "camel/core/type/composite/func.h"
#include <list>

#include "camel/runtime/graph_id.h"

namespace camel::runtime {
class GCGraph;
class Program;
}

namespace camel::core::data {

class FunctionData;

using func_ptr_t  = std::shared_ptr<FunctionData>;
using func_wptr_t = std::weak_ptr<FunctionData>;
using func_lst_t  = std::list<func_ptr_t>;
using func_vec_t  = std::vector<func_ptr_t>;
using func_list_t = std::initializer_list<func_ptr_t>;

class FunctionData : public CompositeData {
    // FunctionData is a compile-time boxed function value that already names the materialized
    // runtime graph: the graph of a program, by id. It no longer depends on compile GIR ownership.
    const camel::runtime::Program *program_;
    camel::runtime::GraphId graphId_;
    std::vector<std::string> closureRefs_;
    data_vec_t closure_;

  public:
    FunctionData(
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        std::vector<std::string> closureRefs = {});
    virtual ~FunctionData() = default;

    static func_ptr_t create(
        const camel::runtime::Program *program, camel::runtime::GraphId graph,
        std::vector<std::string> closureRefs = {});

    std::string name() const;
    camel::runtime::GCGraph *graph() const;
    const camel::runtime::Program *program() const { return program_; }
    camel::runtime::GraphId graphId() const { return graphId_; }
    type::FunctionType *funcType() const;
    bool isMacro() const;
    const data_vec_t &closure() const { return closure_; }

    virtual std::vector<std::string> refs() const override;
    virtual std::vector<size_t> holes() const override;
    virtual bool resolved() const override;
    virtual void resolve(const data_vec_t &dataList) override;

    virtual bool equals(const data_ptr_t &other) const override;
    virtual data_ptr_t clone(bool deep = false) const override;
    virtual const std::string toString() const override;
    virtual data_ptr_t convertTo(type::Type *type) override;
};

} // namespace camel::core::data
