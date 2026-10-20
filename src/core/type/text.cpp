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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The text, the mangled name and the structural equality of a type, on an explicit stack
 * (camel/core/type/text.h has the reason).
 *
 * The three walks have the same shape: a work list holds the types still to write or compare.
 * A type that is not one of the built-in composites is answered by its own virtual method, so
 * module types keep whatever text they define.
 */

#include "camel/core/type/text.h"

#include "camel/core/type/base.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

namespace {

bool isStructured(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Array:
    case TypeCode::Tuple:
    case TypeCode::Struct:
    case TypeCode::Function:
        return true;
    default:
        break;
    }
    if (asCollectionTypeOrPrototype(type) != nullptr) {
        return true;
    }
    return asEnumTypeOrPrototype(const_cast<Type *>(type)) != nullptr;
}

/// One item of a text walk: write a literal, or write a type (which may expand to more items).
struct TextItem {
    bool isType = false;
    const Type *type = nullptr;
    std::string text;
};

TextItem literal(std::string text) {
    TextItem item;
    item.text = std::move(text);
    return item;
}

TextItem typeItem(const Type *type) {
    TextItem item;
    item.isType = true;
    item.type  = type;
    return item;
}

/// Appends `parts`, in order, onto a work list that is read from the back.
void pushParts(std::vector<TextItem> &stack, std::vector<TextItem> parts) {
    for (size_t i = parts.size(); i-- > 0;) {
        stack.push_back(std::move(parts[i]));
    }
}

/// `parts` for one type in output order; `mangle` selects the mangled name instead of the text.
std::vector<TextItem> typeParts(const Type *type, bool mangle);

std::vector<TextItem> tupleParts(const TupleType *tuple, bool mangle) {
    std::vector<TextItem> parts;
    if (mangle) {
        if (tuple->isCell()) {
            parts.push_back(literal("V"));
            parts.push_back(typeItem(tuple->typeAt(0)));
        } else if (tuple->isTagged()) {
            parts.push_back(literal(tuple->isNullable() ? "N" : "E"));
            parts.push_back(typeItem(tuple->typeAt(0)));
        } else {
            parts.push_back(literal("P" + std::to_string(tuple->size())));
            for (size_t i = 0; i < tuple->size(); ++i) {
                Type *member = tuple->typeAt(i);
                parts.push_back(member != nullptr ? typeItem(member) : literal("N"));
            }
        }
        return parts;
    }
    if (tuple->isCell()) {
        parts.push_back(literal("var "));
        parts.push_back(typeItem(tuple->typeAt(0)));
        return parts;
    }
    if (tuple->isTagged()) {
        const bool parens = tuple->typeAt(0)->code() == TypeCode::Function;
        if (parens) {
            parts.push_back(literal("("));
        }
        parts.push_back(typeItem(tuple->typeAt(0)));
        if (parens) {
            parts.push_back(literal(tuple->isNullable() ? ")?" : ")!"));
        } else {
            parts.push_back(literal(tuple->isNullable() ? "?" : "!"));
        }
        return parts;
    }
    parts.push_back(literal("("));
    for (size_t i = 0; i < tuple->size(); ++i) {
        if (i > 0) {
            parts.push_back(literal(", "));
        }
        Type *member = tuple->typeAt(i);
        parts.push_back(member != nullptr ? typeItem(member) : literal("null"));
    }
    parts.push_back(literal(")"));
    return parts;
}

