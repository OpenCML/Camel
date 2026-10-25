// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/enum.h"

#include "camel/common/ref.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <vector>

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

EnumDecl *EnumDecl::create(
    const std::string &name, std::vector<std::string> params, std::string module) {
    return new EnumDecl(name, std::move(params), std::move(module));
}

EnumDecl::EnumDecl(
    const std::string &name, std::vector<std::string> params, std::string module)
    : name_(name), module_(std::move(module)),
      id_(enumDeclCounter.fetch_add(1, std::memory_order_relaxed)), params_(std::move(params)) {
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

/// The intern key of an instance: its declaration and the canonical ids of its type arguments,
/// exactly as a named struct's is (struct.cpp). The declaration decides which type it is, and an
/// instance compares its arguments with `equals`, so the names in them are erased here.
std::vector<uintptr_t> instanceKey(const EnumDecl *decl, std::span<Type *const> args) {
    std::vector<uintptr_t> key;
    key.reserve(1 + args.size());
    key.push_back(reinterpret_cast<uintptr_t>(decl));
    for (Type *arg : args) {
        key.push_back(typeKeyWord(TypeTable::instance().canonical(arg)));
    }
    return key;
}

} // namespace

TypeCode EnumType::typeCode() { return registerOtherType("Enum", TypeFlag::GC_Traced); }

Type *EnumType::hole() {
    // One object for the one hole code, from the type table.
    return Type::create(TypeCode::EnumHole);
}

EnumType *EnumType::get(EnumDecl *decl, std::span<Type *const> args) {
    if (args.size() > decl->params().size()) {
        return nullptr;
    }
    // One object per declaration and canonical type arguments, from the type table (see intern.h):
    // the instance of a declaration is one type with one id, as a named struct's is.
    const std::vector<uintptr_t> key = instanceKey(decl, args);
    std::vector<Type *> typeArgs(args.begin(), args.end());
    return static_cast<EnumType *>(TypeTable::instance().intern(
        TypeTable::Shape::Enum, key, [&]() -> Type * {
            return new (allocTypeStorage<EnumType>(TypeStorageKind::Enum))
                EnumType(decl, std::span<Type *const>(typeArgs));
        }));
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

std::string EnumType::toString() const { return typeToString(this); }

std::string EnumType::mangle() const { return typeMangle(this); }

Type *EnumType::clone(bool) const { return const_cast<EnumType *>(this); }

bool EnumType::equals(Type *type) const { return typeEquals(this, type); }

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

namespace {

bool containsEnumHoleImpl(Type *type, std::vector<const Type *> &path) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::EnumHole) {
        return true;
    }
    // A named type may mention itself through an array; seeing it again on the same path says
    // its fields have already been searched, and the search would not end.
    if (std::find(path.begin(), path.end(), type) != path.end()) {
        return false;
    }
    path.push_back(type);
    bool found = false;
    switch (type->code()) {
    case TypeCode::Array:
        found = containsEnumHoleImpl(static_cast<ArrayType *>(type)->elemType(), path);
        break;
    case TypeCode::Tuple:
        for (Type *member : static_cast<TupleType *>(type)->types()) {
            if ((found = containsEnumHoleImpl(member, path))) {
                break;
            }
        }
        break;
    case TypeCode::Struct:
        for (Type *member : static_cast<StructType *>(type)->types()) {
            if ((found = containsEnumHoleImpl(member, path))) {
                break;
            }
        }
        break;
    default:
        break;
    }
    if (!found) {
        if (EnumType *enumType = asEnumTypeOrPrototype(type)) {
            for (Type *arg : enumType->paramsSpan()) {
                if ((found = containsEnumHoleImpl(arg, path))) {
                    break;
                }
            }
        }
    }
    path.pop_back();
    return found;
}

} // namespace

bool containsEnumHole(Type *type) {
    std::vector<const Type *> path;
    return containsEnumHoleImpl(type, path);
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