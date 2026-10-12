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
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace camel::core::data {
class Data;
}

namespace camel::core::type {

/// One argument of a parameterised type as it is written between `<` and `>`: a type
/// (`Tensor<f32>`) or a compile-time constant (`Tensor<const [3, 4]>`). Exactly one member is set.
struct TypeArgument {
    Type *type = nullptr;
    std::shared_ptr<data::Data> data;
};

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
    /// The type `Name<args...>` denotes, arguments in source order. A type that gives its
    /// constant arguments a meaning overrides this; the default accepts type arguments only
    /// (and hands them to cloneWithParams). Returns nullptr with `error` set when the arguments
    /// are not ones the type accepts; the compiler reports that at the type expression.
    virtual OtherType *
    cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const;

    /// Whether the arguments of this type are plain types and nothing else (`Map<K, V>`), as
    /// opposed to arguments the type gives a meaning of its own (`Tensor<f32, const [3]>`).
    /// When true, a type variable may be written among them, and the type-variable algebra
    /// (typevar.h) substitutes and binds through params() with cloneWithParams.
    virtual bool typeArguments() const { return false; }

    /// The type as a program writes it, for diagnostics that suggest source text (`x as T`).
    /// toString() is the default; a type whose printed form is not its source form overrides.
    virtual std::string spelling() const { return toString(); }

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
