// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/typecall.h"

#include "camel/core/derivative.h"
#include "camel/core/mm.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <vector>

namespace camel::core::type {

TypeCode TypeCallType::typeCode() {
    static TypeCode code = registerOtherType("TypeCall", TypeFlag::Auxiliary);
    return code;
}

namespace {

constexpr std::string_view kTypeFunctions[] = {
    "PayloadOf",
    "KeyOf",
    "ValueOf",
    "HeadOf",
    "RestOf",
    "ParamsOf",
    "WithOf",
    "ResultOf",
    "Tangent"};

constexpr std::string_view kPackName      = "$pack";
constexpr std::string_view kExpandName    = "$expand";
constexpr std::string_view kLengthsTag    = "$lengths:";
constexpr std::string_view kTypeApplyName = "$type_function";

/// The tangent space of a value (design principles 9.9): a float and a tensor of floats are their
/// own tangent space; a tuple and a struct are the tangent space of each member in the member's
/// own place; every other type is the empty tuple, the value that carries no data and that a rule
/// ignores -- an integer, a `bool` and a `string` have no derivative, and neither has a function.
///
/// The empty tuple is what the answer is for a type the derivative machinery has no space for, so
/// a tangent type is always a type a program can write: a parameter the user differentiates never
/// meets a "no tangent" hole, it meets a value of no data. Which member is a continuous quantity
/// is not this function's question; it is the same one `machineryTangentTypeOf` answers.
Type *tangentSpaceOf(Type *type) {
    if (type->code() == TypeCode::Struct || type->code() == TypeCode::Tuple) {
        const bool structure = type->code() == TypeCode::Struct;
        const size_t size =
            structure ? tt::as_ptr<StructType>(type)->size() : tt::as_ptr<TupleType>(type)->size();
        StructTypeFactory factory;
        std::vector<Type *> elements;
        for (size_t i = 0; i < size; ++i) {
            Type *member = structure ? tt::as_ptr<StructType>(type)->typeAt(i)
                                     : tt::as_ptr<TupleType>(type)->typeAt(i);
            Type *space  = tangentSpaceOf(member);
            if (structure) {
                factory.add(std::string(tt::as_ptr<StructType>(type)->fieldName(i)), space);
            } else {
                elements.push_back(space);
            }
        }
        return structure ? static_cast<Type *>(factory.build())
                         : static_cast<Type *>(TupleType::create(std::move(elements)));
    }
    if (Type *space = camel::core::machineryTangentTypeOf(type); space != nullptr) {
        return space;
    }
    return TupleType::create(std::vector<Type *>{});
}

} // namespace

bool TypeCallType::isTypeFunction(std::string_view name) {
    return std::find(std::begin(kTypeFunctions), std::end(kTypeFunctions), name) !=
           std::end(kTypeFunctions);
}

Type *TypeCallType::evaluate(
    std::string_view name, std::span<Type *const> arguments, std::string &error) {
    if (name == kTypeApplyName) {
        // A source type function whose arguments included `typeof` was kept as an application
        // instead of being applied before the expression's type was known. Its declaration is the
        // first, internal parameter; the written arguments follow it.
        if (arguments.empty() || arguments.front() == nullptr ||
            arguments.front()->code() != TypeCode::TypeFunc) {
            error = "its type function declaration is missing";
            return nullptr;
        }
        auto *function = static_cast<TypeFunctionType *>(arguments.front());
        if (arguments.size() != function->params().size() + 1) {
            error = "it takes " + std::to_string(function->params().size()) +
                    " type argument(s), and " + std::to_string(arguments.size() - 1) +
                    " were given";
            return nullptr;
        }
        const type_vec_t written(arguments.begin() + 1, arguments.end());
        return function->apply(written);
    }
    // The two projections of a pack (`stdlib/types.cml#HeadOf`, `#RestOf`): the member at its head,
    // and the pack of the members after it. They are read **before** the member-wise rule below,
    // because their answer is about the pack itself and not about each member of it: mapped over
    // the members, `HeadOf<(float, int)>` would ask `HeadOf<float>` for every one of them.
    //
    // They are primitives where `FirstOf<P>` is the `match type` a program writes, and the
    // difference is not the answer -- both answer the type at the head -- but where the answer is
    // *read*: a `match type` written into the result of a generic function leaves its two names
    // (`W`, `Rest`) as type variables of that call, which no argument binds, so a library function
    // whose answer moves a pack's first member onto another list cannot be called at all
    // (`stdlib/vjp.cml#grad`). A primitive is evaluated with the pack in hand.
    if (name == "HeadOf" || name == "RestOf") {
        if (arguments.size() != 1 || arguments.front() == nullptr) {
            error = "it takes one pack";
            return nullptr;
        }
        Type *pack = arguments.front();
        if (!TypeCallType::isPack(pack)) {
            error = pack->toString() + " is not a pack (`..` of a type is a type expression)";
            return nullptr;
        }
        const std::span<Type *const> members = TypeCallType::packMembers(pack);
        if (name == "HeadOf") {
            if (members.empty()) {
                error = "the pack is empty: it has no first member";
                return nullptr;
            }
            return members.front();
        }
        if (members.empty()) {
            return makePack(std::vector<Type *>{});
        }
        return makePack(std::vector<Type *>(members.begin() + 1, members.end()));
    }
    // A pack is a list of types, and taking it apart is the `match type` case the language writes
    // (design principles 5.6: `case (W, ...Rest)` in a type position binds the member at the head
    // of the pack and the rest of it). No primitive of this file does it: a type function that
    // takes a pack apart is the case written in the standard library, so a program that writes the
    // pattern gets the same answer the library's name gives.
    if (arguments.size() != 1 || arguments.front() == nullptr) {
        error = "it takes one type";
        return nullptr;
    }
    Type *type = arguments.front();
    // A type function applied to a pack of known members is applied to each member, and the pack of
    // the answers is its answer: `Tangent<ParamsOf<F>>` is one tangent per parameter of `F`, and
    // `..Tangent<ParamsOf<F>>` in a parameter list is one parameter per member (design 5.6).
    // Without this rule the pack arrives as a single type that is neither a float nor a tuple, and
    // the empty tuple -- the tangent of an integer -- is the answer for every parameter of `F`; a
    // function whose parameters are arrays then declares no tangent at all, and the call that was
    // typed by this rule is refused for the wrong reason.
    if (TypeCallType::isPack(type)) {
        const std::span<Type *const> members = TypeCallType::packMembers(type);
        std::vector<Type *> mapped;
        mapped.reserve(members.size());
        for (Type *member : members) {
            const std::span<Type *const> one(&member, 1);
            std::string inner;
            Type *computed = TypeCallType::evaluate(name, one, inner);
            if (computed == nullptr) {
                error = inner;
                return nullptr;
            }
            mapped.push_back(computed);
        }
        return makePack(mapped);
    }
    if (name == "Tangent") {
        return tangentSpaceOf(type);
    }
    if (name == "ParamsOf" || name == "WithOf" || name == "ResultOf") {
        if (type->code() != TypeCode::Function) {
            error = type->toString() + " is not a function type";
            return nullptr;
        }
        const auto *function = static_cast<const FunctionType *>(type);
        if (name == "ResultOf") {
            if (!function->hasExitType() || function->exitType() == nullptr) {
                error = type->toString() + " has no result type yet";
                return nullptr;
            }
            return function->exitType();
        }
        // The parameters a call passes: the `constas` values are not among them, and a rest
        // parameter or a `with` pack stands for none that is known.
        std::vector<Type *> members;
        const bool withList = name == "WithOf";
        const size_t count  = withList ? function->withTypesCount() : function->normTypesCount();
        for (size_t i = 0; i < count; ++i) {
            if (withList ? function->hasWithRest() && i + 1 == count
                         : function->hasRest() && i + 1 == count) {
                error = type->toString() + " has a parameter pack, whose members are not known";
                return nullptr;
            }
            if (withList ? function->withIsConstasAt(i) : function->normIsConstasAt(i)) {
                continue;
            }
            members.push_back(withList ? function->withTypeAt(i) : function->normTypeAt(i));
        }
        return makePack(members);
    }
    if (name == "PayloadOf") {
        if (TupleType *tagged = asTagged(type)) {
            return tagged->payloadType();
        }
        error = type->toString() + " is neither a T? nor a T!";
        return nullptr;
    }
    if (name == "KeyOf" || name == "ValueOf") {
        const CollectionType *collection = asCollectionType(type);
        // A map has keys and values; a set has keys and nothing else -- its elements are its keys
        // (`CollectionType::keyType` is the element type of either). So `KeyOf` answers the element
        // type of a set, and `ValueOf` has nothing to answer for one.
        const bool answers = collection != nullptr && collection->complete() &&
                             (name == "KeyOf" || collection->isMap());
        if (!answers) {
            const bool isSet = collection != nullptr && collection->complete();
            error = type->toString() +
                    (isSet ? " is a set, which has keys and no values" : " is not a map");
            return nullptr;
        }
        return name == "KeyOf" ? collection->keyType() : collection->valueType();
    }
    error = "it is not a type function";
    return nullptr;
}

TypeCallType *TypeCallType::makePack(std::span<Type *const> members) {
    return create(kPackName, members);
}

TypeCallType *TypeCallType::makeExpand(Type *pattern) {
    Type *const arguments[] = {pattern};
    return create(kExpandName, arguments);
}

TypeCallType *TypeCallType::makeLengthMismatch(const std::string &description) {
    return create(std::string(kLengthsTag) + description, {});
}

bool TypeCallType::isPack(const Type *type) {
    return type != nullptr && type->code() == typeCode() &&
           static_cast<const TypeCallType *>(type)->name() == kPackName;
}

bool TypeCallType::isExpand(const Type *type) {
    return type != nullptr && type->code() == typeCode() &&
           static_cast<const TypeCallType *>(type)->name() == kExpandName;
}

std::span<Type *const> TypeCallType::packMembers(const Type *type) {
    return isPack(type) ? static_cast<const TypeCallType *>(type)->paramsSpan()
                        : std::span<Type *const>{};
}

TypeCallType *TypeCallType::create(std::string_view name, std::span<Type *const> arguments) {
    const std::string *text = TypeTable::instance().internText(name);
    std::vector<uintptr_t> parts;
    parts.reserve(arguments.size() + 1);
    parts.push_back(reinterpret_cast<uintptr_t>(text));
    for (Type *argument : arguments) {
        parts.push_back(typeKeyWord(TypeTable::instance().canonical(argument)));
    }
    return static_cast<TypeCallType *>(
        TypeTable::instance().internOther(typeCode(), parts, [&]() -> Type * {
            Type **params = OtherType::copyParams(arguments);
            void *mem     = mm::permSpace().alloc(sizeof(TypeCallType), alignof(TypeCallType));
            GUARD(mem != nullptr, "Failed to allocate TypeCallType from permSpace");
            return new (mem) TypeCallType(text, arguments.size(), params);
        }));
}

TypeCallType *
TypeCallType::makeTypeFunctionCall(TypeFunctionType *function, std::span<Type *const> arguments) {
    GUARD(function != nullptr, "A deferred type application needs its declaration.");
    type_vec_t deferred;
    deferred.reserve(arguments.size() + 1);
    deferred.push_back(function);
    deferred.insert(deferred.end(), arguments.begin(), arguments.end());
    return create(kTypeApplyName, deferred);
}

std::string TypeCallType::toString() const {
    if (*name_ == kExpandName) {
        return "..." + (params_[0] != nullptr ? params_[0]->toString() : std::string("?"));
    }
    if (name_->starts_with(kLengthsTag)) {
        return name_->substr(kLengthsTag.size());
    }
    const bool deferred = *name_ == kTypeApplyName;
    std::string text    = deferred && paramCount_ > 0 && params_[0] != nullptr &&
                                  params_[0]->code() == TypeCode::TypeFunc
                              ? static_cast<TypeFunctionType *>(params_[0])->name() + "<"
                              : (*name_ == kPackName ? std::string("pack") : *name_) + "<";
    const size_t first  = deferred ? 1 : 0;
    for (size_t i = first; i < paramCount_; ++i) {
        text += (i > first ? ", " : "") + (params_[i] != nullptr ? params_[i]->toString() : "?");
    }
    return text + ">";
}

std::string TypeCallType::mangle() const {
    std::string text = "TC" + std::to_string(name_->size()) + *name_;
    for (size_t i = 0; i < paramCount_; ++i) {
        text += "_" + (params_[i] != nullptr ? params_[i]->mangle() : std::string("X"));
    }
    return text;
}

Type *TypeCallType::clone(bool) const { return const_cast<TypeCallType *>(this); }

bool TypeCallType::equals(Type *type) const { return type == this; }

bool TypeCallType::assignableFrom(Type *sourceType) const { return sourceType == this; }

// A call is not the type of any value: nothing converts to it.
CastSafety TypeCallType::castSafetyFrom(Type *) const { return CastSafety::Forbidden; }

OtherType *TypeCallType::cloneWithParams(std::span<Type *const> params) const {
    return create(*name_, params);
}

// ---- MatchTypeType ----

namespace {

/// Whether any `match type` a build made has carried a guard (see `anyTypeCaseGuardBuilt`).
bool gTypeCaseGuardBuilt = false;

} // namespace

bool anyTypeCaseGuardBuilt() { return gTypeCaseGuardBuilt; }

TypeCode MatchTypeType::typeCode() {
    static TypeCode code = registerOtherType("MatchType", TypeFlag::Auxiliary);
    return code;
}

std::string matchShapeText(uint16_t shapes) {
    const auto bit = [](unsigned shape) { return static_cast<uint16_t>(1u << shape); };
    if ((shapes & bit(3)) != 0) {
        return "_";
    }
    static constexpr std::pair<unsigned, const char *> names[] = {
        {0, "struct"},
        {1, "tuple"},
        {2, "#leaf"},
        {4, "func"},
        {6, "array"},
        {7, "nullable"},
        {8, "fallible"},
        {9, "map"},
        {10, "set"},
    };
    std::string text;
    for (const auto &[shape, name] : names) {
        if ((shapes & bit(shape)) == 0) {
            continue;
        }
        text += (text.empty() ? "" : " | ") + std::string(name);
    }
    return text.empty() ? "..." : text;
}

uint16_t typeShapeBits(const Type *type) {
    const auto bit = [](unsigned shape) { return static_cast<uint16_t>(1u << shape); };
    uint16_t bits  = bit(3); // the wildcard selects every type
    if (type == nullptr) {
        return bits;
    }
    bool structured = false;
    switch (type->code()) {
    case TypeCode::Struct:
        bits |= bit(0);
        structured = true;
        break;
    case TypeCode::Array:
        bits |= bit(6);
        break;
    case TypeCode::Function:
        bits |= bit(4);
        break;
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        if (tuple->isNullable()) {
            bits |= bit(7);
        } else if (tuple->isFallible()) {
            bits |= bit(8);
        } else if (!tuple->isCell()) {
            bits |= bit(1);
            structured = true;
        }
        break;
    }
    default:
        if (const CollectionType *collection = asCollectionType(type)) {
            bits |= bit(collection->isMap() ? 9 : 10);
        }
        break;
    }
    // Every type that is neither a struct nor an ordinary tuple is a leaf.
    return structured ? bits : static_cast<uint16_t>(bits | bit(2));
}

