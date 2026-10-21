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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/data/composite/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

using namespace std;
using namespace camel::core::data;
using namespace camel::core::type;

FunctionData::FunctionData(
    const camel::runtime::Program *program, camel::runtime::GraphId graph,
    std::vector<std::string> closureRefs)
    : CompositeData(program && program->graph(graph) ? program->graph(graph)->funcType() : nullptr),
      program_(program), graphId_(graph), closureRefs_(std::move(closureRefs)) {}

func_ptr_t FunctionData::create(
    const camel::runtime::Program *program, camel::runtime::GraphId graph,
    std::vector<std::string> closureRefs) {
    ASSERT(
        program != nullptr && program->graph(graph) != nullptr,
        "FunctionData requires a runtime graph.");
    ASSERT(
        program->graph(graph)->funcType() != nullptr,
        "Graph must have a function type for FunctionData.");
    return std::make_shared<FunctionData>(program, graph, std::move(closureRefs));
}

camel::runtime::GCGraph *FunctionData::graph() const {
    return program_ ? program_->graph(graphId_) : nullptr;
}

std::string FunctionData::name() const { return graph() ? graph()->name() : "<null>"; }

bool FunctionData::isMacro() const {
    camel::runtime::GCGraph *runtimeGraph = graph();
    return runtimeGraph != nullptr && runtimeGraph->isMacro();
}

FunctionType *FunctionData::funcType() const { return tt::as_ptr<FunctionType>(type_); }

std::vector<std::string> FunctionData::refs() const { return closureRefs_; }

std::vector<size_t> FunctionData::holes() const {
    std::vector<size_t> slots;
    slots.reserve(closureRefs_.size());
    for (size_t i = 0; i < closureRefs_.size(); ++i) {
        slots.push_back(i);
    }
    return slots;
}

bool FunctionData::resolved() const { return closureRefs_.empty() || !closure_.empty(); }

void FunctionData::resolve(const data_vec_t &dataList) {
    ASSERT(closure_.size() == 0, "FunctionData closure has already been resolved.");
    ASSERT(
        dataList.size() == closureRefs_.size(),
        std::format(
            "Cannot resolve closure of function '{}': expected {} data, got {}.",
            name(),
            closureRefs_.size(),
            dataList.size()));
    closure_.insert(closure_.end(), dataList.begin(), dataList.end());
}

bool FunctionData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t FunctionData::clone(bool deep) const {
    auto cloned = std::make_shared<FunctionData>(program_, graphId_, closureRefs_);
    if (deep) {
        cloned->closure_.reserve(closure_.size());
        for (const auto &elem : closure_) {
            cloned->closure_.push_back(elem ? elem->clone(true) : nullptr);
        }
    } else {
        cloned->closure_ = closure_;
    }
    return cloned;
}

const std::string FunctionData::toString() const {
    FunctionType *type = dynamic_cast<FunctionType *>(type_);
    return std::format(
        "{}: {} ({})",
        name(),
        type->toString(),
        strutil::join(refs(), ", ", [](const std::string &s) { return s; }));
}

data_ptr_t FunctionData::convertTo(Type *type) {
    if (type->equals(type_)) {
        return tt::as_shared<FunctionData>(shared_from_this());
    }
    return nullptr;
}
