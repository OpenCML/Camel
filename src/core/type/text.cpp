// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/intern.h"

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

namespace {

/// How two resource clauses are compared inside the structural walk: `equals` compares them as
/// they stand, and the relation a written type asks of a value lets a clause nobody wrote take
/// the resources of the other side (text.h, sameTypeWithUnwrittenResources).
using ResourceMatch = bool (*)(
    const sem::ResourceSignature &target, const sem::ResourceSignature &source);

bool sameResources(const sem::ResourceSignature &left, const sem::ResourceSignature &right) {
    return left == right;
}

bool takesUnwrittenResources(
    const sem::ResourceSignature &target, const sem::ResourceSignature &source) {
    return target.mode == sem::ResourceSignature::Mode::Generic || target == source;
}

bool isStructured(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->code()) {
    case TypeCode::Array:
    case TypeCode::Tuple:
    case TypeCode::Struct:
    case TypeCode::Function:
    case TypeCode::Stream:
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

/// Whether the text of a function type ends in a `uses` clause, of its own or of the function it
/// returns. The clause reads on through a comma and an identifier, so a type that ends in one is
/// written between parentheses wherever something can follow it in the same list.
bool endsInUses(const Type *type) {
    while (type != nullptr && type->code() == TypeCode::Function) {
        const auto *fn                    = static_cast<const FunctionType *>(type);
        const sem::ResourceSignature &sig = fn->resources();
        if (sig.mode != sem::ResourceSignature::Mode::Generic) {
            return !sig.pure();
        }
        if (!fn->hasExitType()) {
            return false;
        }
        type = fn->exitType();
    }
    return false;
}

/// Whether the element of an array is written between parentheses: a tagged type and a function
/// type reach past the dimensions that follow (`T?[3]` is not in the grammar, and
/// `(x: int) => int[]` is a function returning an array).
bool needsParensAsElement(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::Function) {
        return true;
    }
    if (type->code() == TypeCode::Tuple) {
        const auto *tuple = static_cast<const TupleType *>(type);
        return tuple->isTagged();
    }
    return false;
}

/// Appends `parts`, in order, onto a work list that is read from the back.
void pushParts(std::vector<TextItem> &stack, std::vector<TextItem> parts) {
    for (size_t i = parts.size(); i-- > 0;) {
        stack.push_back(std::move(parts[i]));
    }
}

/// `parts` for one type in output order; `mangle` selects the mangled name instead of the text.
std::vector<TextItem> typeParts(const Type *type, bool mangle);

/// A type that stands as a member of a list (a tuple, a struct, a parameter list, type
/// arguments): between parentheses when it ends in a `uses` clause, which would take the next
/// member for one more resource.
void pushChild(std::vector<TextItem> &parts, const Type *type, const char *absent) {
    if (type == nullptr) {
        parts.push_back(literal(absent));
        return;
    }
    if (endsInUses(type)) {
        parts.push_back(literal("("));
        parts.push_back(typeItem(type));
        parts.push_back(literal(")"));
        return;
    }
    parts.push_back(typeItem(type));
}

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
        pushChild(parts, tuple->typeAt(i), "null");
    }
    if (tuple->size() == 1) {
        // `(T)` is the parenthesized type; the one-tuple has the comma.
        parts.push_back(literal(","));
    }
    parts.push_back(literal(")"));
    return parts;
}