MatchTypeType *MatchTypeType::create(
    Type *subject,
    std::span<const MatchTypeArm> arms,
    std::span<const std::pair<std::string, Type *>> pendingGuardBindings) {
    std::vector<Type *> params{subject};
    std::vector<uint16_t> shapes;
    std::vector<uint32_t> identityCounts;
    std::vector<uint32_t> packCounts;
    std::vector<uint32_t> arrayCounts;
    std::vector<std::shared_ptr<const TypeCaseGuard>> guards;
    guards.reserve(arms.size());
    std::vector<uintptr_t> key{typeKeyWord(TypeTable::instance().canonical(subject))};
    for (const MatchTypeArm &arm : arms) {
        shapes.push_back(arm.shapes);
        identityCounts.push_back(static_cast<uint32_t>(arm.identities.size()));
        packCounts.push_back(static_cast<uint32_t>(arm.packPatterns.size()));
        arrayCounts.push_back(static_cast<uint32_t>(arm.arrayPatterns.size()));
        // A guard is part of what this case says, so two cases that differ in it are two
        // different types. The compiler's tree of the predicate is the guard's identity: it is
        // built once per case written, and held for as long as the type lives.
        guards.push_back(arm.guard);
        if (arm.guard != nullptr) {
            gTypeCaseGuardBuilt = true;
        }
        key.push_back(arm.shapes);
        key.push_back(arm.identities.size());
        key.push_back(arm.packPatterns.size());
        key.push_back(arm.arrayPatterns.size());
        key.push_back(reinterpret_cast<uintptr_t>(arm.guard.get()));
        for (Type *identity : arm.identities) {
            params.push_back(identity);
            key.push_back(typeKeyWord(TypeTable::instance().canonical(identity)));
        }
        for (Type *pattern : arm.packPatterns) {
            params.push_back(pattern);
            key.push_back(typeKeyWord(TypeTable::instance().canonical(pattern)));
        }
        // An array pattern's identity is the object itself, not its canonical twin: the twin
        // erases the pack name, and the name is what the case binds (`E[...Dims]` and `E[...]`
        // are one shape and two different cases).
        for (Type *pattern : arm.arrayPatterns) {
            params.push_back(pattern);
            key.push_back(typeKeyWord(pattern));
        }
        params.push_back(arm.result);
        key.push_back(typeKeyWord(TypeTable::instance().canonical(arm.result)));
    }
    return static_cast<MatchTypeType *>(
        TypeTable::instance().internOther(typeCode(), key, [&]() -> Type * {
            Type **copy = OtherType::copyParams(params);
            void *mem   = mm::permSpace().alloc(sizeof(MatchTypeType), alignof(MatchTypeType));
            GUARD(mem != nullptr, "Failed to allocate MatchTypeType from permSpace");
            return new (mem) MatchTypeType(
                shapes,
                identityCounts,
                packCounts,
                arrayCounts,
                params.size(),
                copy,
                std::move(guards),
                std::vector<std::pair<std::string, Type *>>(
                    pendingGuardBindings.begin(),
                    pendingGuardBindings.end()));
        }));
}

