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

#include "common/type.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class RetCode {
    success,
    invalidWithParameter,
    invalidnormParameter,
    invalidURI,
    errorOnExecution,
    unknownError
};

struct Status {
    RetCode code;
    std::string errorMessage;
};

class BaseExecutor {
  public:
    std::string executorName = "";
    virtual Status execute(
        std::string uri, std::vector<data_ptr_t> withArgs, std::vector<data_ptr_t> normArgs,
        data_ptr_t &ret);
    virtual ~BaseExecutor() = default;
};

class ExecutorManager {
  private:
    using creator_t = std::function<std::unique_ptr<BaseExecutor>()>;
    std::unordered_map<std::string, std::unique_ptr<BaseExecutor>> loadedExecutors;
    std::unordered_map<std::string, creator_t> executorCreators;
    void registerExecutorCreator(std::string name, creator_t creator);

  public:
    Status executeOperator(
        std::string uri, std::vector<data_ptr_t> withArgs, std::vector<data_ptr_t> normArgs,
        data_ptr_t &ret);
    ExecutorManager();
    ~ExecutorManager() = default;
};