// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/compile/gct/types.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"
#include <algorithm>
#include <string_view>
#include <stdexcept>

namespace camel::compile::gct {

std::string to_string(NodeKind kind) {
    switch (kind) {
    case NodeKind::DECL:
        return "DECL";
    case NodeKind::FUNC:
        return "FUNC";
    case NodeKind::DATA:
        return "DATA";
    case NodeKind::TYPE:
        return "TYPE";
    case NodeKind::NREF:
        return "NREF";
    case NodeKind::DREF:
        return "DREF";
    case NodeKind::VARI:
        return "VARI";
    case NodeKind::LINK:
        return "LINK";
    case NodeKind::WITH:
        return "WITH";
    case NodeKind::TAPP:
        return "TAPP";
    case NodeKind::ACCS:
        return "ACCS";
    case NodeKind::BRCH:
        return "BRCH";
    case NodeKind::CASE:
        return "CASE";
    case NodeKind::CAST:
        return "CAST";
    case NodeKind::ANNO:
        return "ANNO";
    case NodeKind::EXIT:
        return "EXIT";
    case NodeKind::EXEC:
        return "EXEC";
    case NodeKind::EXPT:
        return "EXPT";
    case NodeKind::LOOP:
        return "LOOP";
    case NodeKind::TMTCH:
        return "TMTCH";
    case NodeKind::TCASE:
        return "TCASE";
    case NodeKind::SFOR:
        return "SFOR";
    case NodeKind::SACC:
        return "SACC";
    case NodeKind::SPRD:
        return "SPRD";
    case NodeKind::SCOMP:
        return "SCOMP";
    case NodeKind::SLOOP:
        return "SLOOP";
    case NodeKind::ASSIGN:
        return "ASSIGN";
    default:
        throw std::runtime_error("Unknown NodeKind");
    }
    return "UNKNOWN";
}

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::OtherType;
using camel::core::type::StreamType;
using camel::core::type::StructType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::core::type::TypeVarType;

std::string to_string(AssignmentForm form) {
    switch (form) {
    case AssignmentForm::Plain:
        return "Plain";
    case AssignmentForm::Compound:
        return "Compound";
    case AssignmentForm::Rebuilt:
        return "Rebuilt";
    case AssignmentForm::Owned:
        return "Owned";
    }
    throw std::runtime_error("Unknown AssignmentForm");
}

bool isTypeOfPlaceholder(const Type *type) {
    if (type == nullptr || type->code() != TypeCode::TypeVar) {
        return false;
    }
    const auto *variable = static_cast<const TypeVarType *>(type);
    return std::string_view(variable->name()).starts_with(kTypeOfPlaceholderPrefix);
}

bool hasTypeOfPlaceholder(const Type *type) {
    // The positions walked here are the ones the type layer walked when it held the placeholder
    // itself: a whole type, the element of an array or a stream, a tuple's members, a struct's
    // fields or type arguments, a function's parameters and result, and the parameters of any
    // other type (`TypeCallType`'s arguments and `MatchTypeType`'s subject among them). A type
    // function is not walked: its body is a type of its own, applied elsewhere.
    if (type == nullptr) {
        return false;
    }
    if (isTypeOfPlaceholder(type)) {
        return true;
    }
    switch (type->code()) {
    case TypeCode::TypeFunc:
        return false;
    case TypeCode::Array:
        return hasTypeOfPlaceholder(static_cast<const ArrayType *>(type)->elemType());
    case TypeCode::Stream:
        return hasTypeOfPlaceholder(static_cast<const StreamType *>(type)->elemType());
    case TypeCode::Tuple: {
        const auto members = static_cast<const TupleType *>(type)->types();
        return std::any_of(members.begin(), members.end(), hasTypeOfPlaceholder);
    }
    case TypeCode::Struct: {
        const auto *record = static_cast<const StructType *>(type);
        const auto members = record->named() ? record->typeArgs() : record->types();
        return std::any_of(members.begin(), members.end(), hasTypeOfPlaceholder);
    }
    case TypeCode::Function: {
        const auto *function = static_cast<const FunctionType *>(type);
        const auto with      = function->withTypesSpan();
        const auto norm      = function->normTypesSpan();
        return std::any_of(with.begin(), with.end(), hasTypeOfPlaceholder) ||
               std::any_of(norm.begin(), norm.end(), hasTypeOfPlaceholder) ||
               (function->hasExitType() && hasTypeOfPlaceholder(function->exitType()));
    }
    default:
        break;
    }
    if (type->isOtherType()) {
        const auto params = static_cast<const OtherType *>(type)->paramsSpan();
        return std::any_of(params.begin(), params.end(), hasTypeOfPlaceholder);
    }
    return false;
}

void collectTypeVarsWithoutPlaceholders(Type *type, std::vector<std::string> &names) {
    const size_t from = names.size();
    collectTypeVars(type, names);
    names.erase(
        std::remove_if(
            names.begin() + static_cast<std::ptrdiff_t>(from),
            names.end(),
            [](const std::string &name) {
                return std::string_view(name).starts_with(kTypeOfPlaceholderPrefix);
            }),
        names.end());
}

} // namespace camel::compile::gct
