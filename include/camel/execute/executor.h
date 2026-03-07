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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <functional>
#include <memory>
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

    virtual void eval(std::string uri, GIR::Node *self, camel::core::context::Frame &frame) = 0;
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

  public:
    ExecutorManager(camel::core::context::context_ptr_t ctx) : context_(ctx) {};
    ~ExecutorManager() = default;
    void registerExecutorFactory(std::string name, executor_factory_t fact);

    void eval(std::string uri, GIR::Node *self, camel::core::context::Frame &frame) const;
    std::optional<operator_t> find(const std::string &uri) const;
    std::string getNameOfAnOperator(const operator_t &op) const;
};

using exec_mgr_uptr_t = std::unique_ptr<ExecutorManager>;
