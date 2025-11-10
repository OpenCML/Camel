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
 * Updated: Nov. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../base.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

class OtherTypeRegistry {
  public:
    static TypeCode registerType(const std::string &typeName) {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = registry.find(typeName);
        if (it != registry.end()) {
            return static_cast<TypeCode>(0xF0000000 | it->second);
        }

        uint32_t id = counter.fetch_add(1);
        if (id >= 0x0FFFFFFF) {
            throw std::runtime_error("Too many Other types registered!");
        }

        registry[typeName]  = id;
        reverseRegistry[id] = typeName;
        return static_cast<TypeCode>(makeTypeCode(TypeFlag::OtherType, id));
    }

    static std::string getTypeName(TypeCode code) {
        uint32_t id = static_cast<uint32_t>(code) & static_cast<uint32_t>(0x0FFFFFFF);

        std::lock_guard<std::mutex> lock(mtx);
        auto it = reverseRegistry.find(id);
        if (it != reverseRegistry.end()) {
            return it->second;
        }
        throw std::runtime_error("TypeCode not registered!");
    }

  private:
    static std::unordered_map<std::string, uint32_t> registry;
    static std::unordered_map<uint32_t, std::string> reverseRegistry;
    static std::mutex mtx;
    static std::atomic<uint32_t> counter;
};

class OtherType : public Type {
  public:
    OtherType() = delete;
    OtherType(TypeCode code) : Type(code) {}
    virtual ~OtherType() noexcept = default;

    std::string typeName() const {
        ASSERT((static_cast<uint32_t>(code_) & 0xF0000000) == 0xF0000000, "Not an OtherType");
        return OtherTypeRegistry::getTypeName(code_);
    }

    virtual std::string toString() const override = 0;

    virtual std::string mangle() const override = 0;

    virtual bool operator==(const Type &other) const override {
        if (!isOtherType(other.code())) {
            return false;
        }
        return code_ == other.code();
    }
    virtual bool operator!=(const Type &other) const override { return !(*this == other); }

    virtual bool assignable(const type_ptr_t &type) const {
        if (!isOtherType(type->code())) {
            return false;
        }
        return code_ == type->code();
    }

    virtual type_ptr_t clone() const override = 0;

    virtual CastSafety castSafetyTo(const Type &other) const override = 0;
};
