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
    virtual data_ptr_t
    execute(std::string uri, const data_vec_t &withArgs, const data_vec_t &normArgs);
    virtual ~Executor() = default;
};

class ExecutorManager {
  private:
    using creator_t = std::function<std::unique_ptr<Executor>()>;
    std::unordered_map<std::string, std::unique_ptr<Executor>> loadedExecutors;
    std::unordered_map<std::string, creator_t> executorCreators;
    void registerExecutorCreator(std::string name, creator_t creator);

  public:
    data_ptr_t
    executeOperator(std::string uri, const data_vec_t withArgs, const data_vec_t normArgs);
    ExecutorManager();
    ~ExecutorManager() = default;
};