size_t MatchTypeType::firstParamOf(size_t arm) const {
    size_t at = 1;
    for (size_t i = 0; i < arm; ++i) {
        at += identityCounts_[i] + packCounts_[i] + arrayCounts_[i] + 1;
    }
    return at;
}

std::span<Type *const> MatchTypeType::armIdentities(size_t arm) const {
    return std::span<Type *const>(params_ + firstParamOf(arm), identityCounts_[arm]);
}

std::span<Type *const> MatchTypeType::armPackPatterns(size_t arm) const {
    return std::span<Type *const>(
        params_ + firstParamOf(arm) + identityCounts_[arm],
        packCounts_[arm]);
}

std::span<Type *const> MatchTypeType::armArrayPatterns(size_t arm) const {
    return std::span<Type *const>(
        params_ + firstParamOf(arm) + identityCounts_[arm] + packCounts_[arm],
        arrayCounts_[arm]);
}

void MatchTypeType::armBoundNames(size_t arm, std::vector<std::string> &names) const {
    const auto add = [&names](std::string_view name) {
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
            names.emplace_back(name);
        }
    };
    // A pack pattern is written out of type variables -- `(W, ...Rest)` -- and the ones it holds
    // are the ones taking it apart binds. An array pattern binds the type variable written where
    // the element stands, and the pack it names for the dimensions.
    for (Type *pattern : armPackPatterns(arm)) {
        collectTypeVars(pattern, names);
    }
    for (Type *pattern : armArrayPatterns(arm)) {
        collectTypeVars(pattern, names);
        if (const ArrayType *array = asArrayType(pattern)) {
            add(array->packName());
        }
    }
}

