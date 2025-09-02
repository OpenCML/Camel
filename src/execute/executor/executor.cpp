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

void ExecutorManager::registerExecutorCreator(
    std::string name, ExecutorManager::creator_t creator) {
    executorCreators.emplace(name, std::move(creator));
}

ExecutorManager::ExecutorManager() {
    registerExecutorCreator("inner", []() { return std::make_unique<InnerExecutor>(); });
}

Status ExecutorManager::executeOperator(
    std::string uri, std::vector<data_ptr_t> withArgs, std::vector<data_ptr_t> normArgs,
    data_ptr_t &ret) {
    const size_t pos = uri.find("://");
    if (pos == std::string::npos) {
        return {RetCode::invalidURI, "Invalid URI format"};
    }
    const std::string protocol = uri.substr(0, pos);
    auto it_loaded = loadedExecutors.find(protocol);
    if (it_loaded != loadedExecutors.end()) {
        return it_loaded->second->execute(uri.substr(pos + 3), withArgs, normArgs, ret);
    }
    auto it_creator = executorCreators.find(protocol);
    if (it_creator == executorCreators.end()) {
        return {RetCode::invalidURI, "Unregistered protocol: " + protocol};
    }
    auto executor = it_creator->second();
    Status status = executor->execute(uri.substr(pos + 3), withArgs, normArgs, ret);
    loadedExecutors.emplace(protocol, std::move(executor));
    return status;
}

Status BaseExecutor::execute(
    std::string uri, std::vector<data_ptr_t> withArgs, std::vector<data_ptr_t> normArgs,
    data_ptr_t &ret) {
    return {RetCode::unknownError, "BaseExecutor: No implementation for execute"};
}