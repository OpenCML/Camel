/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 9, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

template <typename T, typename = std::void_t<>> struct hashable : std::false_type {};

template <typename T>
struct hashable<T, std::void_t<decltype(std::declval<std::hash<T>>()(std::declval<T>()))>> : std::true_type {};

template <typename K, typename V> class Scope : public std::enable_shared_from_this<Scope<K, V>> {
  protected:
    mutable std::shared_mutex rwMutex_;
    std::unordered_map<K, V> map_;
    std::shared_ptr<Scope<K, V>> outer_;

  public:
    Scope() = default;

    explicit Scope(std::shared_ptr<Scope<K, V>> outer) : map_(), outer_(std::move(outer)) {}

    std::shared_ptr<Scope<K, V>> outer() { return outer_; }

    std::optional<V> at(const K &k, bool recursive = true) {
        // std::shared_lock<std::shared_mutex> lock(rwMutex_);
        auto it = map_.find(k);
        if (it != map_.end()) {
            return it->second;
        } else if (recursive && outer_) {
            return (*outer_).at(k, recursive);
        } else {
            return std::nullopt;
        }
    }

    void insert(const K &k, const V &v) {
        // std::unique_lock<std::shared_mutex> lock(rwMutex_);
        map_.insert({k, v});
    }

    bool erase(const K &k, bool recursive = true) {
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

    bool has(const K &k, bool recursive = true) const {
        // std::shared_lock<std::shared_mutex> lock(rwMutex_);
        if (map_.count(k) != 0) {
            return true;
        } else if (recursive && outer_) {
            // lock.unlock(); // Release the shared lock before calling outer->has
            return outer_->has(k, recursive);
        }
        return false;
    }

    bool isRoot() const { return !outer_; }

    std::unordered_map<K, V> self() const { return map_; }

    static std::shared_ptr<Scope<K, V>> create(std::shared_ptr<Scope<K, V>> outer = nullptr) {
        return std::make_shared<Scope<K, V>>(outer);
    }

    static std::shared_ptr<Scope<K, V>> push(std::shared_ptr<Scope<K, V>> target) {
        return std::make_shared<Scope<K, V>>(target);
    }

    static std::shared_ptr<Scope<K, V>> pop(std::shared_ptr<Scope<K, V>> target) {
        // TODO: Shall we free the scope?
        return target->outer();
    }
};

template <typename K, typename V> using scope_ptr_t = std::shared_ptr<Scope<K, V>>;