Type *MatchTypeType::armResult(size_t arm) const {
    return params_[firstParamOf(arm) + identityCounts_[arm] + packCounts_[arm] + arrayCounts_[arm]];
}

std::string MatchTypeType::toString() const {
    std::string text = "match type " + subject()->toString() + " {";
    for (size_t i = 0; i < armCount(); ++i) {
        const std::span<Type *const> identities = armIdentities(i);
        const std::span<Type *const> pack       = armPackPatterns(i);
        const std::span<Type *const> arrays     = armArrayPatterns(i);
        text += " case ";
        if (identities.empty() && pack.empty() && arrays.empty()) {
            text += matchShapeText(armShapes(i));
        }
        size_t written = 0;
        for (Type *identity : identities) {
            text += (written++ == 0 ? "" : " | ") + identity->toString();
        }
        for (Type *pattern : pack) {
            text += (written++ == 0 ? "" : " | ") + pattern->toString();
        }
        for (Type *pattern : arrays) {
            text += (written++ == 0 ? "" : " | ") + pattern->toString();
        }
        if (const auto &guard = armGuard(i)) {
            // The guard is written before the type the case gives: `case _ when c => T`.
            text += " when " + (guard->text.empty() ? std::string("...") : guard->text);
        }
        text += " => " + armResult(i)->toString() + ";";
    }
    return text + " }";
}

