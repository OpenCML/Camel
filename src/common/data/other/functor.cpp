/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 21, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "functor.h"
#include "common/graph.h"

using namespace std;
using namespace GraphIR;

FunctorData::FunctorData(const type_ptr_t &type, graph_ptr_t graph) : Data(type), graph_(graph) {};

GraphIR::graph_ptr_t FunctorData::graph() const { return graph_; }

std::string FunctorData::name() const {
    auto func = dynamic_pointer_cast<FunctorType>(type_);
    return func->name();
}

func_type_ptr_t FunctorData::funcType() const {
    return dynamic_pointer_cast<FunctorType>(type_);
}

bool FunctorData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t FunctorData::convert(type_ptr_t target, bool inplace) {
    throw DataConvError("Cannot convert functor to " + typeCodeToString(target->code()));
}

data_ptr_t FunctorData::clone(bool deep) const {
    auto newGraph = make_shared<Graph>(*graph_);
    return std::make_shared<FunctorData>(type_, newGraph);
}

const std::string FunctorData::toString() const {
    FunctorType *type = dynamic_cast<FunctorType *>(type_.get());
    return "Functor<" + type->toString() + ">";
}
