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
 * Created: Apr. 16, 2025
 * Updated: Apr. 16, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "innerExecutor.h"

void InnerExecutor::registerFunc(std::string name, funcType func) {
    funcMap[name] = func;
}

InnerExecutor::InnerExecutor() {
}

Status InnerExecutor::execute(std::string uri, std::vector<data_ptr_t> withArgs, std::vector<data_ptr_t> normArgs, data_ptr_t &ret) {
    if (funcMap.find(uri) == funcMap.end()) {
        return {RetCode::invalidURI, "Invalid operator: " + uri};
    }
    return funcMap[uri](withArgs, normArgs, ret);
}