std::string MatchTypeType::mangle() const {
    std::string text = "MT" + subject()->mangle();
    for (size_t i = 0; i < armCount(); ++i) {
        text += "_" + std::to_string(shapes_[i]) + "_" + std::to_string(identityCounts_[i]) + "_" +
                std::to_string(packCounts_[i]) + "_" + std::to_string(arrayCounts_[i]);
        for (Type *pattern : armPackPatterns(i)) {
            text += "P" + pattern->mangle();
        }
        for (Type *pattern : armArrayPatterns(i)) {
            text += "A" + pattern->mangle();
        }
        text += "_" + armResult(i)->mangle();
    }
    return text;
}

Type *MatchTypeType::clone(bool) const { return const_cast<MatchTypeType *>(this); }

bool MatchTypeType::equals(Type *type) const { return type == this; }

bool MatchTypeType::assignableFrom(Type *sourceType) const { return sourceType == this; }

CastSafety MatchTypeType::castSafetyFrom(Type *) const { return CastSafety::Forbidden; }

OtherType *MatchTypeType::cloneWithParams(std::span<Type *const> params) const {
    std::vector<MatchTypeArm> arms = armsFrom(params);
    // The guards are this case's own and are not among the params: a substitution of the
    // parameters keeps them, and keeps what they wait for, or a match that still waits for its
    // subject would lose the predicate it has to run once the subject is known.
    return create(
        params[0],
        arms,
        std::span<const std::pair<std::string, Type *>>(
            pendingGuardBindings_.data(),
            pendingGuardBindings_.size()));
}

