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

#pragma once

#include "core/operator.h"
#include "core/type/type.h"
#include "error/runtime.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace GIR {
class Node;
using node_ptr_t = std::shared_ptr<Node>;
} // namespace GIR

class Frame;
class Context;
using context_ptr_t = std::shared_ptr<Context>;

class Executor : public std::enable_shared_from_this<Executor> {
  protected:
    context_ptr_t context_;

    std::unordered_map<std::string, operator_t> opsMap_;

  public:
    Executor(context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : context_(ctx), opsMap_(ops) {};
    virtual ~Executor() = default;

    virtual EvalResultCode eval(std::string uri, GIR::node_ptr_t &self, Frame &frame) = 0;
};

using executor_ptr_t = std::shared_ptr<Executor>;
using executor_factory_t = std::function<executor_ptr_t()>;

class ExecutorManager {
  private:
    context_ptr_t context_;
    std::unordered_map<std::string, executor_factory_t> executorFactories;
    std::unordered_map<std::string, executor_ptr_t> loadedExecutors;

  public:
    ExecutorManager(context_ptr_t ctx) : context_(ctx) {};
    ~ExecutorManager() = default;
    void registerExecutorFactory(std::string name, executor_factory_t fact);

    EvalResultCode eval(std::string uri, GIR::node_ptr_t &self, Frame &frame);
};

using exec_mgr_uptr_t = std::unique_ptr<ExecutorManager>;