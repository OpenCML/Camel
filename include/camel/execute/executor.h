// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Executor : public std::enable_shared_from_this<Executor> {
  protected:
    camel::core::context::context_ptr_t context_;

    std::unordered_map<std::string, operator_t> opsMap_;

  public:
    Executor(
        camel::core::context::context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : context_(ctx), opsMap_(ops) {};
    virtual ~Executor() = default;

    std::optional<operator_t> find(const std::string &uri);
    std::string getNameOfAnOperator(const operator_t &op);
};

using executor_ptr_t     = std::shared_ptr<Executor>;
using executor_factory_t = std::function<executor_ptr_t()>;

class ExecutorManager {
  private:
    camel::core::context::context_ptr_t context_;
    std::unordered_map<std::string, executor_factory_t> executorFactories;
    mutable std::unordered_map<std::string, executor_ptr_t> loadedExecutors;
    // find() loads an executor on the first use of its protocol, and parallel schedulers call
    // it from several workers at once. The mutex guards both maps.
    mutable std::shared_mutex loadedExecutorsMutex;
    bool factoryRegistered(const std::string &protocol) const;

  public:
    ExecutorManager(camel::core::context::context_ptr_t ctx) : context_(ctx) {};
    ~ExecutorManager() = default;
    void registerExecutorFactory(std::string name, executor_factory_t fact);

    std::optional<operator_t> find(const std::string &uri) const;
    /// find() for whoever must not import a module by asking: the operator when the executor of
    /// its protocol is loaded or registered already, nothing when finding it would import the
    /// module first (or when there is no such operator).
    std::optional<operator_t> findWithoutImport(const std::string &uri) const;
    std::string getNameOfAnOperator(const operator_t &op) const;
};

using exec_mgr_uptr_t = std::unique_ptr<ExecutorManager>;
