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

#include "common/error/runtime.h"
#include "common/operator.h"
#include "common/type.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Executor {
  public:
    std::string executorName = "";
    virtual data_ptr_t execute(std::string uri, data_vec_t &withArgs, data_vec_t &normArgs);
    virtual ~Executor() = default;
};

using executor_ptr_t = std::shared_ptr<Executor>;

class ExecutorManager {
  private:
    std::unordered_map<std::string, executor_ptr_t> loadedExecutors;
    void registerExecutor(std::string name, executor_ptr_t exec);

  public:
    ExecutorManager();
    ~ExecutorManager() = default;

    data_ptr_t execute(std::string uri, data_vec_t &withArgs, data_vec_t &normArgs);
};