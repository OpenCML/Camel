// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/other.h"
#include "camel/core/mm.h"
#include "camel/core/type/storage.h"
#include "camel/utils/assert.h"

#include <span>

using namespace camel::core::type;

namespace camel::core::type {

std::mutex OtherTypeRegistry::mtx;
std::unordered_map<std::string, TypeCode> OtherTypeRegistry::registry;
std::unordered_map<TypeCode, std::string> OtherTypeRegistry::reverseRegistry;
std::atomic<unsigned int> OtherTypeRegistry::counter{0};

TypeCode registerOtherType(const std::string &typeName, TypeFlag flags) {
    // Code that sees the Composite flag treats the type as a CompositeType
    // object. Other types derive from OtherType instead, so they must not
    // carry that flag.
    ASSERT(
        !hasFlag(static_cast<uint32_t>(flags), TypeFlag::Composite),
        "Other types cannot be flagged Composite: " + typeName);
    return OtherTypeRegistry::registerType(typeName, flags);
}

std::string OtherType::typeName() const {
    ASSERT(camel::core::type::isOtherType(code_), "Not an OtherType");
    return OtherTypeRegistry::getTypeName(code_);
}

Type *
OtherType::cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    std::vector<Type *> types;
    types.reserve(arguments.size());
    for (const TypeArgument &argument : arguments) {
        if (argument.type == nullptr) {
            error = "type '" + typeName() + "' does not take a constant ('const ...') argument";
            return nullptr;
        }
        types.push_back(argument.type);
    }
    return cloneWithParams(std::span<Type *const>(types.data(), types.size()));
}

const Type *
OtherType::applyArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    // A module type decides what its arguments mean; the core only routes them here.
    return cloneWithArguments(arguments, error);
}

Type **OtherType::copyParams(std::span<Type *const> params) {
    if (params.empty())
        return nullptr;
    size_t n     = params.size();
    void *arrMem = allocTypeStorage(TypeStorageKind::Params, n * sizeof(Type *), alignof(Type *));
    Type **p     = static_cast<Type **>(arrMem);
    for (size_t i = 0; i < n; ++i)
        p[i] = params[i];
    return p;
}

} // namespace camel::core::type
