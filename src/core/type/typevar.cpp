// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Type variables and type functions: see camel/core/type/typevar.h.
 */

#include "camel/core/type/typevar.h"

#include "camel/core/mm.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/other.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"

#include <algorithm>

namespace camel::core::type {

// ---- TypeVarType ----

TypeVarType::TypeVarType(const std::string &name, bool binder)
    : Type(TypeCode::TypeVar), name_(name), binder_(binder) {}

TypeVarType *TypeVarType::create(const std::string &name, bool binder) {
    // One object per variable name and binding mark, from the type table (see intern.h): every
    // occurrence of `T` is one type with one id, instead of a fresh object per occurrence. A type
    // variable's identity is its binding site, so the name and the mark are both in the key and
    // **neither is erased**: the mark says whether this occurrence is one an argument can bind,
    // which is not a spelling, and it is what keeps a declaration's own parameter object and a
    // binding occurrence in a signature apart. The variable is therefore its own canonical form --
    // `equals` ignores the mark, so an occurrence that binds and a plain use of the same name are
    // one type to every type check.
    const std::string *text  = TypeTable::instance().internText(name);
    const uintptr_t words[2] = {reinterpret_cast<uintptr_t>(text), binder ? 1u : 0u};
    return static_cast<TypeVarType *>(TypeTable::instance().intern(
        TypeTable::Shape::TypeVar, std::span<const uintptr_t>(words, 2), [&]() -> Type * {
            void *mem = allocTypeStorage<TypeVarType>(TypeStorageKind::TypeVar);
            return new (mem) TypeVarType(name, binder);
        }));
}

std::string TypeVarType::toString() const { return name_; }

std::string TypeVarType::mangle() const { return "V" + std::to_string(name_.size()) + name_; }

Type *TypeVarType::clone(bool) const { return create(name_, binder_); }

bool TypeVarType::equals(Type *type) const {
    if (this == type) {
        return true;
    }
    if (!type || type->code() != TypeCode::TypeVar) {
        return false;
    }
    return static_cast<const TypeVarType *>(type)->name_ == name_;
}

// A variable stands for a type that is not known yet: nothing converts to it. Code that casts
// to a type written with a variable runs when the variable has been replaced.
CastSafety TypeVarType::castSafetyFrom(Type *sourceType) const {
    return equals(sourceType) ? CastSafety::Safe : CastSafety::Forbidden;
}

// ---- TypeOfType ----

TypeOfType::TypeOfType(const void *expression) : Type(TypeCode::TypeOf), expression_(expression) {}

TypeOfType *TypeOfType::create(const void *expression) {
    // One placeholder per expression (see intern.h): `typeof e` twice in one position is one
    // type. Two of them are equal when they are the same object (see below), so the placeholder
    // is its own canonical form and carries no name-erased twin: nothing compares it by
    // structure, and the graph builder resolves it before any type check.
    const uintptr_t words[1] = {reinterpret_cast<uintptr_t>(expression)};
    return static_cast<TypeOfType *>(TypeTable::instance().intern(
        TypeTable::Shape::TypeOf, std::span<const uintptr_t>(words, 1), [&]() -> Type * {
            void *mem = allocTypeStorage<TypeOfType>(TypeStorageKind::TypeVar);
            return new (mem) TypeOfType(expression);
        }));
}

std::string TypeOfType::toString() const { return "typeof"; }

std::string TypeOfType::mangle() const { return "O"; }

Type *TypeOfType::clone(bool) const { return create(expression_); }

// Two `typeof` placeholders are equal only when they are the same object: the expression they
// stand for is not compared here, and the graph builder resolves them before any type check.
bool TypeOfType::equals(Type *type) const { return this == type; }

// A placeholder is not the type of any value: it is replaced where the type is used.
CastSafety TypeOfType::castSafetyFrom(Type *) const { return CastSafety::Forbidden; }

Type *findTypeBinding(const type_bindings_t &bindings, const std::string &name) {
    for (const auto &[bound, type] : bindings) {
        if (bound == name) {
            return type;
        }
    }
    return nullptr;
}

// ---- TypeFunctionType ----

TypeFunctionType::TypeFunctionType(
    const std::string &name, std::vector<std::string> params, Type *body)
    : Type(TypeCode::TypeFunc), name_(name), params_(std::move(params)), body_(body) {}

TypeFunctionType *
TypeFunctionType::create(const std::string &name, std::vector<std::string> params, Type *body) {
    // One object per declaration of a parameterised type (see intern.h): its name, its parameter
    // names and its body. Two of them are equal when they are the same object (see below), so a
    // type function is its own canonical form: nothing compares it by structure.
    std::vector<uintptr_t> words;
    words.reserve(2 + params.size());
    words.push_back(reinterpret_cast<uintptr_t>(TypeTable::instance().internText(name)));
    words.push_back(typeKeyWord(body));
    for (const std::string &param : params) {
        words.push_back(reinterpret_cast<uintptr_t>(TypeTable::instance().internText(param)));
    }
    return static_cast<TypeFunctionType *>(TypeTable::instance().intern(
        TypeTable::Shape::TypeFunc, words, [&]() -> Type * {
            void *mem = allocTypeStorage<TypeFunctionType>(TypeStorageKind::TypeVar);
            return new (mem) TypeFunctionType(name, std::move(params), body);
        }));
}

Type *TypeFunctionType::apply(const type_vec_t &arguments) const {
    ASSERT(arguments.size() == params_.size(), "Type function applied to a wrong argument count.");
    type_bindings_t bindings;
    bindings.reserve(params_.size());
    for (size_t i = 0; i < params_.size(); ++i) {
        bindings.emplace_back(params_[i], arguments[i]);
    }
    return substituteTypeVars(body_, bindings);
}

std::string TypeFunctionType::toString() const {
    std::string result = name_ + "<";
    for (size_t i = 0; i < params_.size(); ++i) {
        result += (i > 0 ? ", " : "") + params_[i];
    }
    return result + ">";
}

std::string TypeFunctionType::mangle() const {
    return "Y" + std::to_string(name_.size()) + name_ + std::to_string(params_.size());
}

Type *TypeFunctionType::clone(bool) const { return create(name_, params_, body_); }

bool TypeFunctionType::equals(Type *type) const { return this == type; }

// A type function is not the type of any value.
CastSafety TypeFunctionType::castSafetyFrom(Type *) const { return CastSafety::Forbidden; }

// ---- The algebra ----

namespace {

const TypeVarType *asVar(Type *type) {
    return type && type->code() == TypeCode::TypeVar ? static_cast<const TypeVarType *>(type)
                                                     : nullptr;
}

std::string paramNameOf(const FunctionType *func, size_t argIndex, const char *prefix, size_t i) {
    return argIndex < func->argNamesCount() ? std::string(func->argNameAt(argIndex))
                                            : prefix + std::to_string(i);
}

/// A factory holding `func` with the variables (and, when `extents` is not null, the named
/// extents) replaced; `changed` tells whether anything was. A `constas` parameter is part of the
/// declaration's identity, not a runtime argument: it is dropped when `dropConstas` says the
/// function itself is being instantiated (the call binds its value), and kept otherwise, so a
/// function type written inside a substituted type keeps its own value parameter.
FunctionTypeFactory rebuildFunctionType(
    FunctionType *func, const type_bindings_t &bindings, const ExtentValues *extents,
    bool dropConstas, bool &changed) {
    FunctionTypeFactory factory;
    factory.setModifiers(func->modifiers());
    factory.setHasCompileInfo(func->hasArgNames());
    const auto replaced = [&](Type *type) {
        Type *result = substituteTypeVars(type, bindings);
        if (extents != nullptr) {
            result = substituteExtentValues(result, *extents);
        }
        changed |= result != type;
        return result;
    };
    for (size_t i = 0; i < func->withTypesCount(); ++i) {
        factory.addWithArg(
            paramNameOf(func, i, "__w", i),
            replaced(func->withTypeAt(i)),
            func->withIsVarAt(i),
            func->withNeedsValueAt(i));
    }
    for (size_t i = 0; i < func->normTypesCount(); ++i) {
        const std::string name =
            paramNameOf(func, func->withTypesCount() + i, "__n", i);
        if (func->hasRest() && i + 1 == func->normTypesCount()) {
            factory.addRestArg(name);
            continue;
        }
        if (func->normIsConstasAt(i)) {
            if (dropConstas) {
                // The value is known at the call: the instance has no such parameter.
                changed = true;
                continue;
            }
            factory.addConstasArg(name, replaced(func->normTypeAt(i)));
            continue;
        }
        factory.addNormArg(name, replaced(func->normTypeAt(i)), func->normIsVarAt(i));
    }
    factory.setExitType(func->hasExitType() ? replaced(func->exitType()) : nullptr);
    factory.setResources(func->resources());
    return factory;
}

/// A module type whose arguments are plain types (OtherType::typeArguments): the algebra
/// descends through them. Any other module type is opaque.
OtherType *asTypeApplication(Type *type) {
    if (type == nullptr || !type->isOtherType()) {
        return nullptr;
    }
    auto *other = static_cast<OtherType *>(type);
    return other->typeArguments() && other->paramCount() > 0 ? other : nullptr;
}

/// Whether any function type in `type` writes a resource clause. Only such a clause can be left
/// open by another occurrence of a variable a type is already solved to, so only it makes the
/// walk of unifyTypeVars worth continuing into a pattern whose variables are all solved.
bool writesResourceClause(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Function: {
        const auto *func = static_cast<const FunctionType *>(type);
        if (func->resources().mode != sem::ResourceSignature::Mode::Generic) {
            return true;
        }
        if (func->hasExitType() && writesResourceClause(func->exitType())) {
            return true;
        }
        for (size_t i = 0; i < func->withTypesCount(); ++i) {
            if (writesResourceClause(func->withTypeAt(i))) {
                return true;
            }
        }
        for (size_t i = 0; i < func->normTypesCount(); ++i) {
            if (writesResourceClause(func->normTypeAt(i))) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Array:
        return writesResourceClause(static_cast<const ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (writesResourceClause(tuple->typeAt(i))) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Struct: {
        const auto *record = static_cast<const StructType *>(type);
        if (record->named()) {
            for (size_t i = 0; i < record->typeArgCount(); ++i) {
                if (writesResourceClause(record->typeArgAt(i))) {
                    return true;
                }
            }
            return false;
        }
        for (size_t i = 0; i < record->size(); ++i) {
            if (writesResourceClause(record->typeAt(i))) {
                return true;
            }
        }
        return false;
    }
    default:
        break;
    }
    if (const OtherType *application = asTypeApplication(type); application != nullptr) {
        for (size_t i = 0; i < application->paramCount(); ++i) {
            if (writesResourceClause(application->params()[i])) {
                return true;
            }
        }
    }
    return false;
}

/// Whether one of the solved variables in `names` is solved to a type that writes a resource
/// clause (see writesResourceClause).
bool anySolutionWritesResourceClause(
    const std::vector<std::string> &names, const type_bindings_t &bindings) {
    for (const std::string &name : names) {
        if (writesResourceClause(findTypeBinding(bindings, name))) {
            return true;
        }
    }
    return false;
}

} // namespace

TupleType *findRepeatedMark(Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    const auto among = [](std::span<Type *const> types) -> TupleType * {
        for (Type *member : types) {
            if (TupleType *found = findRepeatedMark(member)) {
                return found;
            }
        }
        return nullptr;
    };
    switch (type->code()) {
    case TypeCode::Array:
        return findRepeatedMark(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        auto *tuple = static_cast<TupleType *>(type);
        if (tuple->isTagged()) {
            TupleType *inner = asTagged(tuple->payloadType());
            if (inner != nullptr && inner->tag() == tuple->tag()) {
                return tuple;
            }
            return findRepeatedMark(tuple->payloadType());
        }
        return among(tuple->types());
    }
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(type);
        // A named type is its declaration and type arguments; its fields are derived from
        // them, so looking at the arguments is enough (and cannot walk a declared type's
        // fields, which a named type prints but does not expose).
        if (record->named()) {
            return among(record->typeArgs());
        }
        return among(record->types());
    }
    case TypeCode::Function: {
        auto *func = static_cast<FunctionType *>(type);
        if (TupleType *found = among(func->withTypesSpan())) {
            return found;
        }
        if (TupleType *found = among(func->normTypesSpan())) {
            return found;
        }
        return func->hasExitType() ? findRepeatedMark(func->exitType()) : nullptr;
    }
    default:
        break;
    }
    if (OtherType *applied = asTypeApplication(type)) {
        return among(applied->paramsSpan());
    }
    return nullptr;
}

bool containsTypeVar(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::TypeVar:
        return true;
    case TypeCode::TypeFunc:
        return false;
    case TypeCode::Array:
        return containsTypeVar(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto types = static_cast<TupleType *>(type)->types();
        return std::any_of(types.begin(), types.end(), containsTypeVar);
    }
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(type);
        const auto types = record->named() ? record->typeArgs() : record->types();
        return std::any_of(types.begin(), types.end(), containsTypeVar);
    }
    case TypeCode::Function: {
        const auto *func = static_cast<FunctionType *>(type);
        const auto with  = func->withTypesSpan();
        const auto norm  = func->normTypesSpan();
        return std::any_of(with.begin(), with.end(), containsTypeVar) ||
               std::any_of(norm.begin(), norm.end(), containsTypeVar) ||
               (func->hasExitType() && containsTypeVar(func->exitType()));
    }
    default:
        break;
    }
    if (type->isOtherType()) {
        const auto params = static_cast<OtherType *>(type)->paramsSpan();
        return std::any_of(params.begin(), params.end(), containsTypeVar);
    }
    return false;
}