MatchTypeType *MatchTypeType::withPendingGuardBindings(
    std::span<Type *const> params,
    std::span<const std::pair<std::string, Type *>> pending) const {
    std::vector<std::pair<std::string, Type *>> merged = pendingGuardBindings_;
    for (const auto &entry : pending) {
        if (std::none_of(merged.begin(), merged.end(), [&](const auto &held) {
                return held.first == entry.first;
            })) {
            merged.push_back(entry);
        }
    }
    if (merged.size() == pendingGuardBindings_.size()) {
        // Nothing new was bound: the match is what it was, with its params as substituted.
        return static_cast<MatchTypeType *>(cloneWithParams(params));
    }
    std::vector<MatchTypeArm> arms = armsFrom(params);
    return create(params[0], arms, merged);
}

std::vector<MatchTypeArm> MatchTypeType::armsFrom(std::span<Type *const> params) const {
    std::vector<MatchTypeArm> arms;
    size_t at = 1;
    for (size_t i = 0; i < armCount(); ++i) {
        MatchTypeArm arm;
        arm.shapes = shapes_[i];
        for (uint32_t k = 0; k < identityCounts_[i]; ++k) {
            arm.identities.push_back(params[at++]);
        }
        for (uint32_t k = 0; k < packCounts_[i]; ++k) {
            arm.packPatterns.push_back(params[at++]);
        }
        for (uint32_t k = 0; k < arrayCounts_[i]; ++k) {
            arm.arrayPatterns.push_back(params[at++]);
        }
        arm.result = params[at++];
        arm.guard = armGuard(i);
        arms.push_back(std::move(arm));
    }
    return arms;
}

