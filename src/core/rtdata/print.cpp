// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The text of one value (camel/core/rtdata/base.h, printSlot).
 *
 * A value is as deep as its data: a recursive enum a million nodes long, an array nested a
 * hundred thousand levels. The shape of the value and its text have the same depth, so a
 * recursive printer needs one native frame per level and runs out of stack. Here the parts to
 * print are a work list on the heap: a Slot item asks for a value, a Text item asks for a
 * literal between values. Output order is the stack's pop order, so a composite pushes its
 * closing text and its parts in reverse.
 *
 * Scalars and the object-backed types that print without looking at other values (string,
 * bytes, function, tensor, a foreign resource, a Json value) are printed by their own `print`.
 * The composites are walked here; `Object::print` remains their single-value entry point for
 * callers that ask an object directly, and it stays shallow because it goes back through
 * printSlot, which walks iteratively.
 */

#include "camel/core/rtdata/base.h"

#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"

#include "collection_tree.h"

#include <limits>
#include <string_view>
#include <vector>

using namespace camel::core::type;

namespace camel::core::rtdata {

namespace {

/// One item of the work list: print a value, or write a literal.
struct PrintItem {
    enum class Kind { Slot, Text };

    Kind kind;
    std::string_view text;
    slot_t slot;
    const Type *type;
};

void pushSlot(std::vector<PrintItem> &stack, slot_t slot, const Type *type) {
    stack.push_back(PrintItem{PrintItem::Kind::Slot, {}, slot, type});
}

void pushText(std::vector<PrintItem> &stack, std::string_view text) {
    stack.push_back(PrintItem{PrintItem::Kind::Text, text, NullSlot, nullptr});
}

/// Whether the text of a value of `type` is made of other values. These are the types whose
/// depth follows the data; everything else prints in one step.
bool isPrintedByParts(const Type *type) {
    switch (type->code()) {
    case TypeCode::Array:
    case TypeCode::Tuple:
    case TypeCode::Struct:
        return true;
    default:
        break;
    }
    return asCollectionType(type) != nullptr || asEnumType(type) != nullptr;
}

/// A value that prints in one step: a number, a bool, a byte, `null`, `ref`, or an object whose
/// own print does not descend into values (string, bytes, function, tensor, ...).
void printScalar(std::ostream &os, slot_t data, const Type *type) {
    if (type->isGCTraced()) {
        // Null was handled by the caller.
        reinterpret_cast<const Object *>(data)->print(os, type);
        return;
    }
    if (type->isOtherType()) {
        os << "<" << type->toString() << ">";
        return;
    }
    switch (type->code()) {
    case TypeCode::Int32:
        os << fromSlot<Int32>(data);
        break;
    case TypeCode::Int64:
        os << fromSlot<Int64>(data);
        break;
    case TypeCode::Float32:
        os << camel::utils::floatText(fromSlot<Float32>(data));
        break;
    case TypeCode::Float64:
        os << camel::utils::floatText(fromSlot<Float64>(data));
        break;
    // A bf16 or f16 prints by the float text rules of design principles 11.5, from the value
    // its bits name (camel/core/rtdata/half.h): `0.1` is `0.1`, not its bit pattern.
    case TypeCode::BFloat16:
        os << camel::utils::floatText(halfToFloat<BFloat16>(data));
        break;
    case TypeCode::Float16:
        os << camel::utils::floatText(halfToFloat<Float16>(data));
        break;
    case TypeCode::Bool:
        os << (fromSlot<Bool>(data) ? "true" : "false");
        break;
    case TypeCode::Void:
        os << "null";
        break;
    case TypeCode::Ref:
        os << "ref";
        break;
    default:
        os << std::format("<{}>", type->toString());
        break;
    }
}

/// The text of a failed `T!`: `error <kind>: <message>` (rtdata/tagged.cpp, printTaggedSlots).
void pushError(std::vector<PrintItem> &stack, slot_t error) {
    const auto *info = fromSlot<const Struct *>(error);
    const auto field = [&](std::string_view name) -> std::string_view {
        const auto *str = info->get<const String *>(errorFieldIndex(name));
        return str ? str->view() : std::string_view{};
    };
    // Reverse output order.
    pushText(stack, field("message"));
    pushText(stack, ": ");
    pushText(stack, field("kind"));
    pushText(stack, "error ");
}

} // namespace

namespace {

/// The shared body of printValue and printSlotCapped. `arrayLimit` applies to every array met
/// at any depth: a larger array prints as its type, address and length.
void printValueImpl(std::ostream &os, slot_t data, Type *type, size_t arrayLimit) {
    ASSERT(
        data != DeadSlot,
        std::format("Accessing uninitialized slot in printSlot: {}", type->toString()));
    if (!type->isGCTraced()) {
        printScalar(os, data, type);
        return;
    }
    if (data == NullSlot) {
        os << "null";
        return;
    }
    if (!isPrintedByParts(type)) {
        printScalar(os, data, type);
        return;
    }

    std::vector<PrintItem> stack;
    pushSlot(stack, data, type);
    while (!stack.empty()) {
        const PrintItem item = stack.back();
        stack.pop_back();
        if (item.kind == PrintItem::Kind::Text) {
            os << item.text;
            continue;
        }

        const Type *t  = item.type;
        const slot_t v = item.slot;
        if (v == DeadSlot) {
            // An element of a value that is still being built: a higher-order operator stores
            // its result array in the frame slot before the element calls fill it (so a
            // safepoint inside one keeps the array alive), and the frame's debug log or a
            // debugger may print it in between. The elements not written yet are all this
            // pattern; show them rather than assert, as the debugger's value reader does.
            os << "<uninitialized>";
            continue;
        }
        if (!t->isGCTraced()) {
            printScalar(os, v, t);
            continue;
        }
        if (v == NullSlot) {
            os << "null";
            continue;
        }
        if (!isPrintedByParts(t)) {
            printScalar(os, v, t);
            continue;
        }

        switch (t->code()) {
        case TypeCode::Array: {
            const auto *array    = fromSlot<const Array *>(v);
            const Type *elemType = static_cast<const ArrayType *>(t)->elemType();
            if (array->size() > arrayLimit) {
                // Type, address and length: enough to identify the collection without the
                // element-by-element text a log line must not pay for (H31).
                os << "<" << t->toString() << " at 0x" << std::hex << static_cast<uint64_t>(v)
                   << std::dec << ", " << array->size() << " elements>";
                continue;
            }
            pushText(stack, "]");
            for (size_t i = array->size(); i-- > 0;) {
                pushSlot(stack, array->getSlot(i), elemType);
                if (i > 0) {
                    pushText(stack, ", ");
                }
            }
            os << "[";
            continue;
        }
        case TypeCode::Tuple: {
            const auto *tuple     = fromSlot<const Tuple *>(v);
            const auto *tupleType = static_cast<const TupleType *>(t);
            if (tupleType->isNullable()) {
                if (isNullValue(v)) {
                    os << "null";
                } else {
                    pushSlot(stack, nullablePayload(v), tupleType->payloadType());
                }
                continue;
            }
            if (tupleType->isFallible()) {
                if (isErrorValue(v)) {
                    pushError(stack, fallibleError(v));
                } else {
                    pushSlot(stack, falliblePayload(v), tupleType->payloadType());
                }
                continue;
            }
            pushText(stack, ")");
            for (size_t i = tupleType->size(); i-- > 0;) {
                pushSlot(stack, tuple->slotAt(i), tupleType->typeAt(i));
                if (i > 0) {
                    pushText(stack, ", ");
                }
            }
            os << "(";
            continue;
        }
        case TypeCode::Struct: {
            const auto *record     = fromSlot<const Struct *>(v);
            const auto *structType = static_cast<const StructType *>(t);
            if (structType->size() == 0) {
                os << "{}";
                continue;
            }
            pushText(stack, " }");
            for (size_t i = structType->size(); i-- > 0;) {
                pushSlot(stack, record->slotAt(i), structType->typeAt(i));
                pushText(stack, ": ");
                pushText(stack, structType->fieldName(i));
                if (i > 0) {
                    pushText(stack, ", ");
                }
            }
            os << "{ ";
            continue;
        }
        default:
            break;
        }

        if (const CollectionType *collType = asCollectionType(t)) {
            const auto *table     = fromSlot<const Table *>(v);
            const Type *keyType   = collType->keyType();
            const Type *valueType = collType->valueType();
            const bool isMap      = collType->isMap();
            std::vector<std::pair<slot_t, slot_t>> entries;
            entries.reserve(table->size());
            table::detail::forEachLeaf(table, [&](const Table *leaf) {
                for (size_t i = 0; i < leaf->size(); ++i) {
                    entries.emplace_back(leaf->keys()[i], isMap ? leaf->values()[i] : NullSlot);
                }
            });
            pushText(stack, "}");
            for (size_t i = entries.size(); i-- > 0;) {
                if (isMap) {
                    pushSlot(stack, entries[i].second, valueType);
                    pushText(stack, ": ");
                }
                pushSlot(stack, entries[i].first, keyType);
                if (i > 0) {
                    pushText(stack, ", ");
                }
            }
            os << "{";
            continue;
        }

        if (const EnumType *enumType = asEnumType(t)) {
            const auto *variantValue   = fromSlot<const Variant *>(v);
            const EnumVariant &variant = enumType->variant(variantValue->tag());
            switch (variant.shape) {
            case VariantShape::Unit:
                break;
            case VariantShape::Tuple:
                pushText(stack, ")");
                for (size_t i = variantValue->fieldCount(); i-- > 0;) {
                    pushSlot(stack, variantValue->get(i), variant.fieldTypes[i]);
                    if (i > 0) {
                        pushText(stack, ", ");
                    }
                }
                pushText(stack, "(");
                break;
            case VariantShape::Struct:
                pushText(stack, " }");
                for (size_t i = variantValue->fieldCount(); i-- > 0;) {
                    pushSlot(stack, variantValue->get(i), variant.fieldTypes[i]);
                    pushText(stack, ": ");
                    pushText(stack, variant.fieldNames[i]);
                    if (i > 0) {
                        pushText(stack, ", ");
                    }
                }
                pushText(stack, " { ");
                break;
            }
            // Reverse output order: the name of the type, then the variant.
            pushText(stack, variant.name);
            pushText(stack, "::");
            pushText(stack, enumType->decl()->shortName());
            continue;
        }

        // Not a composite after all: an object type whose print does not descend into values.
        printScalar(os, v, t);
    }
}

} // namespace

void printValue(std::ostream &os, slot_t data, Type *type) {
    printValueImpl(os, data, type, std::numeric_limits<size_t>::max());
}

void printSlotCapped(std::ostream &os, slot_t data, Type *type, size_t arrayLimit) {
    printValueImpl(os, data, type, arrayLimit);
}

} // namespace camel::core::rtdata
