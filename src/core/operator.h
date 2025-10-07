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
 * Created: Aug. 10, 2024
 * Updated: Oct. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/data/data.h"

#include <functional>
#include <unordered_map>

class Frame;
class Context;
class Operator;
class OperatorIndex;
class OperatorGroup;

namespace GraphIR {
class Node;
class Graph;
using node_ptr_t = std::shared_ptr<Node>;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIR

enum class OperatorReturnCode {
    OK = 0,
    Error = 1,
    NotImplemented = 2,
    InvalidArgument = 3,
    RuntimeError = 4,
};

using operator_t = std::function<OperatorReturnCode(GraphIR::node_ptr_t &, Frame &, Context &)>;

using oper_idx_ptr_t = std::shared_ptr<OperatorIndex>;
using oper_idx_vec_t = std::vector<oper_idx_ptr_t>;
using oper_idx_vec_ptr_t = std::shared_ptr<oper_idx_vec_t>;

class OperatorIndex {
  private:
    std::string name_;
    func_type_ptr_t type_;
    std::string uri_;

  public:
    OperatorIndex(const std::string &name, const func_type_ptr_t &&type, const std::string &uri)
        : name_(name), type_(std::move(type)), uri_(uri) {}

    const std::string &name() const { return name_; }
    const std::string &uri() const { return uri_; }
    const func_type_ptr_t &funcType() const { return type_; }
};

using oper_group_ptr_t = std::shared_ptr<OperatorGroup>;

class OperatorGroup {
  private:
    std::string name_;
    std::vector<std::pair<std::string, resolver_uptr_t>> resolvers_;

  public:
    OperatorGroup(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_uptr_t>> &resolvers)
        : name_(name), resolvers_(resolvers) {}
    OperatorGroup(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_uptr_t>> &&resolvers)
        : name_(name), resolvers_(std::move(resolvers)) {}

    static oper_group_ptr_t create(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_uptr_t>> &resolvers) {
        return std::make_shared<OperatorGroup>(name, resolvers);
    }
    static oper_group_ptr_t create(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_uptr_t>> &&resolvers) {
        return std::make_shared<OperatorGroup>(name, std::move(resolvers));
    }

    const std::string &name() const { return name_; }

    std::optional<OperatorIndex>
    resolve(const type_vec_t &with, const type_vec_t &norm, const ModifierSet &modifiers) const {
        for (const auto &[uri, resolver] : resolvers_) {
            auto optType = resolver->resolve(with, norm, modifiers);
            if (optType) {
                return OperatorIndex(name_, std::move(*optType), uri);
            }
        }
        return std::nullopt;
    }
};
