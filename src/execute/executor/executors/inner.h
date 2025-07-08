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

#include "../executor.h"

#pragma once

class InnerExecutor : public BaseExecutor {
  private:
    using funcType = std::function<Status(std::vector<data_ptr_t>, std::vector<data_ptr_t>, data_ptr_t &)>;
    std::unordered_map<std::string, funcType> funcMap;
    void registerFunc(std::string name, funcType func);
    // functions define here, static
  public:
    Status execute(std::string uri, std::vector<data_ptr_t> withArgs, std::vector<data_ptr_t> normArgs,
                   data_ptr_t &ret) override;
    InnerExecutor();
    ~InnerExecutor() = default;
};