bool containsTypeOf(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::TypeOf:
        return true;
    case TypeCode::TypeFunc:
        return false;
    case TypeCode::Array:
        return containsTypeOf(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto types = static_cast<TupleType *>(type)->types();
        return std::any_of(types.begin(), types.end(), containsTypeOf);
    }
    case TypeCode::Struct: {
        auto *record     = static_cast<StructType *>(type);
        const auto types = record->named() ? record->typeArgs() : record->types();
        return std::any_of(types.begin(), types.end(), containsTypeOf);
    }
    case TypeCode::Function: {
        const auto *func = static_cast<FunctionType *>(type);
        const auto with  = func->withTypesSpan();
        const auto norm  = func->normTypesSpan();
        return std::any_of(with.begin(), with.end(), containsTypeOf) ||
               std::any_of(norm.begin(), norm.end(), containsTypeOf) ||
               (func->hasExitType() && containsTypeOf(func->exitType()));
    }
    default:
        break;
    }
    if (type->isOtherType()) {
        const auto params = static_cast<OtherType *>(type)->paramsSpan();
        return std::any_of(params.begin(), params.end(), containsTypeOf);
    }
    return false;
}

Type *substituteTypeVars(Type *type, const type_bindings_t &bindings) {
    if (type == nullptr || bindings.empty()) {
        return type;
    }
    switch (type->code()) {
    case TypeCode::TypeVar: {
        Type *bound = findTypeBinding(bindings, asVar(type)->name());
        return bound != nullptr ? bound : type;
    }
    case TypeCode::Array: {
        auto *array = static_cast<ArrayType *>(type);
        Type *elem  = substituteTypeVars(array->elemType(), bindings);
        // The shape belongs to the type: substituting the element type keeps the dimensions,
        // the pack and the open rank.
        return elem == array->elemType() ? type : array->withElementType(elem);
    }
    case TypeCode::Tuple: {
        auto *tuple  = static_cast<TupleType *>(type);
        bool changed = false;
        std::vector<Type *> types;
        types.reserve(tuple->size());
        for (Type *elem : tuple->types()) {
            types.push_back(substituteTypeVars(elem, bindings));
            changed |= types.back() != elem;
        }
        if (!changed) {
            return type;
        }
        if (tuple->isNullable()) {
            return TupleType::nullable(types.front());
        }
        if (tuple->isFallible()) {
            return TupleType::fallible(types.front());
        }
        return tuple->isCell() ? TupleType::cell(types.front()) : TupleType::create(types);
    }
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(type);
        bool changed = false;
        if (record->named()) {
            // A named instance is its declaration with its arguments: substitute in the
            // arguments, and the type is the same declaration instantiated anew
            // (`Entry<K, V>` with K, V bound is `Entry<int, string>`), never an anonymous
            // struct with the fields written out.
            std::vector<Type *> args;
            args.reserve(record->typeArgCount());
            for (Type *arg : record->typeArgs()) {
                args.push_back(substituteTypeVars(arg, bindings));
                changed |= args.back() != arg;
            }
            return changed ? StructType::get(record->decl(), args) : type;
        }
        StructTypeFactory factory;
        for (size_t i = 0; i < record->size(); ++i) {
            Type *field = substituteTypeVars(record->typeAt(i), bindings);
            changed |= field != record->typeAt(i);
            factory.add(std::string(record->fieldName(i)), field);
        }
        return changed ? factory.build() : type;
    }
    case TypeCode::Function: {
        bool changed = false;
        FunctionTypeFactory factory =
            rebuildFunctionType(static_cast<FunctionType *>(type), bindings, nullptr, false, changed);
        return changed ? factory.build() : type;
    }
    default:
        break;
    }
    if (OtherType *applied = asTypeApplication(type)) {
        bool changed = false;
        std::vector<Type *> params;
        params.reserve(applied->paramCount());
        for (Type *param : applied->paramsSpan()) {
            params.push_back(substituteTypeVars(param, bindings));
            changed |= params.back() != param;
        }
        return changed
                   ? applied->cloneWithParams(std::span<Type *const>(params.data(), params.size()))
                   : type;
    }
    // Primitive types, and module types whose arguments are not plain types: what those
    // arguments mean is the type's own business, so variables are not accepted among them in
    // the first place.
    return type;
}

