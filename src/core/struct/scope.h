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

template <typename K, typename V> class Scope {
  public:
    using Map = typename std::conditional<hashable<K>::value, std::unordered_map<K, V>, std::map<K, V>>::type;

  protected:
    mutable std::shared_mutex rwMutex;
    Map map;
    std::shared_ptr<Scope<K, V>> outer;

  public:
    Scope() = default;

    explicit Scope(std::shared_ptr<Scope<K, V>> outer) : map(), outer(std::move(outer)) {}

    std::optional<std::reference_wrapper<V>> at(const K &k, bool recursive = true) const {
        std::shared_lock<std::shared_mutex> lock(rwMutex);
        auto it = map.find(k);
        if (it != map.end()) {
            return it->second;
        } else if (recursive && outer) {
            return (*outer).at(k, recursive);
        } else {
            return std::nullopt;
        }
    }

    void insert(const K &k, const V &v) {
        std::unique_lock<std::shared_mutex> lock(rwMutex);
        map.insert({k, v});
    }

    bool erase(const K &k, bool recursive = true) {
        std::unique_lock<std::shared_mutex> lock(rwMutex);
        auto c = map.erase(k);
        if (recursive && outer) {
            lock.unlock(); // Release the exclusive lock before calling outer->erase
            return outer->erase(k, recursive) || c;
        } else {
            return c;
        }
    }

    bool has(const K &k, bool recursive = true) const {
        std::shared_lock<std::shared_mutex> lock(rwMutex);
        if (map.count(k) != 0) {
            return true;
        } else if (recursive && outer) {
            lock.unlock(); // Release the shared lock before calling outer->has
            return outer->has(k, recursive);
        }
        return false;
    }

    bool isRoot() const { return !outer; }

    Map self() const { return map; }
};

template <typename K, typename V> using scope_ptr_t = std::shared_ptr<Scope<K, V>>;