bool describeUnevaluatedTypeCall(Type *type, std::string &what, std::string &why) {
    if (type == nullptr) {
        return false;
    }
    const auto concrete = [](Type *t) { return !containsTypeVar(t) && !containsTypeOf(t); };
    if (type->code() == TypeCallType::typeCode()) {
        const auto *call = static_cast<TypeCallType *>(type);
        if (call->name().starts_with(kLengthsTag)) {
            // The tag holds the expansion, then `|`, then the lengths of the packs it expands.
            const std::string text = call->toString();
            const size_t bar       = text.find('|');
            what                   = text.substr(0, bar);
            why = "the packs it expands have different lengths (" +
                  (bar == std::string::npos ? std::string() : text.substr(bar + 1)) + ")";
            return true;
        }
        // An argument that is itself a computation which failed is the cause.
        for (Type *argument : call->paramsSpan()) {
            if (describeUnevaluatedTypeCall(argument, what, why)) {
                return true;
            }
        }
        if (std::all_of(call->paramsSpan().begin(), call->paramsSpan().end(), concrete)) {
            if (TypeCallType::isPack(call)) {
                what = call->toString();
                why  = "a type pack is a list of types, and `...` writes its members where a type "
                       "list is";
                return true;
            }
            if (TypeCallType::isExpand(call)) {
                what = call->toString();
                why  = "`...` expands a type pack, and the type after it uses none";
                return true;
            }
            std::string error;
            if (TypeCallType::evaluate(call->name(), call->paramsSpan(), error) == nullptr) {
                what = call->toString();
                why  = error;
                return true;
            }
        }
        return false;
    }
    if (type->code() == MatchTypeType::typeCode()) {
        const auto *match = static_cast<MatchTypeType *>(type);
        if (describeUnevaluatedTypeCall(match->subject(), what, why)) {
            return true;
        }
        if (concrete(match->subject())) {
            what = "match type " + match->subject()->toString();
            why  = "no case selects " + match->subject()->toString();
            bool guarded = false;
            for (size_t arm = 0; arm < match->armCount(); ++arm) {
                guarded = guarded || match->armGuard(arm) != nullptr;
            }
            if (guarded && typeCaseGuardEvaluator()) {
                // Some cases were tried and their predicates did not hold: say so, rather than
                // leave a reader looking for the case that should have taken the type.
                why += ", and the guard of every case that matched it did not hold";
            }
            return true;
        }
        return false;
    }
    if (type->isOtherType()) {
        for (Type *param : static_cast<OtherType *>(type)->paramsSpan()) {
            if (describeUnevaluatedTypeCall(param, what, why)) {
                return true;
            }
        }
    }
    switch (type->code()) {
    case TypeCode::Array:
        return describeUnevaluatedTypeCall(static_cast<ArrayType *>(type)->elemType(), what, why);
    case TypeCode::Stream:
        return describeUnevaluatedTypeCall(static_cast<StreamType *>(type)->elemType(), what, why);
    case TypeCode::Tuple:
        for (Type *member : static_cast<TupleType *>(type)->types()) {
            if (describeUnevaluatedTypeCall(member, what, why)) {
                return true;
            }
        }
        return false;
    case TypeCode::Struct: {
        auto *record     = static_cast<StructType *>(type);
        const auto types = record->named() ? record->typeArgs() : record->types();
        for (Type *member : types) {
            if (describeUnevaluatedTypeCall(member, what, why)) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Function: {
        // A function type holds types too -- the `with` list, the parameters and the one it
        // answers -- and a type function left unevaluated in any of them is a type the program did
        // not ask for. Without this the check stopped at the edge of a function type, so
        // `Gradient<F>` for a function with no `with` parameter was a type the compiler accepted
        // while the same empty pack, written where a type stands, was refused.
        auto *func = static_cast<FunctionType *>(type);
        for (Type *member : func->withTypesSpan()) {
            if (describeUnevaluatedTypeCall(member, what, why)) {
                return true;
            }
        }
        for (Type *member : func->normTypesSpan()) {
            if (describeUnevaluatedTypeCall(member, what, why)) {
                return true;
            }
        }
        return func->hasExitType() && describeUnevaluatedTypeCall(func->exitType(), what, why);
    }
    default:
        return false;
    }
}

bool containsTypeCall(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Array:
        return containsTypeCall(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Stream:
        return containsTypeCall(static_cast<StreamType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto types = static_cast<TupleType *>(type)->types();
        return std::any_of(types.begin(), types.end(), containsTypeCall);
    }
    case TypeCode::Struct: {
        auto *record     = static_cast<StructType *>(type);
        const auto types = record->named() ? record->typeArgs() : record->types();
        return std::any_of(types.begin(), types.end(), containsTypeCall);
    }
    case TypeCode::Function: {
        // A function type is a type like a tuple is: what it is computed from is computed too.
        auto *func       = static_cast<FunctionType *>(type);
        const auto withs = func->withTypesSpan();
        const auto norms = func->normTypesSpan();
        return std::any_of(withs.begin(), withs.end(), containsTypeCall) ||
               std::any_of(norms.begin(), norms.end(), containsTypeCall) ||
               (func->hasExitType() && containsTypeCall(func->exitType()));
    }
    default:
        break;
    }
    if (type->code() == TypeCallType::typeCode() || type->code() == MatchTypeType::typeCode()) {
        return true;
    }
    if (type->isOtherType()) {
        const auto params = static_cast<OtherType *>(type)->paramsSpan();
        return std::any_of(params.begin(), params.end(), containsTypeCall);
    }
    return false;
}

bool containsGuardedTypeMatch(Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Array:
        return containsGuardedTypeMatch(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Stream:
        return containsGuardedTypeMatch(static_cast<StreamType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto types = static_cast<TupleType *>(type)->types();
        return std::any_of(types.begin(), types.end(), containsGuardedTypeMatch);
    }
    case TypeCode::Struct: {
        auto *record     = static_cast<StructType *>(type);
        const auto types = record->named() ? record->typeArgs() : record->types();
        return std::any_of(types.begin(), types.end(), containsGuardedTypeMatch);
    }
    case TypeCode::Function: {
        auto *func       = static_cast<FunctionType *>(type);
        const auto withs = func->withTypesSpan();
        const auto norms = func->normTypesSpan();
        return std::any_of(withs.begin(), withs.end(), containsGuardedTypeMatch) ||
               std::any_of(norms.begin(), norms.end(), containsGuardedTypeMatch) ||
               (func->hasExitType() && containsGuardedTypeMatch(func->exitType()));
    }
    default:
        break;
    }
    if (type->code() == MatchTypeType::typeCode()) {
        const auto *match = static_cast<const MatchTypeType *>(type);
        for (size_t arm = 0; arm < match->armCount(); ++arm) {
            if (match->armGuard(arm) != nullptr) {
                return true;
            }
        }
        return false;
    }
    if (type->isOtherType()) {
        for (Type *param : static_cast<OtherType *>(type)->paramsSpan()) {
            if (containsGuardedTypeMatch(param)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace camel::core::type