std::vector<TextItem> structParts(const StructType *record, bool mangle) {
    std::vector<TextItem> parts;
    if (mangle) {
        parts.push_back(literal("S" + std::to_string(record->size())));
        for (size_t i = 0; i < record->size(); ++i) {
            const std::string name(record->fieldName(i));
            parts.push_back(literal(std::to_string(name.size()) + name));
            Type *member = record->typeAt(i);
            parts.push_back(member != nullptr ? typeItem(member) : literal("N"));
        }
        return parts;
    }
    parts.push_back(literal("{ "));
    for (size_t i = 0; i < record->size(); ++i) {
        if (i > 0) {
            parts.push_back(literal(", "));
        }
        parts.push_back(literal(std::string(record->fieldName(i)) + ": "));
        Type *member = record->typeAt(i);
        parts.push_back(member != nullptr ? typeItem(member) : literal("<null>"));
    }
    parts.push_back(literal(" }"));
    return parts;
}

std::vector<TextItem> functionParts(const FunctionType *fn, bool mangle) {
    std::vector<TextItem> parts;
    const size_t withCount = fn->withTypesCount();
    const size_t normCount = fn->normTypesCount();
    if (mangle) {
        parts.push_back(literal("F"));
        if (withCount > 0) {
            parts.push_back(literal("W" + std::to_string(withCount)));
            for (size_t i = 0; i < withCount; ++i) {
                if (fn->withIsVarAt(i)) {
                    parts.push_back(literal("V"));
                }
                parts.push_back(typeItem(fn->withTypeAt(i)));
            }
        }
        if (normCount > 0) {
            parts.push_back(literal("N" + std::to_string(normCount)));
            for (size_t i = 0; i < normCount; ++i) {
                if (fn->normIsVarAt(i)) {
                    parts.push_back(literal("V"));
                }
                parts.push_back(typeItem(fn->normTypeAt(i)));
            }
        }
        parts.push_back(literal("R"));
        parts.push_back(fn->hasExitType() ? typeItem(fn->exitType()) : literal("v"));
        return parts;
    }
    if (fn->implMark() != ImplMark::Graph) {
        parts.push_back(literal(::to_string(fn->implMark()) + " "));
    }
    const std::string modifiers = static_cast<std::string>(fn->modifiers());
    if (!modifiers.empty()) {
        parts.push_back(literal(modifiers + " "));
    }
    if (withCount > 0) {
        parts.push_back(literal("<"));
        for (size_t i = 0; i < withCount; ++i) {
            if (i > 0) {
                parts.push_back(literal(", "));
            }
            if (fn->withIsVarAt(i)) {
                parts.push_back(literal("var "));
            }
            if (fn->hasArgNames()) {
                parts.push_back(literal(std::string(fn->argNameAt(i)) + ": "));
            }
            parts.push_back(typeItem(fn->withTypeAt(i)));
        }
        parts.push_back(literal("> "));
    }
    parts.push_back(literal("("));
    for (size_t i = 0; i < normCount; ++i) {
        if (i > 0) {
            parts.push_back(literal(", "));
        }
        if (fn->normIsVarAt(i)) {
            parts.push_back(literal("var "));
        }
        if (fn->hasArgNames()) {
            parts.push_back(literal(std::string(fn->argNameAt(withCount + i)) + ": "));
        }
        parts.push_back(typeItem(fn->normTypeAt(i)));
    }
    parts.push_back(literal(") => "));
    parts.push_back(fn->hasExitType() ? typeItem(fn->exitType()) : literal("<null>"));
    return parts;
}

std::vector<TextItem> collectionParts(const CollectionType *coll, bool mangle) {
    std::vector<TextItem> parts;
    const bool isMap = coll->isMap();
    if (mangle) {
        if (!coll->complete()) {
            parts.push_back(literal(isMap ? "Dm0" : "Ds0"));
            return parts;
        }
        parts.push_back(literal(isMap ? "Dm" : "Ds"));
        parts.push_back(typeItem(coll->keyType()));
        if (isMap) {
            parts.push_back(typeItem(coll->valueType()));
        }
        return parts;
    }
    if (!coll->complete()) {
        parts.push_back(literal(isMap ? "Map" : "Set"));
        return parts;
    }
    parts.push_back(literal(isMap ? "Map<" : "Set<"));
    parts.push_back(typeItem(coll->keyType()));
    if (isMap) {
        parts.push_back(literal(", "));
        parts.push_back(typeItem(coll->valueType()));
    }
    parts.push_back(literal(">"));
    return parts;
}