OpenFunctionType *instantiateFunctionType(
    FunctionType *type, const type_bindings_t &bindings, const ExtentValues *extents) {
    bool changed = false;
    return rebuildFunctionType(type, bindings, extents, extents != nullptr, changed).buildOpen();
}

Type *substituteExtentValues(Type *type, const ExtentValues &values, const ExtentPacks &packs) {
    if (type == nullptr || values.empty()) {
        return type;
    }
    switch (type->code()) {
    case TypeCode::Array: {
        auto *array = static_cast<ArrayType *>(type);
        Type *elem  = substituteExtentValues(array->elemType(), values, packs);
        // An array's own extents name values too (`f32[n]` with a `constas n: int`), and they are
        // where such a name lives now that `[]` is the core array type: replace them as the type
        // arguments of a module type are replaced, so the parameter of a call becomes the concrete
        // one (`f32[3]`) and an argument of another extent is refused (design principles 5.4 gives
        // the shape check to the core; ruling 2026-10-24 evening, `print-form-flip-2026-10-25`
        // item 1). While the name stayed in the type, an array with a named extent accepts every
        // extent, so `total(3, ones([4]))` with `x: f32[n]` was taken as it was.
        if (const Type *named = array->withExtentValues(values, packs); named != nullptr) {
            array = const_cast<ArrayType *>(static_cast<const ArrayType *>(named));
        }
        return elem == array->elemType() ? array : array->withElementType(elem);
    }
    case TypeCode::Tuple: {
        auto *tuple  = static_cast<TupleType *>(type);
        bool changed = false;
        std::vector<Type *> types;
        types.reserve(tuple->size());
        for (Type *elem : tuple->types()) {
            types.push_back(substituteExtentValues(elem, values, packs));
            changed |= types.back() != elem;
        }
        if (!changed) {
            return type;
        }
        if (tuple->isNullable()) {
            return TupleType::nullable(types.front());
        }
        if (tuple->isFallible()) {
            return TupleType::fallible(types.front());
        }
        return tuple->isCell() ? TupleType::cell(types.front()) : TupleType::create(types);
    }
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(type);
        bool changed = false;
        if (record->named()) {
            std::vector<Type *> args;
            args.reserve(record->typeArgCount());
            for (Type *arg : record->typeArgs()) {
                args.push_back(substituteExtentValues(arg, values, packs));
                changed |= args.back() != arg;
            }
            return changed ? StructType::get(record->decl(), args) : type;
        }
        StructTypeFactory factory;
        for (size_t i = 0; i < record->size(); ++i) {
            Type *field = substituteExtentValues(record->typeAt(i), values, packs);
            changed |= field != record->typeAt(i);
            factory.add(std::string(record->fieldName(i)), field);
        }
        return changed ? factory.build() : type;
    }
    case TypeCode::Function: {
        bool changed = false;
        FunctionTypeFactory factory =
            rebuildFunctionType(static_cast<FunctionType *>(type), {}, &values, false, changed);
        return changed ? factory.build() : type;
    }
    default:
        break;
    }
    if (type->isOtherType()) {
        // A module type decides what its named extents mean (`Tensor` keeps them as a shape).
        // A type that knows nothing of them leaves the type alone.
        if (const Type *replaced = type->withExtentValues(values, packs);
            replaced != nullptr && replaced != type) {
            return const_cast<Type *>(replaced);
        }
    }
    return type;
}

