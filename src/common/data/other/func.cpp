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
 * Updated: Mar. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "func.h"
#include "common/graph.h"

using namespace std;
using namespace GIR;

func_type_ptr_t FunctionData::funcType() const { return dynamic_pointer_cast<FunctionType>(type_); }

bool FunctionData::equals(const data_ptr_t &other) const { return true; }

data_ptr_t FunctionData::convert(type_ptr_t target, bool inplace) {
    throw DataConvError("Cannot convert functor to " + typeCodeToString(target->code()));
}

data_ptr_t FunctionData::clone(bool deep) const { return std::make_shared<FunctionData>(type_, graph_); }

const std::string FunctionData::toString() const {
    FunctionType *type = dynamic_cast<FunctionType *>(type_.get());
    return "Function<" + type->toString() + ">";
}