std::vector<TextItem> structParts(const StructType *record, bool mangle) {
    std::vector<TextItem> parts;
    if (record->named()) {
        // A named struct type is printed by its name, as an enum is: the declaration is the
        // type (design principles 5.2). A generic instance prints its arguments.
        const StructDecl *decl = record->decl();
        if (mangle) {
            parts.push_back(literal("S" + std::to_string(decl->id())));
            if (record->typeArgCount() == 0) {
                parts.push_back(literal("P"));
                return parts;
            }
            for (size_t i = 0; i < record->typeArgCount(); ++i) {
                parts.push_back(literal("_"));
                Type *argument = record->typeArgAt(i);
                parts.push_back(argument != nullptr ? typeItem(argument) : literal("H"));
            }
            return parts;
        }
        parts.push_back(literal(decl->name()));
        if (record->typeArgCount() == 0) {
            return parts;
        }
        parts.push_back(literal("<"));
        for (size_t i = 0; i < record->typeArgCount(); ++i) {
            if (i > 0) {
                parts.push_back(literal(", "));
            }
            pushChild(parts, record->typeArgAt(i), "_");
        }
        parts.push_back(literal(">"));
        return parts;
    }
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
        pushChild(parts, record->typeAt(i), "<null>");
    }
    parts.push_back(literal(" }"));
    return parts;
}

/// The resource clause of a function type, as written: "pure" or "uses s, fs". `mangle` writes
/// the stable encoding that takes part in the mangled name instead.
std::string resourceClause(const FunctionType *fn, bool mangle) {
    const sem::ResourceSignature &sig = fn->resources();
    if (sig.mode == sem::ResourceSignature::Mode::Generic) {
        return {};
    }
    std::string out;
    if (mangle) {
        out = "K";
        out += std::to_string(static_cast<int>(sig.mode));
        out += "P" + std::to_string(sig.params.size()) + "C" +
               std::to_string(sig.captures.size()) + "A" + std::to_string(sig.ambient.size()) +
               "Y" + std::to_string(sig.anyOf.size());
        for (const auto &use : sig.params) {
            out += std::format(
                "p{}{}{}{}{}",
                use.index,
                static_cast<int>(use.cls),
                static_cast<int>(use.mode),
                use.name.size(),
                use.name);
        }
        for (uint8_t capture : sig.captures) {
            out += "c" + std::to_string(capture);
        }
        for (const auto &use : sig.ambient) {
            out += "a" + std::to_string(static_cast<int>(use.mode)) +
                   std::to_string(use.name.size()) + use.name;
        }
        for (sem::ResourceClass cls : sig.anyOf) {
            out += "y" + std::to_string(static_cast<int>(cls));
        }
        out += "r" + std::to_string(static_cast<int>(sig.result.kind)) +
               std::to_string(sig.result.operand);
        out += sig.allocates
                   ? "o" + std::to_string(static_cast<int>(sig.allocates->cls)) +
                         std::to_string(sig.allocates->name.size()) + sig.allocates->name
                   : "o-";
        out += sig.external ? "e" : "-";
        return out;
    }
    if (sig.pure()) {
        return "pure";
    }
    out = "uses ";
    bool first = true;
    const auto add = [&](const std::string &name) {
        out += (first ? "" : ", ") + name;
        first = false;
    };
    for (const auto &use : sig.params) {
        if (fn->hasArgNames() &&
            use.index < fn->withTypesCount() + fn->normTypesCount()) {
            add(std::string(fn->argNameAt(use.index)));
        } else {
            add(std::format("#{}", use.index));
        }
    }
    for (const auto &use : sig.ambient) {
        add(use.name);
    }
    for (sem::ResourceClass cls : sig.anyOf) {
        add(std::format("any {}", sem::resourceClassName(cls)));
    }
    for (uint8_t capture : sig.captures) {
        add(std::format("capture {}", capture));
    }
    return out;
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
            parts.push_back(literal((fn->hasRest() ? "X" : "") + std::string("N") +
                                    std::to_string(normCount)));
            if (fn->hasConstas()) {
                parts.push_back(literal("C" + std::to_string(fn->constasCount())));
                for (uint16_t index : fn->constasIndices()) {
                    parts.push_back(literal(std::to_string(index)));
                }
            }
            for (size_t i = 0; i < normCount; ++i) {
                if (fn->normIsVarAt(i)) {
                    parts.push_back(literal("V"));
                }
                parts.push_back(typeItem(fn->normTypeAt(i)));
            }
        } else if (fn->hasRest()) {
            parts.push_back(literal("X"));
        }
        parts.push_back(literal("R"));
        parts.push_back(fn->hasExitType() ? typeItem(fn->exitType()) : literal("v"));
        const std::string resources = resourceClause(fn, true);
        if (!resources.empty()) {
            parts.push_back(literal(resources));
        }
        return parts;
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
            pushChild(parts, fn->withTypeAt(i), "<null>");
        }
        parts.push_back(literal("> "));
    }
    parts.push_back(literal("("));
    for (size_t i = 0; i < normCount; ++i) {
        if (i > 0) {
            parts.push_back(literal(", "));
        }
        if (fn->hasRest() && i + 1 == normCount) {
            parts.push_back(literal(".."));
            if (fn->hasArgNames()) {
                parts.push_back(literal(std::string(fn->argNameAt(withCount + i))));
            }
            continue;
        }
        if (fn->normIsConstasAt(i)) {
            parts.push_back(literal("constas "));
        }
        if (fn->normIsVarAt(i)) {
            parts.push_back(literal("var "));
        }
        if (fn->hasArgNames()) {
            parts.push_back(literal(std::string(fn->argNameAt(withCount + i)) + ": "));
        }
        pushChild(parts, fn->normTypeAt(i), "<null>");
    }
    parts.push_back(literal(") => "));
    const std::string resources = resourceClause(fn, false);
    if (fn->hasExitType() && !resources.empty() && fn->exitType()->code() == TypeCode::Function) {
        // The clause after a returned function type would be read as the returned type's.
        parts.push_back(literal("("));
        parts.push_back(typeItem(fn->exitType()));
        parts.push_back(literal(")"));
    } else if (fn->hasExitType()) {
        parts.push_back(typeItem(fn->exitType()));
    } else {
        parts.push_back(literal("<null>"));
    }
    if (!resources.empty()) {
        parts.push_back(literal(" " + resources));
    }
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
    pushChild(parts, coll->keyType(), "<null>");
    if (isMap) {
        parts.push_back(literal(", "));
        pushChild(parts, coll->valueType(), "<null>");
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
        pushChild(parts, enumType->params()[i], "_");
    }
    parts.push_back(literal(">"));
    return parts;
}