bool bindTypeVars(Type *pattern, Type *actual, type_bindings_t &bindings) {
    if (pattern == nullptr || actual == nullptr) {
        return true;
    }
    if (const TypeVarType *var = asVar(pattern)) {
        if (var->binder() && findTypeBinding(bindings, var->name()) == nullptr) {
            bindings.emplace_back(var->name(), actual);
        }
        return true;
    }
    // Nothing to bind below: whether the argument fits is the ordinary check's question.
    std::vector<std::string> bindable;
    collectBindableTypeVars(pattern, bindable);
    if (std::all_of(bindable.begin(), bindable.end(), [&](const std::string &name) {
            return findTypeBinding(bindings, name) != nullptr;
        })) {
        return true;
    }
    if (pattern->code() != actual->code()) {
        return false;
    }
    switch (pattern->code()) {
    case TypeCode::Array:
        return bindTypeVars(
            static_cast<ArrayType *>(pattern)->elemType(),
            static_cast<ArrayType *>(actual)->elemType(),
            bindings);
    case TypeCode::Tuple: {
        auto *want = static_cast<TupleType *>(pattern);
        auto *have = static_cast<TupleType *>(actual);
        if (want->size() != have->size() || want->isCell() != have->isCell() ||
            want->tag() != have->tag()) {
            return false;
        }
        for (size_t i = 0; i < want->size(); ++i) {
            if (!bindTypeVars(want->typeAt(i), have->typeAt(i), bindings)) {
                return false;
            }
        }
        return true;
    }
    case TypeCode::Struct: {
        auto *want = static_cast<StructType *>(pattern);
        auto *have = static_cast<StructType *>(actual);
        if (want->named() && have->named()) {
            // Two instances of the same declaration: the arguments are the whole story. An
            // instance of another declaration is another type, and binds nothing.
            if (want->decl() != have->decl() ||
                want->typeArgCount() != have->typeArgCount()) {
                return false;
            }
            for (size_t i = 0; i < want->typeArgCount(); ++i) {
                if (!bindTypeVars(want->typeArgAt(i), have->typeArgAt(i), bindings)) {
                    return false;
                }
            }
            return true;
        }
        // A generic named type met by an unnamed value: the fields are matched by name, as
        // they are when such a value enters the type (design principles 5.2).
        for (size_t i = 0; i < want->size(); ++i) {
            const auto field = have->findField(want->fieldName(i));
            if (!field.has_value()) {
                return false;
            }
            if (!bindTypeVars(want->typeAt(i), have->typeAt(*field), bindings)) {
                return false;
            }
        }
        return true;
    }
    case TypeCode::Function: {
        auto *want = static_cast<FunctionType *>(pattern);
        auto *have = static_cast<FunctionType *>(actual);
        if (want->withTypesCount() != have->withTypesCount() ||
            want->normTypesCount() != have->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < want->withTypesCount(); ++i) {
            if (!bindTypeVars(want->withTypeAt(i), have->withTypeAt(i), bindings)) {
                return false;
            }
        }
        for (size_t i = 0; i < want->normTypesCount(); ++i) {
            if (!bindTypeVars(want->normTypeAt(i), have->normTypeAt(i), bindings)) {
                return false;
            }
        }
        if (want->hasExitType() && have->hasExitType()) {
            return bindTypeVars(want->exitType(), have->exitType(), bindings);
        }
        return true;
    }
    default:
        break;
    }
    if (OtherType *want = asTypeApplication(pattern)) {
        // The same type code was checked above; an argument list of another length is the
        // prototype of the name (`Map` written without arguments).
        auto *have = static_cast<OtherType *>(actual);
        if (have->paramCount() != want->paramCount()) {
            return false;
        }
        for (size_t i = 0; i < want->paramCount(); ++i) {
            if (!bindTypeVars(want->params()[i], have->params()[i], bindings)) {
                return false;
            }
        }
    }
    return true;
}

