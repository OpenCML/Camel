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
 * Created: Jul. 29, 2025
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include "ref.h"
#include "utils/template.h"

template <Hashable Key, typename Val> class Namespace : public std::enable_shared_from_this<Namespace<Key, Val>> {
  protected:
    using namespace_ptr_t = std::shared_ptr<Namespace<Key, Val>>;
    std::unordered_map<Key, Val> map_;
    std::unordered_map<Key, namespace_ptr_t> innerNamespaces_;

  public:
    Namespace() = default;

    bool insert(const Key &k, const Val &&v) { return map_.emplace(std::move(k), std::move(v)).second; };

    bool insert(const Reference &ref, const Val &&v)
        requires std::is_same_v<Key, std::string>
    {
        if (ref.empty())
            return false;

        auto current = this->shared_from_this();
        for (const auto &ns_name : ref.paths()) {
            auto it = current->innerNamespaces_.find(ns_name);
            if (it == current->innerNamespaces_.end()) {
                auto new_ns = std::make_shared<Namespace<Key, Val>>();
                current->innerNamespaces_[ns_name] = new_ns;
                current = new_ns;
            } else {
                current = it->second;
            }
        }

        return current->insert(ref.ident(), std::move(v));
    };

    bool insertNamespace(const Key &k, const namespace_ptr_t &ns) { return innerNamespaces_.emplace(k, ns).second; };

    std::optional<Val> find(const Reference &ref)
        requires std::is_same_v<Key, std::string>
    {
        if (!ref.empty())
            return std::nullopt;

        const Namespace *current = this;
        for (const auto &ns_name : ref.paths()) {
            auto it = current->innerNamespaces_.find(ns_name);
            if (it == current->innerNamespaces_.end())
                return std::nullopt;
            current = it->second.get();
        }

        auto valIt = current->map_.find(ref.ident());
        if (valIt != current->map_.end()) {
            return valIt->second;
        }
        return std::nullopt;
    }

    std::optional<namespace_ptr_t> findNamespace(const Reference &ref)
        requires std::is_same_v<Key, std::string>
    {
        namespace_ptr_t current = this->shared_from_this();
        for (const auto &ns_name : ref.paths()) {
            auto it = current->innerNamespaces_.find(ns_name);
            if (it == current->innerNamespaces_.end())
                return std::nullopt;
            current = it->second;
        }
        return current;
    }
};