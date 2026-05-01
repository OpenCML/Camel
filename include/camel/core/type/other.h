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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/base.h"

#include <atomic>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace camel::core::type {

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

/** Allow external modules (for example python.cmo) to register Other types without directly
 * depending on OtherTypeRegistry's static data and risking link failures. */
TypeCode registerOtherType(const std::string &typeName, TypeFlag flags);

class OtherType : public Type {
  public:
    OtherType() = delete;
    explicit OtherType(TypeCode code) : Type(code), paramCount_(0), params_(nullptr) {}
    virtual ~OtherType() noexcept = default;

    std::string typeName() const;

    size_t paramCount() const { return paramCount_; }
    Type *const *params() const { return params_; }
    std::span<Type *const> paramsSpan() const {
        return params_ ? std::span<Type *const>(params_, paramCount_) : std::span<Type *const>();
    }

    virtual OtherType *cloneWithParams(std::span<Type *const> params) const = 0;

    virtual std::string toString() const override                      = 0;
    virtual std::string mangle() const override                        = 0;
    virtual Type *clone(bool deep = false) const override              = 0;
    virtual bool equals(Type *type) const override                     = 0;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override = 0;
    virtual bool assignableFrom(Type *sourceType) const                = 0;

  protected:
    OtherType(TypeCode code, size_t paramCount, Type **params)
        : Type(code), paramCount_(paramCount), params_(params) {}

    /** Helper for subclass cloneWithParams: allocate in permSpace and copy the params array,
     *
     * returning the new pointer. */
    static Type **copyParams(std::span<Type *const> params);

  protected:
    size_t paramCount_;
    Type **params_;
};

} // namespace camel::core::type
