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
 * Created: Apr. 09, 2024
 * Updated: Oct. 21, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

template <typename T>
concept Hashable = requires(T t) {
    { std::hash<T>{}(t) } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept HasEmpty = requires(T t) {
    { t.empty() } -> std::convertible_to<bool>;
};

template <typename T>
concept HashableAndHasEmpty = Hashable<T> && HasEmpty<T>;

template <Hashable Key, typename Val, HashableAndHasEmpty Name = std::string>
class Scope : public std::enable_shared_from_this<Scope<Key, Val, Name>> {
  protected:
    using scope_ptr_t = std::shared_ptr<Scope<Key, Val, Name>>;
    mutable std::shared_mutex rwMutex_;
    std::unordered_map<Key, Val> map_;
    scope_ptr_t outer_;
    std::unordered_map<Name, scope_ptr_t> innerScopes_;

  public:
    Scope() = default;
    Scope(std::unordered_map<Key, Val> map, std::shared_ptr<Scope<Key, Val>> outer = nullptr)
        : map_(std::move(map)), outer_(std::move(outer)) {}
    Scope(scope_ptr_t outer) : map_(), outer_(std::move(outer)) {}

    scope_ptr_t &outer() { return outer_; }
    std::vector<scope_ptr_t> innerScopes() const {
        std::vector<scope_ptr_t> scopes;
        for (const auto &pair : innerScopes_) {
            scopes.push_back(pair.second);
        }
        return scopes;
    }
    std::unordered_map<Key, Val> &map() { return map_; }

    bool has(const Key &k, bool recursive = true) const {
        // std::shared_lock<std::shared_mutex> lock(rwMutex_);
        if (map_.count(k) != 0) {
            return true;
        } else if (recursive && outer_) {
            // lock.unlock(); // Release the shared lock before calling outer->has
            return outer_->has(k, recursive);
        }
        return false;
    }

    std::optional<Val> get(const Key &k, bool recursive = true) {
        // std::shared_lock<std::shared_mutex> lock(rwMutex_);
        auto it = map_.find(k);
        if (it != map_.end()) {
            return it->second;
        } else if (recursive && outer_) {
            return (*outer_).get(k, recursive);
        } else {
            return std::nullopt;
        }
    }

    void insert(const Key &k, const Val &v) {
        // this method won't check if the key already exists
        // keep in mind that the insertion is successful
        // even if the key already exists in the outer scope
        // std::unique_lock<std::shared_mutex> lock(rwMutex_);
        map_.insert({k, v});
    }

    bool erase(const Key &k, bool recursive = true) {
        // std::unique_lock<std::shared_mutex> lock(rwMutex_);
        auto c = map_.erase(k);
        if (recursive && outer_) {
            // lock.unlock(); // Release the exclusive lock before calling outer->erase
            return outer_->erase(k, recursive) || c;
        } else {
            return c;
        }
    }

    void clear() {
        // std::unique_lock<std::shared_mutex> lock(rwMutex_);
        map_.clear();
        if (outer_) {
            // lock.unlock(); // Release the exclusive lock before calling outer->clear
            outer_->clear();
        }
    }

    bool isRoot() const { return !outer_; }

    std::unordered_map<Key, Val> self() const { return map_; }

    static scope_ptr_t create(scope_ptr_t outer = nullptr) { return std::make_shared<Scope<Key, Val, Name>>(outer); }

    static scope_ptr_t create(std::unordered_map<Key, Val> map, scope_ptr_t outer = nullptr) {
        return std::make_shared<Scope<Key, Val, Name>>(map, outer);
    }

    scope_ptr_t enter(Name name = Name()) {
        if (!name.empty()) {
            auto it = innerScopes_.find(name);
            if (it != innerScopes_.end()) {
                return it->second;
            }
            auto newScope = std::make_shared<Scope<Key, Val, Name>>(this->shared_from_this());
            innerScopes_[name] = newScope;
            return newScope;
        } else {
            return std::make_shared<Scope<Key, Val, Name>>(this->shared_from_this());
        }
    }

    scope_ptr_t leave() {
        // TODO: Shall we free the scope?
        return outer();
    }
};

template <typename Key, typename Val, typename Name = std::string>
using scope_ptr_t = std::shared_ptr<Scope<Key, Val, Name>>;