void collectTypeVars(Type *type, std::vector<std::string> &names) {
    if (type == nullptr) {
        return;
    }
    const auto among = [&](std::span<Type *const> types) {
        for (Type *member : types) {
            collectTypeVars(member, names);
        }
    };
    switch (type->code()) {
    case TypeCode::TypeVar: {
        const std::string &name = asVar(type)->name();
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
        return;
    }
    case TypeCode::Array:
        collectTypeVars(static_cast<ArrayType *>(type)->elemType(), names);
        return;
    case TypeCode::Tuple:
        among(static_cast<TupleType *>(type)->types());
        return;
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(type);
        if (record->named()) {
            among(record->typeArgs());
        } else {
            among(record->types());
        }
        return;
    }
    case TypeCode::Function: {
        auto *func = static_cast<FunctionType *>(type);
        among(func->withTypesSpan());
        among(func->normTypesSpan());
        if (func->hasExitType()) {
            collectTypeVars(func->exitType(), names);
        }
        return;
    }
    default:
        break;
    }
    if (OtherType *applied = asTypeApplication(type)) {
        among(applied->paramsSpan());
    }
}

bool unifyTypeVars(Type *pattern, Type *actual, type_bindings_t &bindings) {
    if (pattern == nullptr || actual == nullptr) {
        return true;
    }
    if (const TypeVarType *var = asVar(pattern)) {
        if (findTypeBinding(bindings, var->name()) == nullptr && !containsTypeVar(actual)) {
            bindings.emplace_back(var->name(), actual);
            return true;
        }
        // The variable is solved already, and this occurrence constrains it too. What satisfies
        // every occurrence is the weakest type they gave: a type that writes no resource clause is
        // a value of the one that writes them (sameTypeWithUnwrittenResources), never the other
        // way round, so this occurrence becomes the solution where the other wrote a clause and
        // this one writes none. A clause written in one argument is then not a requirement the
        // others have to keep, and the call's result promises no more than the arguments stated.
        Type *bound = findTypeBinding(bindings, var->name());
        if (bound != nullptr && !sameTypeWithUnwrittenResources(bound, actual) &&
            sameTypeWithUnwrittenResources(actual, bound)) {
            for (auto &[name, type] : bindings) {
                if (name == var->name()) {
                    type = actual;
                    break;
                }
            }
        }
        return true;
    }
    // Nothing left to learn below, or nothing known on the other side. A solved variable is still
    // something to learn below when its solution writes a resource clause: this occurrence may
    // leave that clause open (the case above), so the walk continues into a pattern whose
    // variables are all solved.
    if (actual->code() == TypeCode::TypeVar) {
        return true;
    }
    std::vector<std::string> names;
    collectTypeVars(pattern, names);
    if (std::all_of(
            names.begin(),
            names.end(),
            [&](const std::string &name) { return findTypeBinding(bindings, name) != nullptr; }) &&
        !anySolutionWritesResourceClause(names, bindings)) {
        return true;
    }
    // A value that is not of the form of a `T?` / `T!` parameter enters it: its type is the
    // payload's. `null` says nothing about the payload.
    if (TupleType *tagged = asTagged(pattern)) {
        TupleType *have = asTagged(actual);
        if (have == nullptr || have->tag() != tagged->tag()) {
            if (tagged->isNullable() && actual->code() == TypeCode::Void) {
                return true;
            }
            return unifyTypeVars(tagged->payloadType(), actual, bindings);
        }
    }
    if (pattern->code() != actual->code()) {
        return false;
    }
    switch (pattern->code()) {
    case TypeCode::Array:
        return unifyTypeVars(
            static_cast<ArrayType *>(pattern)->elemType(),
            static_cast<ArrayType *>(actual)->elemType(),
            bindings);
    case TypeCode::Tuple: {
        auto *want = static_cast<TupleType *>(pattern);
        auto *have = static_cast<TupleType *>(actual);
        if (want->size() != have->size() || want->isCell() != have->isCell() ||
            want->tag() != have->tag()) {
            return false;
        }
        bool ok = true;
        for (size_t i = 0; i < want->size(); ++i) {
            ok = unifyTypeVars(want->typeAt(i), have->typeAt(i), bindings) && ok;
        }
        return ok;
    }
    case TypeCode::Struct: {
        auto *want = static_cast<StructType *>(pattern);
        auto *have = static_cast<StructType *>(actual);
        bool ok    = true;
        if (want->named() && have->named()) {
            if (want->decl() != have->decl() ||
                want->typeArgCount() != have->typeArgCount()) {
                return false;
            }
            for (size_t i = 0; i < want->typeArgCount(); ++i) {
                ok = unifyTypeVars(want->typeArgAt(i), have->typeArgAt(i), bindings) && ok;
            }
            return ok;
        }
        for (size_t i = 0; i < want->size(); ++i) {
            const auto field = have->findField(want->fieldName(i));
            if (!field.has_value()) {
                return false;
            }
            ok = unifyTypeVars(want->typeAt(i), have->typeAt(*field), bindings) && ok;
        }
        return ok;
    }
    case TypeCode::Function: {
        auto *want = static_cast<FunctionType *>(pattern);
        auto *have = static_cast<FunctionType *>(actual);
        if (want->withTypesCount() != have->withTypesCount() ||
            want->normTypesCount() != have->normTypesCount()) {
            return false;
        }
        bool ok = true;
        for (size_t i = 0; i < want->withTypesCount(); ++i) {
            ok = unifyTypeVars(want->withTypeAt(i), have->withTypeAt(i), bindings) && ok;
        }
        for (size_t i = 0; i < want->normTypesCount(); ++i) {
            ok = unifyTypeVars(want->normTypeAt(i), have->normTypeAt(i), bindings) && ok;
        }
        if (want->hasExitType() && have->hasExitType()) {
            ok = unifyTypeVars(want->exitType(), have->exitType(), bindings) && ok;
        }
        return ok;
    }
    default:
        break;
    }
    if (OtherType *want = asTypeApplication(pattern)) {
        auto *have = static_cast<OtherType *>(actual);
        if (have->paramCount() != want->paramCount()) {
            return false;
        }
        bool ok = true;
        for (size_t i = 0; i < want->paramCount(); ++i) {
            ok = unifyTypeVars(want->params()[i], have->params()[i], bindings) && ok;
        }
        return ok;
    }
    return true;
}