std::vector<TextItem> enumParts(const EnumType *enumType, bool mangle) {
    std::vector<TextItem> parts;
    if (mangle) {
        parts.push_back(literal("E" + std::to_string(enumType->decl()->id())));
        if (enumType->paramCount() == 0) {
            parts.push_back(literal("P"));
            return parts;
        }
        for (size_t i = 0; i < enumType->paramCount(); ++i) {
            parts.push_back(literal("_"));
            Type *argument = enumType->params()[i];
            parts.push_back(argument != nullptr ? typeItem(argument) : literal("H"));
        }
        return parts;
    }
    parts.push_back(literal(enumType->decl()->name()));
    if (enumType->paramCount() == 0) {
        return parts;
    }
    parts.push_back(literal("<"));
    for (size_t i = 0; i < enumType->paramCount(); ++i) {
        if (i > 0) {
            parts.push_back(literal(", "));
        }
        Type *argument = enumType->params()[i];
        parts.push_back(argument != nullptr ? typeItem(argument) : literal("_"));
    }
    parts.push_back(literal(">"));
    return parts;
}

std::vector<TextItem> typeParts(const Type *type, bool mangle) {
    switch (type->code()) {
    case TypeCode::Array:
        return {typeItem(static_cast<const ArrayType *>(type)->elemType()), literal("[]")};
    case TypeCode::Tuple:
        return tupleParts(static_cast<const TupleType *>(type), mangle);
    case TypeCode::Struct:
        return structParts(static_cast<const StructType *>(type), mangle);
    case TypeCode::Function:
        return functionParts(static_cast<const FunctionType *>(type), mangle);
    default:
        break;
    }
    if (const CollectionType *coll = asCollectionTypeOrPrototype(type)) {
        return collectionParts(coll, mangle);
    }
    if (const EnumType *enumType = asEnumTypeOrPrototype(const_cast<Type *>(type))) {
        return enumParts(enumType, mangle);
    }
    return {};
}

std::string typeText(const Type *type, bool mangle) {
    if (type == nullptr) {
        return mangle ? "N" : "<null>";
    }
    if (!isStructured(type)) {
        return mangle ? type->mangle() : type->toString();
    }
    std::string out;
    std::vector<TextItem> stack;
    {
        TextItem root;
        root.isType = true;
        root.type   = type;
        stack.push_back(std::move(root));
    }
    while (!stack.empty()) {
        TextItem item = std::move(stack.back());
        stack.pop_back();
        if (!item.isType) {
            out += item.text;
            continue;
        }
        const Type *current = item.type;
        if (!isStructured(current)) {
            out += mangle ? current->mangle() : current->toString();
            continue;
        }
        pushParts(stack, typeParts(current, mangle));
    }
    return out;
}

