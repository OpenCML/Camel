// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

#include "camel/utils/template.h"
#include "ref.h"

template <Hashable Key, typename Val>
class Namespace : public std::enable_shared_from_this<Namespace<Key, Val>> {
  protected:
    using namespace_ptr_t = std::shared_ptr<Namespace<Key, Val>>;
    std::unordered_map<Key, Val> map_;
    std::unordered_map<Key, namespace_ptr_t> innerNamespaces_;

    void forEachImpl(
        std::vector<std::string> &pathStack,
        const std::function<void(const Reference &, const Val &)> &visitor) const {
        for (const auto &[key, val] : map_) {
            visitor(Reference(pathStack, key), val);
        }

        for (const auto &[nsName, nsPtr] : innerNamespaces_) {
            if (nsPtr) {
                pathStack.push_back(nsName);
                // The const overload: through the pointer both would be viable.
                static_cast<const Namespace &>(*nsPtr).forEachImpl(pathStack, visitor);
                pathStack.pop_back();
            }
        }
    }

    void forEachImpl(
        std::vector<std::string> &pathStack,
        const std::function<void(const Reference &, Val &)> &visitor) {
        for (auto &[key, val] : map_) {
            visitor(Reference(pathStack, key), val);
        }

        for (auto &[nsName, nsPtr] : innerNamespaces_) {
            if (nsPtr) {
                pathStack.push_back(nsName);
                nsPtr->forEachImpl(pathStack, visitor);
                pathStack.pop_back();
            }
        }
    }

  public:
    Namespace() = default;

    bool insert(const Key &k, const Val &v) { return map_.emplace(k, v).second; };

    bool insert(const Reference &ref, const Val &v)
        requires std::is_same_v<Key, std::string>
    {
        if (ref.empty())
            return false;

        auto current = this->shared_from_this();
        for (const auto &ns_name : ref.paths()) {
            auto it = current->innerNamespaces_.find(ns_name);
            if (it == current->innerNamespaces_.end()) {
                auto new_ns                        = std::make_shared<Namespace<Key, Val>>();
                current->innerNamespaces_[ns_name] = new_ns;
                current                            = new_ns;
            } else {
                current = it->second;
            }
        }

        return current->insert(ref.ident(), v);
    };

    /// Inserts `v` under the reference, replacing a value already there: the later write wins
    /// (a `.cml` beside a `.cmo` writes the signature of an operator the C++ module exported
    /// too; design principles 6.4).
    bool assign(const Reference &ref, const Val &v)
        requires std::is_same_v<Key, std::string>
    {
        if (ref.empty())
            return false;

        auto current = this->shared_from_this();
        for (const auto &ns_name : ref.paths()) {
            auto it = current->innerNamespaces_.find(ns_name);
            if (it == current->innerNamespaces_.end()) {
                auto new_ns                        = std::make_shared<Namespace<Key, Val>>();
                current->innerNamespaces_[ns_name] = new_ns;
                current                            = new_ns;
            } else {
                current = it->second;
            }
        }

        current->map_[ref.ident()] = v;
        return true;
    };

    void insertAllFrom(const Namespace<Key, Val> &other, bool deepCopy = true) {
        for (const auto &[key, val] : other.map_) {
            map_[key] = val;
        }

        for (const auto &[key, otherNsPtr] : other.innerNamespaces_) {
            if (!otherNsPtr)
                continue;

            auto &targetNsPtr = innerNamespaces_[key];

            if (deepCopy) {
                if (!targetNsPtr) {
                    targetNsPtr = std::make_shared<Namespace>();
                }
                targetNsPtr->insertAllFrom(*otherNsPtr, true);
            } else {
                targetNsPtr = otherNsPtr;
            }
        }
    }

    std::optional<Val> get(const Reference &ref)
        requires std::is_same_v<Key, std::string>
    {
        if (ref.empty()) {
            return std::nullopt;
        }

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

    bool has(const Reference &ref)
        requires std::is_same_v<Key, std::string>
    {
        if (ref.empty()) {
            return false;
        }

        const Namespace *current = this;
        for (const auto &ns_name : ref.paths()) {
            auto it = current->innerNamespaces_.find(ns_name);
            if (it == current->innerNamespaces_.end())
                return false;
            current = it->second.get();
        }

        if (current->map_.find(ref.ident()) != current->map_.end()) {
            return true;
        }

        return false;
    };

    void forEach(const std::function<void(const Reference &, const Val &)> &visitor) const
        requires std::is_same_v<Key, std::string>
    {
        std::vector<std::string> currentPath;
        forEachImpl(currentPath, visitor);
    }

    void forEach(const std::function<void(const Reference &, Val &)> &visitor)
        requires std::is_same_v<Key, std::string>
    {
        std::vector<std::string> currentPath;
        forEachImpl(currentPath, visitor);
    }
};
