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
 * Updated: Oct. 23, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "executor.h"
#include "utils/log.h"

std::optional<operator_t> Executor::find(const std::string &uri) {
    auto it = opsMap_.find(uri);
    if (it != opsMap_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string Executor::getNameOfAnOperator(const operator_t &op) {
    for (const auto &[name, func] : opsMap_) {
        if (func == op) {
            return name;
        }
    }
    return "";
}

void ExecutorManager::registerExecutorFactory(std::string name, executor_factory_t fact) {
    EXEC_WHEN_DEBUG(l.in("ExecMgr").debug("Registering executor factory for protocol: <{}>", name));
    ASSERT(
        executorFactories.find(name) == executorFactories.end(),
        "Executor factory for protocol '" + name + "' is already registered.");
    executorFactories[name] = fact;
}

OperatorReturnCode
ExecutorManager::eval(std::string uri, GraphIR::node_ptr_t &self, Frame &frame) const {
    EXEC_WHEN_DEBUG(l.in("ExecMgr").debug("Evaluating operator of URI: {}", uri));
    const size_t pos = uri.find(":");
    if (pos == std::string::npos) {
        throw CamelRuntimeException(RuntimeExceptionCode::InvalidURI, "Invalid URI format");
    }
    const std::string protocol = uri.substr(0, pos);
    auto itExec = loadedExecutors.find(protocol);
    if (itExec != loadedExecutors.end()) {
        return itExec->second->eval(uri.substr(pos + 1), self, frame);
    }
    auto itFact = executorFactories.find(protocol);
    if (itFact == executorFactories.end()) {
        throw CamelRuntimeException(
            RuntimeExceptionCode::InvalidURI,
            std::format("Protocol <{}> not found.", protocol));
    }
    EXEC_WHEN_DEBUG(l.in("ExecMgr").info("Loading executor for protocol <{}>", protocol));
    auto executor = itFact->second();
    loadedExecutors.emplace(protocol, executor);
    return executor->eval(uri.substr(pos + 1), self, frame);
}

std::optional<operator_t> ExecutorManager::find(const std::string &uri) const {
    const size_t pos = uri.find(":");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const std::string protocol = uri.substr(0, pos);
    auto itExec = loadedExecutors.find(protocol);
    if (itExec != loadedExecutors.end()) {
        return itExec->second->find(uri.substr(pos + 1));
    }
    auto itFact = executorFactories.find(protocol);
    if (itFact == executorFactories.end()) {
        return std::nullopt;
    }
    EXEC_WHEN_DEBUG(l.in("ExecMgr").info("Loading executor for protocol <{}>", protocol));
    auto executor = itFact->second();
    loadedExecutors.emplace(protocol, executor);
    return executor->find(uri.substr(pos + 1));
}

std::string ExecutorManager::getNameOfAnOperator(const operator_t &op) const {
    for (const auto &[protocol, executor] : loadedExecutors) {
        std::string name = executor->getNameOfAnOperator(op);
        if (!name.empty()) {
            return protocol + ":" + name;
        }
    }
    return "";
}
