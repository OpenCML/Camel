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

#include "executor.h"
#include "executors/inner.h"

data_ptr_t
ExecutorManager::execute(std::string uri, const data_vec_t withArgs, const data_vec_t normArgs) {
    const size_t pos = uri.find("://");
    if (pos == std::string::npos) {
        throw CamelRuntimeException(RetCode::InvalidURI, "Invalid URI format");
    }
    const std::string protocol = uri.substr(0, pos);
    auto it_loaded = loadedExecutors.find(protocol);
    if (it_loaded != loadedExecutors.end()) {
        return it_loaded->second->execute(uri.substr(pos + 3), withArgs, normArgs, ret);
    }
    auto it_creator = executorCreators.find(protocol);
    if (it_creator == executorCreators.end()) {
        throw CamelRuntimeException(RetCode::InvalidURI, "Unregistered protocol: " + protocol);
    }
    auto executor = it_creator->second();
    Status status = executor->execute(uri.substr(pos + 3), withArgs, normArgs, ret);
    loadedExecutors.emplace(protocol, std::move(executor));
    return status;
}

data_ptr_t
Executor::execute(std::string uri, const data_vec_t &withArgs, const data_vec_t &normArgs) {
    return {RetCode::UnknownError, "BaseExecutor: No implementation for execute"};
}