void collectBindableTypeVars(Type *pattern, std::vector<std::string> &names) {
    if (pattern == nullptr) {
        return;
    }
    switch (pattern->code()) {
    case TypeCode::TypeVar: {
        const TypeVarType *var = asVar(pattern);
        if (var->binder() && std::find(names.begin(), names.end(), var->name()) == names.end()) {
            names.push_back(var->name());
        }
        return;
    }
    case TypeCode::Array:
        collectBindableTypeVars(static_cast<ArrayType *>(pattern)->elemType(), names);
        return;
    case TypeCode::Tuple:
        for (Type *elem : static_cast<TupleType *>(pattern)->types()) {
            collectBindableTypeVars(elem, names);
        }
        return;
    case TypeCode::Struct: {
        auto *record = static_cast<StructType *>(pattern);
        if (record->named()) {
            for (Type *arg : record->typeArgs()) {
                collectBindableTypeVars(arg, names);
            }
        } else {
            for (Type *field : record->types()) {
                collectBindableTypeVars(field, names);
            }
        }
        return;
    }
    case TypeCode::Function: {
        auto *func = static_cast<FunctionType *>(pattern);
        for (Type *param : func->withTypesSpan()) {
            collectBindableTypeVars(param, names);
        }
        for (Type *param : func->normTypesSpan()) {
            collectBindableTypeVars(param, names);
        }
        if (func->hasExitType()) {
            collectBindableTypeVars(func->exitType(), names);
        }
        return;
    }
    default:
        break;
    }
    if (OtherType *applied = asTypeApplication(pattern)) {
        for (Type *param : applied->paramsSpan()) {
            collectBindableTypeVars(param, names);
        }
    }
}

std::string typeBindingsToString(const type_bindings_t &bindings) {
    std::string result;
    for (const auto &[name, type] : bindings) {
        result += (result.empty() ? "" : ", ") + name + " = " + (type ? type->toString() : "?");
    }
    return result;
}

} // namespace camel::core::type



