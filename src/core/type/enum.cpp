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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/type/enum.h"

#include "camel/common/ref.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/typevar.h"

#include <algorithm>
#include <format>
#include <unordered_map>

namespace camel::core::type {

namespace {

std::atomic<uint32_t> enumDeclCounter{1};

} // namespace

std::optional<size_t> EnumVariant::findField(std::string_view name) const {
    for (size_t i = 0; i < fieldNames.size(); ++i) {
        if (fieldNames[i] == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::string EnumVariant::signature() const {
    switch (shape) {
    case VariantShape::Unit:
        return name;
    case VariantShape::Tuple: {
        std::string text = name + "(";
        for (size_t i = 0; i < fieldTypes.size(); ++i) {
            if (i > 0) {
                text += ", ";
            }
            text += fieldTypes[i] != nullptr ? fieldTypes[i]->toString() : std::string("_");
        }
        text += ")";
        return text;
    }
    case VariantShape::Struct: {
        std::string text = name + " { ";
        for (size_t i = 0; i < fieldTypes.size(); ++i) {
            if (i > 0) {
                text += ", ";
            }
            text += fieldNames[i] + ": " +
                    (fieldTypes[i] != nullptr ? fieldTypes[i]->toString() : std::string("_"));
        }
        text += " }";
        return text;
    }
    }
    return name;
}

// ---- EnumDecl ----

EnumDecl *EnumDecl::create(const std::string &name, std::vector<std::string> params) {
    return new EnumDecl(name, std::move(params));
}

EnumDecl::EnumDecl(const std::string &name, std::vector<std::string> params)
    : name_(name), id_(enumDeclCounter.fetch_add(1, std::memory_order_relaxed)),
      params_(std::move(params)) {
    const Reference ref(name);
    shortName_ = ref.empty() ? name : ref.ident();
}

void EnumDecl::define(std::vector<EnumVariant> variants) {
    ASSERT(!defined(), "An enum declaration is defined once.");
    variants_ = std::move(variants);
    defined_.store(true, std::memory_order_release);
}

std::optional<size_t> EnumDecl::findVariant(std::string_view name) const {
    for (size_t i = 0; i < variants_.size(); ++i) {
        if (variants_[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

EnumType *EnumDecl::prototype() const {
    return EnumType::get(const_cast<EnumDecl *>(this), std::span<Type *const>());
}

// ---- EnumType ----

namespace {

/// The interned instances, keyed by declaration and arguments, exactly as collection.cpp does:
/// a type holding a type variable is not interned, because the declaration's binder and a use
/// of it are different types.
std::mutex enumMutex;
std::unordered_map<std::string, EnumType *> &enumInstances() {
    static std::unordered_map<std::string, EnumType *> instances;
    return instances;
}

bool internable(std::span<Type *const> args) {
    return std::all_of(args.begin(), args.end(), [](Type *arg) {
        return arg != nullptr && !containsTypeVar(arg);
    });
}

std::string instanceKey(const EnumDecl *decl, std::span<Type *const> args) {
    std::string key = std::to_string(decl->id()) + "|";
    for (Type *arg : args) {
        key += arg->mangle();
        key += '|';
    }
    return key;
}

} // namespace

TypeCode EnumType::typeCode() { return registerOtherType("Enum", TypeFlag::GC_Traced); }

Type *EnumType::hole() {
    static Type *holeType = [] {
        Type *type = Type::create(TypeCode::EnumHole);
        TypeTable::instance().adopt(type);
        return type;
    }();
    return holeType;
}

EnumType *EnumType::get(EnumDecl *decl, std::span<Type *const> args) {
    if (args.size() > decl->params().size()) {
        return nullptr;
    }
    if (args.empty() || !internable(args)) {
        return new (allocTypeStorage<EnumType>(TypeStorageKind::Enum)) EnumType(decl, args);
    }
    std::lock_guard<std::mutex> lock(enumMutex);
    auto &instances       = enumInstances();
    const std::string key = instanceKey(decl, args);
    if (auto it = instances.find(key); it != instances.end()) {
        return it->second;
    }
    auto *type = new (allocTypeStorage<EnumType>(TypeStorageKind::Enum)) EnumType(decl, args);
    instances.emplace(key, type);
    return type;
}

EnumType::EnumType(EnumDecl *decl, std::span<Type *const> args)
    : OtherType(typeCode(), args.size(), copyParams(args)), decl_(decl) {}

bool EnumType::pending() const {
    return std::any_of(params_, params_ + paramCount_, [](Type *arg) {
        return arg != nullptr && containsEnumHole(arg);
    });
}

const std::vector<EnumVariant> &EnumType::variants() const {
    ASSERT(complete(), "The variants of an enum prototype are not those of a value.");
    std::call_once(variantsOnce_, [this] {
        type_bindings_t bindings;
        for (size_t i = 0; i < decl_->params().size(); ++i) {
            bindings.emplace_back(decl_->params()[i], params_[i]);
        }
        variants_ = decl_->variants();
        for (EnumVariant &variant : variants_) {
            for (Type *&fieldType : variant.fieldTypes) {
                fieldType = substituteTypeVars(fieldType, bindings);
            }
        }
    });
    return variants_;
}

OtherType *EnumType::cloneWithParams(std::span<Type *const> params) const {
    return get(decl_, params);
}

OtherType *
EnumType::cloneWithArguments(std::span<const TypeArgument> arguments, std::string &error) const {
    if (prototype()) {
        std::vector<Type *> types;
        types.reserve(arguments.size());
        for (const TypeArgument &argument : arguments) {
            if (argument.type == nullptr) {
                error = "a constant is not an argument of '" + decl_->name() + "'";
                return nullptr;
            }
            types.push_back(argument.type);
        }
        if (types.size() != decl_->params().size()) {
            error = std::format(
                "'{}' takes {} type argument(s), but {} were written",
                decl_->name(),
                decl_->params().size(),
                types.size());
            return nullptr;
        }
        return get(decl_, types);
    }
    if (!arguments.empty()) {
        error = "'" + toString() + "' is already an instance of '" + decl_->name() +
                "' and takes no arguments";
        return nullptr;
    }
    return cloneWithParams({});
}

std::string EnumType::toString() const {
    if (paramCount_ == 0) {
        return decl_->name();
    }
    std::string text = decl_->name() + "<";
    for (size_t i = 0; i < paramCount_; ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += params_[i] != nullptr ? params_[i]->toString() : std::string("_");
    }
    text += ">";
    return text;
}

std::string EnumType::mangle() const {
    std::string text = "E" + std::to_string(decl_->id());
    if (paramCount_ == 0) {
        text += "P";
        return text;
    }
    for (size_t i = 0; i < paramCount_; ++i) {
        text += "_";
        text += params_[i] != nullptr ? params_[i]->mangle() : std::string("H");
    }
    return text;
}

Type *EnumType::clone(bool) const { return const_cast<EnumType *>(this); }

bool EnumType::equals(Type *type) const {
    if (this == type) {
        return true;
    }
    if (type == nullptr || type->code() != code_) {
        return false;
    }
    const auto *other = static_cast<const EnumType *>(type);
    if (other->decl_ != decl_ || other->paramCount_ != paramCount_) {
        return false;
    }
    for (size_t i = 0; i < paramCount_; ++i) {
        if (!params_[i]->equals(other->params_[i])) {
            return false;
        }
    }
    return true;
}

Type *EnumType::unify(Type *other) const {
    if (other == nullptr || other->code() != code_) {
        return nullptr;
    }
    const auto *type = static_cast<const EnumType *>(other);
    if (type->decl_ != decl_ || type->paramCount_ != paramCount_) {
        return nullptr;
    }
    if (equals(other)) {
        return const_cast<EnumType *>(this);
    }
    std::vector<Type *> merged(paramCount_, nullptr);
    bool changed = false;
    for (size_t i = 0; i < paramCount_; ++i) {
        Type *mine  = params_[i];
        Type *their = type->params_[i];
        if (mine->code() == TypeCode::EnumHole) {
            merged[i] = their;
            changed |= their->code() != TypeCode::EnumHole;
        } else if (their->code() == TypeCode::EnumHole) {
            merged[i] = mine;
        } else if (mine->equals(their)) {
            merged[i] = mine;
        } else if (Type *unified = mine->unify(their)) {
            merged[i] = unified;
            changed |= unified != mine;
        } else {
            return nullptr;
        }
    }
    return changed ? get(decl_, merged) : const_cast<EnumType *>(this);
}

bool EnumType::assignableFrom(Type *sourceType) const {
    return equals(sourceType) || enumEntersFrom(const_cast<EnumType *>(this), sourceType);
}

CastSafety EnumType::castSafetyFrom(Type *sourceType) const {
    if (equals(sourceType)) {
        return CastSafety::Safe;
    }
    if (enumEntersFrom(const_cast<EnumType *>(this), sourceType)) {
        return CastSafety::Safe;
    }
    return CastSafety::Forbidden;
}

slot_t EnumType::castSlotFrom(slot_t value, Type *sourceType) const {
    ASSERT(sourceType != nullptr, "castSlotFrom source type is null");
    ASSERT(
        castSafetyFrom(sourceType) == CastSafety::Safe,
        "castSlotFrom only allowed when castSafetyFrom returns Safe");
    return value;
}

// ---- Queries ----

const EnumType *asEnumType(const Type *type) {
    if (type == nullptr || type->code() != EnumType::typeCode()) {
        return nullptr;
    }
    const auto *enumType = static_cast<const EnumType *>(type);
    return enumType->prototype() ? nullptr : enumType;
}

EnumType *asEnumType(Type *type) {
    return const_cast<EnumType *>(asEnumType(static_cast<const Type *>(type)));
}

EnumType *asEnumTypeOrPrototype(Type *type) {
    if (type == nullptr || type->code() != EnumType::typeCode()) {
        return nullptr;
    }
    return static_cast<EnumType *>(type);
}

bool containsEnumHole(Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::EnumHole) {
        return true;
    }
    switch (type->code()) {
    case TypeCode::Array:
        return containsEnumHole(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        for (Type *member : static_cast<TupleType *>(type)->types()) {
            if (containsEnumHole(member)) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Struct: {
        for (Type *member : static_cast<StructType *>(type)->types()) {
            if (containsEnumHole(member)) {
                return true;
            }
        }
        return false;
    }
    default:
        break;
    }
    if (EnumType *enumType = asEnumTypeOrPrototype(type)) {
        for (Type *arg : enumType->paramsSpan()) {
            if (containsEnumHole(arg)) {
                return true;
            }
        }
    }
    return false;
}

bool enumEntersFrom(Type *target, Type *source) {
    EnumType *targetEnum = asEnumType(target);
    EnumType *sourceEnum = asEnumType(source);
    if (targetEnum == nullptr || sourceEnum == nullptr ||
        targetEnum->decl() != sourceEnum->decl() ||
        targetEnum->complete() != sourceEnum->complete()) {
        return false;
    }
    for (size_t i = 0; i < targetEnum->paramCount(); ++i) {
        Type *want = targetEnum->params()[i];
        Type *have = sourceEnum->params()[i];
        if (want->equals(have)) {
            continue;
        }
        if (have->code() == TypeCode::EnumHole) {
            continue;
        }
        if (EnumType *wantEnum = asEnumTypeOrPrototype(want)) {
            if (EnumType *haveEnum = asEnumTypeOrPrototype(have)) {
                if (enumEntersFrom(wantEnum, haveEnum)) {
                    continue;
                }
            }
        }
        return false;
    }
    return true;
}

} // namespace camel::core::type