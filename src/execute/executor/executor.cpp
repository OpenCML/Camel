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
#include "utils/log.h"

void ExecutorManager::registerExecutorFactory(std::string name, executor_factory_t fact) {
    l.in("ExecMgr").debug("Registering executor factory for protocol: <{}>", name);
    ASSERT(
        executorFactories.find(name) == executorFactories.end(),
        "Executor factory for protocol '" + name + "' is already registered.");
    executorFactories[name] = fact;
}

EvalResultCode ExecutorManager::eval(std::string uri, GIR::node_ptr_t &self, Frame &frame) {
    l.in("ExecMgr").debug("Evaluating operator of URI: {}", uri);
    const size_t pos = uri.find(":");
    if (pos == std::string::npos) {
        throw CamelRuntimeException(RetCode::InvalidURI, "Invalid URI format");
    }
    const std::string protocol = uri.substr(0, pos);
    auto itExec = loadedExecutors.find(protocol);
    if (itExec != loadedExecutors.end()) {
        return itExec->second->eval(uri.substr(pos + 1), self, frame);
    }
    auto itFact = executorFactories.find(protocol);
    if (itFact == executorFactories.end()) {
        throw CamelRuntimeException(
            RetCode::InvalidURI,
            std::format("Protocol <{}> not found.", protocol));
    }
    l.in("ExecMgr").info("Loading executor for protocol <{}>", protocol);
    auto executor = itFact->second();
    loadedExecutors.emplace(protocol, executor);
    return executor->eval(uri.substr(pos + 1), self, frame);
}