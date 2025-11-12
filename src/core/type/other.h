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
 * Created: Oct. 11, 2024
 * Updated: Nov. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

class OtherTypeRegistry {
  public:
    static TypeCode registerType(const std::string &typeName, TypeFlag flags) {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = registry.find(typeName);
        if (it != registry.end()) {
            return it->second;
        }

        uint32_t id = counter.fetch_add(1);
        if (id >= 0x0FFFFFFF) {
            throw std::runtime_error("Too many Other types registered!");
        }

        TypeCode newCode   = static_cast<TypeCode>(makeTypeCode(TypeFlag::OtherType | flags, id));
        registry[typeName] = newCode;
        reverseRegistry[newCode] = typeName;
        return newCode;
    }

    static std::string getTypeName(TypeCode code) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = reverseRegistry.find(code);
        if (it != reverseRegistry.end()) {
            return it->second;
        }
        throw std::runtime_error("TypeCode not registered!");
    }

  private:
    static std::unordered_map<std::string, TypeCode> registry;
    static std::unordered_map<TypeCode, std::string> reverseRegistry;
    static std::mutex mtx;
    static std::atomic<uint32_t> counter;
};

class OtherType : public Type {
  public:
    OtherType() = delete;
    OtherType(TypeCode code) : Type(code) {}
    virtual ~OtherType() noexcept = default;

    std::string typeName() const;

    virtual std::string toString() const override                     = 0;
    virtual std::string mangle() const override                       = 0;
    virtual type_ptr_t clone() const override                         = 0;
    virtual bool equals(const type_ptr_t &type) const override        = 0;
    virtual CastSafety castSafetyTo(const Type &other) const override = 0;
    virtual bool assignable(const type_ptr_t &type) const override    = 0;
};
