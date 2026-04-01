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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/execute/executor.h"
#include "camel/compile/gir.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/utils/log.h"

using namespace camel::core::error;
using namespace camel::core::context;

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
    EXEC_WHEN_DEBUG(
        CAMEL_LOG_DEBUG_S("ExecMgr", "Registering executor factory for protocol: <{}>", name));
    ASSERT(
        executorFactories.find(name) == executorFactories.end(),
        "Executor factory for protocol '" + name + "' is already registered.");
    executorFactories[name] = fact;
}

void ExecutorManager::eval(std::string uri, GIR::Node *self, Frame &frame) const {
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S("ExecMgr", "Evaluating operator of URI: {}", uri));
    const size_t pos = uri.find(":");
    if (pos == std::string::npos) {
        throw DiagnosticBuilder::of(SemanticDiag::InvalidOperatorURI).commit(uri);
    }
    const std::string protocol = uri.substr(0, pos);
    auto itExec                = loadedExecutors.find(protocol);
    if (itExec != loadedExecutors.end()) {
        itExec->second->eval(uri.substr(pos + 1), self, frame);
        return;
    }
    auto itFact = executorFactories.find(protocol);
    if (itFact == executorFactories.end()) {
        throw DiagnosticBuilder::of(SemanticDiag::UnrecognizedExecutorProtocol).commit(protocol);
    }
    CAMEL_LOG_INFO_S("ExecMgr", "Loading executor for protocol <{}>", protocol);
    auto executor = itFact->second();
    loadedExecutors.emplace(protocol, executor);
    executor->eval(uri.substr(pos + 1), self, frame);
}

std::optional<operator_t> ExecutorManager::find(const std::string &uri) const {
    const size_t pos = uri.find(":");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const std::string protocol = uri.substr(0, pos);
    auto itExec                = loadedExecutors.find(protocol);
    if (itExec != loadedExecutors.end()) {
        return itExec->second->find(uri.substr(pos + 1));
    }
    auto itFact = executorFactories.find(protocol);
    if (itFact == executorFactories.end()) {
        return std::nullopt;
    }
    CAMEL_LOG_INFO_S("ExecMgr", "Loading executor for protocol <{}>", protocol);
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