bool sameType(const Type *a, const Type *b) {
    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr) {
        return false;
    }
    if (!isStructured(a) && !isStructured(b)) {
        return a->equals(const_cast<Type *>(b));
    }
    if (!isStructured(a) || !isStructured(b)) {
        return false;
    }
    if (a->code() != b->code()) {
        return false;
    }
    std::vector<std::pair<const Type *, const Type *>> pending;
    pending.emplace_back(a, b);
    while (!pending.empty()) {
        const auto [left, right] = pending.back();
        pending.pop_back();
        if (left == right) {
            continue;
        }
        if (left == nullptr || right == nullptr) {
            return false;
        }
        if (!isStructured(left) && !isStructured(right)) {
            if (!left->equals(const_cast<Type *>(right))) {
                return false;
            }
            continue;
        }
        if (!isStructured(left) || !isStructured(right)) {
            return false;
        }
        if (left->code() != right->code()) {
            return false;
        }
        switch (left->code()) {
        case TypeCode::Array: {
            pending.emplace_back(
                static_cast<const ArrayType *>(left)->elemType(),
                static_cast<const ArrayType *>(right)->elemType());
            break;
        }
        case TypeCode::Tuple: {
            const auto *lhs = static_cast<const TupleType *>(left);
            const auto *rhs = static_cast<const TupleType *>(right);
            if (lhs->size() != rhs->size() || lhs->isCell() != rhs->isCell() ||
                lhs->tag() != rhs->tag()) {
                return false;
            }
            for (size_t i = 0; i < lhs->size(); ++i) {
                pending.emplace_back(lhs->typeAt(i), rhs->typeAt(i));
            }
            break;
        }
        case TypeCode::Struct: {
            const auto *lhs = static_cast<const StructType *>(left);
            const auto *rhs = static_cast<const StructType *>(right);
            if (lhs->size() != rhs->size()) {
                return false;
            }
            for (size_t i = 0; i < lhs->size(); ++i) {
                const std::string name(lhs->fieldName(i));
                const std::optional<size_t> index = rhs->findField(name);
                if (!index.has_value()) {
                    return false;
                }
                const Type *lhsMember = lhs->typeAt(i);
                const Type *rhsMember = rhs->typeAt(*index);
                if (lhsMember == nullptr || rhsMember == nullptr) {
                    return false;
                }
                pending.emplace_back(lhsMember, rhsMember);
            }
            break;
        }
        case TypeCode::Function: {
            const auto *lhs = static_cast<const FunctionType *>(left);
            const auto *rhs = static_cast<const FunctionType *>(right);
            if (lhs->withTypesCount() != rhs->withTypesCount() ||
                lhs->normTypesCount() != rhs->normTypesCount()) {
                return false;
            }
            for (size_t i = 0; i < lhs->withTypesCount(); ++i) {
                if (lhs->withIsVarAt(i) != rhs->withIsVarAt(i)) {
                    return false;
                }
                pending.emplace_back(lhs->withTypeAt(i), rhs->withTypeAt(i));
            }
            for (size_t i = 0; i < lhs->normTypesCount(); ++i) {
                if (lhs->normIsVarAt(i) != rhs->normIsVarAt(i)) {
                    return false;
                }
                pending.emplace_back(lhs->normTypeAt(i), rhs->normTypeAt(i));
            }
            if (lhs->hasExitType() != rhs->hasExitType()) {
                return false;
            }
            if (lhs->hasExitType()) {
                pending.emplace_back(lhs->exitType(), rhs->exitType());
            }
            break;
        }
        default: {
            if (const CollectionType *lhs = asCollectionTypeOrPrototype(left)) {
                const auto *rhs = static_cast<const CollectionType *>(right);
                if (lhs->paramCount() != rhs->paramCount()) {
                    return false;
                }
                for (size_t i = 0; i < lhs->paramCount(); ++i) {
                    pending.emplace_back(lhs->params()[i], rhs->params()[i]);
                }
                break;
            }
            const auto *lhs = asEnumTypeOrPrototype(const_cast<Type *>(left));
            const auto *rhs = asEnumTypeOrPrototype(const_cast<Type *>(right));
            if (lhs == nullptr || rhs == nullptr || lhs->decl() != rhs->decl() ||
                lhs->paramCount() != rhs->paramCount()) {
                return false;
            }
            for (size_t i = 0; i < lhs->paramCount(); ++i) {
                pending.emplace_back(lhs->params()[i], rhs->params()[i]);
            }
            break;
        }
        }
    }
    return true;
}

} // namespace

std::string typeToString(const Type *type) { return typeText(type, false); }

std::string typeMangle(const Type *type) { return typeText(type, true); }

bool typeEquals(const Type *a, const Type *b) { return sameType(a, b); }

} // namespace camel::core::type