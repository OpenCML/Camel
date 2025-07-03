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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>
#include <vector>

class Ref {
  public:
    Ref(const std::string &name) : name_(name) {}
    const std::string &name() const { return name_; }
    const std::vector<std::string> &scope() const { return scope_; }
    void addScope(const std::string &scope) {
        if (scope_.empty() || scope_ != std::vector<std::string>{scope}) {
            scope_.push_back(scope);
        }
    }

    const std::string toString() const {
        std::string result;
        if (!scope_.empty()) {
            for (const auto &s : scope_) {
                result += s + "::";
            }
        }
        result += name_;
        return result;
    }

    operator std::string() const { return toString(); }

  private:
    std::vector<std::string> scope_;
    std::string name_;
};