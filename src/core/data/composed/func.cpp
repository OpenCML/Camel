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

#include "func.h"
#include "compile/gir.h"

using namespace std;

FunctionData::FunctionData(GraphIR::Graph &graph) : ComposedData(graph.funcType()), graph_(graph) {}

func_ptr_t FunctionData::create(GraphIR::Graph &graph) {
    ASSERT(graph.funcType() != nullptr, "Graph must have a function type for FunctionData.");
    return std::make_shared<FunctionData>(graph);
}

std::string FunctionData::name() const { return graph_.name(); }

func_type_ptr_t FunctionData::funcType() const { return dynamic_pointer_cast<FunctionType>(type_); }

std::vector<std::string> FunctionData::refs() const {
    std::vector<std::string> refNames;
    for (const auto &node : graph_.closure()) {
        const auto &portNode = tt::as_shared<GraphIR::PortNode>(node);
        refNames.push_back(portNode->name());
    }
    return refNames;
}

bool FunctionData::resolved() const { return graph_.closure().empty() || !closure_.empty(); }

void FunctionData::resolve(const data_vec_t &dataList) {
    ASSERT(
        dataList.size() == graph_.closure().size(),
        "Data list size does not match closure size.");
    closure_.clear();
    closure_.insert(closure_.end(), dataList.begin(), dataList.end());
}

bool FunctionData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t FunctionData::convert(type_ptr_t target, bool inplace) {
    if (target->code() == TypeCode::Function) {
        // TODO: check function type compatibility
        return shared_from_this();
    }
    throw DataConvError("Cannot convert functor to " + typeCodeToString(target->code()));
}

data_ptr_t FunctionData::clone(bool deep) const { return std::make_shared<FunctionData>(graph_); }

const std::string FunctionData::toString() const {
    FunctionType *type = dynamic_cast<FunctionType *>(type_.get());
    return std::format(
        "{}: {} ({})",
        graph_.name(),
        type->toString(),
        strutil::join(refs(), ", ", [](const std::string &s) { return s; }));
}

void FunctionData::print(std::ostream &os) const { os << toString(); }