std::vector<TextItem> typeParts(const Type *type, bool mangle) {
    switch (type->code()) {
    case TypeCode::Array: {
        const auto *array = static_cast<const ArrayType *>(type);
        if (mangle) {
            return {
                literal("Array_"),
                typeItem(array->elemType()),
                literal("_" + array->shapeMangle())};
        }
        // An array whose element type nobody stated is the empty literal `[]`, printed as the
        // program wrote it: the dimensions it left open are not a shape to print, and `void` is
        // the "no value" marker rather than the element type (design principles 5.1, 5.2). The
        // position the literal stands in is what states an element type, so this spelling is
        // what a reader of a message is given to write.
        if (array->elemType() != nullptr && array->elemType()->code() == TypeCode::Void) {
            return {literal("[]")};
        }
        if (needsParensAsElement(array->elemType())) {
            return {
                literal("("), typeItem(array->elemType()), literal(")" + array->shapeText())};
        }
        return {typeItem(array->elemType()), literal(array->shapeText())};
    }
    case TypeCode::Tuple:
        return tupleParts(static_cast<const TupleType *>(type), mangle);
    case TypeCode::Stream: {
        const auto *stream = static_cast<const StreamType *>(type);
        if (mangle) {
            return {literal("Stream_"), typeItem(stream->elemType())};
        }
        // One spelling, `T..`, wherever the type is printed. The element of a stream can also
        // be a function type, and `(x: int) => int..` would read as an interval inside the
        // result: the element that reaches past the suffix is parenthesized, as the payload of
        // a tagged tuple is.
        if (stream->elemType() != nullptr && stream->elemType()->code() == TypeCode::Function) {
            return {literal("("), typeItem(stream->elemType()), literal(")..")};
        }
        return {typeItem(stream->elemType()), literal("..")};
    }
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

bool sameTypeWith(const Type *a, const Type *b, ResourceMatch resources) {
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
            const auto *lhs = static_cast<const ArrayType *>(left);
            const auto *rhs = static_cast<const ArrayType *>(right);
            // The shape is part of the type: a fixed rank and an open rank are different
            // types, and so are two different constant extents. An extent that is not a
            // constant asks for nothing at the call, so two such extents are the same
            // dimension here; dimension names are signature metadata, as parameter names are
            // for function types (this is what the tensor type's equality did before).
            if (lhs->anyRank() != rhs->anyRank() ||
                lhs->extents().size() != rhs->extents().size()) {
                return false;
            }
            for (size_t i = 0; i < lhs->extents().size(); ++i) {
                if (!sameExtentConstraint(lhs->extents()[i], rhs->extents()[i])) {
                    return false;
                }
            }
            pending.emplace_back(lhs->elemType(), rhs->elemType());
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
        case TypeCode::Stream: {
            // `T..` is one type per element type: the element decides, and nothing else is
            // part of it (`T..` and `T[..]` are different
            // types, and the suffix is read left to right).
            const auto *lhs = static_cast<const StreamType *>(left);
            const auto *rhs = static_cast<const StreamType *>(right);
            pending.emplace_back(lhs->elemType(), rhs->elemType());
            break;
        }
        case TypeCode::Struct: {
            const auto *lhs = static_cast<const StructType *>(left);
            const auto *rhs = static_cast<const StructType *>(right);
            // A named struct type is its declaration (and type arguments): two declarations
            // are different types even when their fields are alike, and a named type is never
            // an anonymous one (design principles 5.2).
            if (lhs->named() != rhs->named()) {
                return false;
            }
            if (lhs->named()) {
                if (lhs->decl() != rhs->decl() ||
                    lhs->typeArgCount() != rhs->typeArgCount()) {
                    return false;
                }
                for (size_t i = 0; i < lhs->typeArgCount(); ++i) {
                    pending.emplace_back(lhs->typeArgAt(i), rhs->typeArgAt(i));
                }
                break;
            }
            if (lhs->size() != rhs->size()) {
                return false;
            }
            // The order of the fields is part of an anonymous struct type: `{ x, y }` and
            // `{ y, x }` are different types (design principles 5.2).
            for (size_t i = 0; i < lhs->size(); ++i) {
                if (lhs->fieldName(i) != rhs->fieldName(i)) {
                    return false;
                }
                const Type *lhsMember = lhs->typeAt(i);
                const Type *rhsMember = rhs->typeAt(i);
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
                lhs->normTypesCount() != rhs->normTypesCount() ||
                lhs->hasRest() != rhs->hasRest() ||
                lhs->constasIndices() != rhs->constasIndices()) {
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
            // What a function promises about resources is part of its type (effects plan 2.2).
            if (!resources(lhs->resources(), rhs->resources())) {
                return false;
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

bool typeEquals(const Type *a, const Type *b) { return sameTypeWith(a, b, sameResources); }

bool sameTypeWithUnwrittenResources(const Type *target, const Type *source) {
    return sameTypeWith(target, source, takesUnwrittenResources);
}

bool sameCanonicalType(const Type *a, const Type *b) {
    if (a == b) {
        return true;
    }
    TypeTable &table  = TypeTable::instance();
    const TypeId left = table.canonicalId(a);
    // kNoTypeId is the id of every type the table does not hold, so it says nothing about a
    // structure; two such types are the same type only by being the same object, above.
    return left != kNoTypeId && left == table.canonicalId(b);
}

} // namespace camel::core::type