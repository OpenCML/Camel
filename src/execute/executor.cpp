// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/execute/executor.h"
#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/effect_trace.h"
#include "camel/core/error/diagnostics.h"
#include "camel/profile/operators.h"
#include "camel/utils/log.h"

#include <mutex>

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

bool ExecutorManager::factoryRegistered(const std::string &protocol) const {
    std::shared_lock lock(loadedExecutorsMutex);
    return executorFactories.find(protocol) != executorFactories.end();
}

void ExecutorManager::registerExecutorFactory(std::string name, executor_factory_t fact) {
    std::unique_lock lock(loadedExecutorsMutex);
    EXEC_WHEN_DEBUG(
        CAMEL_LOG_DEBUG_S("ExecMgr", "Registering executor factory for protocol: <{}>", name));
    ASSERT(
        executorFactories.find(name) == executorFactories.end(),
        "Executor factory for protocol '" + name + "' is already registered.");
    executorFactories[name] = fact;
}

std::optional<operator_t> ExecutorManager::findWithoutImport(const std::string &uri) const {
    const size_t pos = uri.find(":");
    if (pos == std::string::npos || !factoryRegistered(uri.substr(0, pos))) {
        return std::nullopt;
    }
    // The protocol has a factory: find() loads its executor at most, it imports nothing.
    return find(uri);
}

std::optional<operator_t> ExecutorManager::find(const std::string &uri) const {
    const size_t pos = uri.find(":");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    const std::string protocol = uri.substr(0, pos);
    // With a trace file set by std::profile effect_trace="...", effectful operators are handed
    // out behind a logging trampoline (camel/core/effect_trace.h); otherwise wrap() returns the
    // operator unchanged.
    auto traced = [&uri](std::optional<operator_t> op) -> std::optional<operator_t> {
        if (op && camel::core::effect_trace::enabled()) {
            return camel::core::effect_trace::wrap(uri, *op);
        }
        return op;
    };
    {
        std::shared_lock lock(loadedExecutorsMutex);
        auto itExec = loadedExecutors.find(protocol);
        if (itExec != loadedExecutors.end()) {
            return traced(itExec->second->find(uri.substr(pos + 1)));
        }
    }
    if (!factoryRegistered(protocol)) {
        // An operator of a module the program has not imported: the globals `exit` and `sleep`
        // are implemented in the process module. Importing the module registers its executor.
        static std::mutex importMutex;
        std::lock_guard importLock(importMutex);
        if (!factoryRegistered(protocol)) {
            try {
                if (!context_ || !context_->importModule(protocol)) {
                    return std::nullopt;
                }
            } catch (const camel::core::error::Diagnostic &) {
                return std::nullopt; // no such module: the operator stays unknown
            }
        }
    }
    std::unique_lock lock(loadedExecutorsMutex);
    auto itFact = executorFactories.find(protocol);
    if (itFact == executorFactories.end()) {
        return std::nullopt;
    }
    auto itExec = loadedExecutors.find(protocol);
    if (itExec == loadedExecutors.end()) {
        CAMEL_LOG_INFO_S("ExecMgr", "Loading executor for protocol <{}>", protocol);
        itExec = loadedExecutors.emplace(protocol, itFact->second()).first;
    }
    return traced(itExec->second->find(uri.substr(pos + 1)));
}

void ExecutorManager::declareBridgeSignature(std::string uri, std::string sig) const {
    bridgeSignatures.insert_or_assign(std::move(uri), std::move(sig));
}

std::optional<std::string> ExecutorManager::signatureOf(const std::string &uri) const {
    const auto it = bridgeSignatures.find(uri);
    if (it == bridgeSignatures.end()) {
        return std::nullopt;
    }
    return it->second;
}
std::string ExecutorManager::getNameOfAnOperator(const operator_t &tracedOp) const {
    const operator_t op = camel::profile::unwrapThunk(camel::core::effect_trace::unwrap(tracedOp));
    for (const auto &[protocol, executor] : loadedExecutors) {
        std::string name = executor->getNameOfAnOperator(op);
        if (!name.empty()) {
            return protocol + ":" + name;
        }
    }
    return "";
}
