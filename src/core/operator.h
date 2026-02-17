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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/rtdata/base.h"
#include "core/rtdata/data.h"
#include "core/type/base.h"
#include "core/type/resolver.h"
#include "utils/rawarr.h"

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
using data_idx_t  = int16_t;
using arr_size_t  = uint16_t;
using node_ptr_t  = std::shared_ptr<Node>;
using graph_ptr_t = std::shared_ptr<Graph>;
} // namespace GraphIR

using data_arr_t = RawArray<const GraphIR::data_idx_t>;

class ArgsView {
  public:
    virtual ~ArgsView()                              = default;
    virtual size_t size() const                      = 0;
    virtual slot_t slot(size_t index) const          = 0;
    virtual void setSlot(size_t index, slot_t value) = 0;
    virtual TypeCode code(size_t index) const        = 0;
    virtual Type *type(size_t index) const           = 0;

    template <typename T> T get(size_t index) const { return fromSlot<T>(slot(index)); }
    template <typename T> void set(size_t index, T value) { setSlot(index, toSlot(value)); }
};

using operator_t = slot_t (*)(ArgsView &with, ArgsView &norm, Context &ctx);

using oper_idx_ptr_t     = std::shared_ptr<OperatorIndex>;
using oper_idx_vec_t     = std::vector<oper_idx_ptr_t>;
using oper_idx_vec_ptr_t = std::shared_ptr<oper_idx_vec_t>;

class OperatorIndex {
  private:
    std::string name_;
    FunctionType *type_;
    std::string uri_;

  public:
    OperatorIndex(const std::string &name, FunctionType *type, const std::string &uri)
        : name_(name), type_(std::move(type)), uri_(uri) {}

    const std::string &name() const { return name_; }
    const std::string &uri() const { return uri_; }
    FunctionType *funcType() const { return type_; }
};

using oper_group_ptr_t = std::shared_ptr<OperatorGroup>;

class OperatorGroup {
  private:
    std::string name_;
    std::vector<std::pair<std::string, resolver_ptr_t>> resolvers_;

  public:
    OperatorGroup(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_ptr_t>> &resolvers)
        : name_(name), resolvers_(resolvers) {}
    OperatorGroup(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_ptr_t>> &&resolvers)
        : name_(name), resolvers_(std::move(resolvers)) {}

    static oper_group_ptr_t create(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_ptr_t>> &resolvers) {
        return std::make_shared<OperatorGroup>(name, resolvers);
    }
    static oper_group_ptr_t create(
        const std::string &name,
        const std::vector<std::pair<std::string, resolver_ptr_t>> &&resolvers) {
        return std::make_shared<OperatorGroup>(name, std::move(resolvers));
    }

    const std::string &name() const { return name_; }

    const auto &resolvers() const { return resolvers_; }

    std::optional<oper_idx_ptr_t>
    resolve(const type_vec_t &with, const type_vec_t &norm, const ModifierSet &modifiers) const {
        for (const auto &[uri, resolver] : resolvers_) {
            auto optType = resolver->resolve(with, norm, modifiers);
            if (optType) {
                return std::make_shared<OperatorIndex>(name_, std::move(*optType), uri);
            }
        }
        return std::nullopt;
    }
};
