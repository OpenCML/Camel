// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Literals entering a type: a constant written in the source as a node of the type the position
 * asks for, and the composite a written value is converted into. Not control flow, which is what
 * the file this was taken out of is about.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/data/primary.h"
#include "camel/core/data/ref.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/type/composite/stream.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {
/// The exact value of a literal primitive datum: an integer or a real. A literal element
/// (field) of a composite is converted from it, so that an integer is one rounding from the
/// value written rather than the same double rounded again.
std::optional<literal::Value> exactValueOfData(const data_ptr_t &data) {
    if (const auto integer = std::dynamic_pointer_cast<LongData>(data); integer != nullptr) {
        return literal::Value::fromInt64(integer->data());
    }
    if (const auto integer = std::dynamic_pointer_cast<IntData>(data); integer != nullptr) {
        return literal::Value::fromInt64(integer->data());
    }
    if (const auto real = std::dynamic_pointer_cast<DoubleData>(data); real != nullptr) {
        return literal::Value::fromDouble(real->data());
    }
    if (const auto real = std::dynamic_pointer_cast<FloatData>(data); real != nullptr) {
        return literal::Value::fromDouble(static_cast<double>(real->data()));
    }
    if (const auto half = std::dynamic_pointer_cast<HalfData>(data); half != nullptr) {
        // A bf16 or f16 element: its value is the f32 it widens to, exactly, so an element of a
        // bf16 or f16 composite converts from the value it holds rather than from a text.
        return literal::Value::fromDouble(static_cast<double>(half->toFloat()));
    }
    return std::nullopt;
}

bool isNumericType(Type *type) {
    return type != nullptr && type->isPrimitive() &&
           camel::core::type::isNumericScalar(type->code());
}

/// Whether the element `data` of a composite literal can be given `type`: the question
/// compositeDataInType / literalElementInType answer while building the converted value, asked
/// here without building it and without reporting. A value of a kind that enters the type is a
/// fit even when the type cannot hold it (`3000000000` in an `i32`): that is a value the
/// conversion refuses, with the value as written, once the callee is known.
bool elementEntersIn(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin,
    const literal::Value *written);

/// The element `index` of the elements the GCT kept for a composite literal: its exact value
/// (when it was a numeric literal) and where it was written. Null when none were kept.
const GCT::Tree::LiteralElement *
literalElementAt(const std::vector<GCT::Tree::LiteralElement> *written, size_t index) {
    return written != nullptr && index < written->size() ? &(*written)[index] : nullptr;
}

/// The exact value an element was written as, or nullptr for a name, a computed element or a
/// nested composite (which holds its own elements).
const literal::Value *writtenValueOf(const GCT::Tree::LiteralElement *element) {
    return element != nullptr && element->value.has_value() ? &*element->value : nullptr;
}

/// The length an array position states as a constant, or nullopt when it leaves the length open
/// (`T[]`, `T[n]`, `T[..]`). The first extent is the one a literal's elements are the elements of.
std::optional<int64_t> statedLength(const ArrayType *type) {
    const std::span<const ExtentForm> extents = type->extents();
    if (extents.empty() || !extents.front().isConstant()) {
        return std::nullopt;
    }
    return extents.front().constant();
}

/// Whether the `data` array holds the number of elements a constant extent of the position
/// states. A literal's elements are known where it is written, so this is decided here and not
/// at runtime: `[1.0, 2.0, 3.0]` does not stand in an `f32[4]` position (design principles 5.4).
bool holdsStatedLength(const ArrayType *type, const ArrayData *array) {
    const std::optional<int64_t> length = statedLength(type);
    return !length.has_value() || static_cast<int64_t>(array->raw().size()) == *length;
}

/// One element of a nested literal, with what the GCT kept for it where it was written (its
/// exact value and its position; nullptr for an element it kept nothing for).
using LiteralLeaf = std::pair<data_ptr_t, const GCT::Tree::LiteralElement *>;

/// The shape a nested array literal writes, one length per level, and its elements in the order
/// the flattened value holds them (`[[a, b], [c, d]]` is [2, 2] over a b c d). False when a level
/// is not one shape — a row beside an element, or rows of different lengths — which is no rank-N
/// array (design principles 5.1, 5.4).
bool flattenLiteral(
    const GCT::Tree *tree, const data_ptr_t &data, std::vector<int64_t> &lengths,
    std::vector<LiteralLeaf> &leaves) {
    const auto array = std::dynamic_pointer_cast<ArrayData>(data);
    if (array == nullptr) {
        return false;
    }
    const auto &raw = array->raw();
    lengths.push_back(static_cast<int64_t>(raw.size()));
    if (raw.empty()) {
        return true;
    }
    const auto *written = tree != nullptr ? tree->literalElementsOf(data) : nullptr;
    const bool rows     = std::dynamic_pointer_cast<ArrayData>(raw.front()) != nullptr;
    std::vector<int64_t> rowShape;
    for (size_t index = 0; index < raw.size(); ++index) {
        const bool isRow = std::dynamic_pointer_cast<ArrayData>(raw[index]) != nullptr;
        if (isRow != rows) {
            return false;
        }
        if (!rows) {
            leaves.emplace_back(raw[index], literalElementAt(written, index));
            continue;
        }
        std::vector<int64_t> sub;
        std::vector<LiteralLeaf> subLeaves;
        if (!flattenLiteral(tree, raw[index], sub, subLeaves)) {
            return false;
        }
        if (index == 0) {
            rowShape = sub;
        } else if (sub != rowShape) {
            return false;
        }
        // Every row's elements are elements of the one array, in row-major order: the walk keeps
        // them all, not the ones of the first row (`[[a, b], [c, d]]` is a b c d).
        leaves.insert(leaves.end(), subLeaves.begin(), subLeaves.end());
    }
    lengths.insert(lengths.end(), rowShape.begin(), rowShape.end());
    return true;
}

/// Whether the array literal `data` writes the shape `type` states: a position that states a rank
/// of its own (`f32[2, 2]`) takes one array written over that many levels, each level as long as
/// the extent the position states (`?` leaves the length to the literal). A flat literal is not
/// an `f32[2, 2]` — it is an `f32[4]` — and nothing reshapes it to fit (design principles 5.1,
/// 5.4). A rank-1 position keeps the rule a literal's elements decide. `lengths` and `leaves` are
/// the walk compositeDataInType builds the value from.
bool arrayLiteralFitsShape(
    const GCT::Tree *tree, const ArrayType *type, const data_ptr_t &data,
    std::vector<int64_t> *lengths = nullptr, std::vector<LiteralLeaf> *leaves = nullptr) {
    const auto array = std::dynamic_pointer_cast<ArrayData>(data);
    if (array == nullptr) {
        return false;
    }
    const std::span<const ExtentForm> extents = type->extents();
    if (extents.size() < 2) {
        return holdsStatedLength(type, array.get());
    }
    std::vector<int64_t> written;
    std::vector<LiteralLeaf> flattened;
    if (!flattenLiteral(tree, data, written, flattened)) {
        return false;
    }
    if (written.size() != extents.size()) {
        return false;
    }
    for (size_t level = 0; level < written.size(); ++level) {
        if (extents[level].isConstant() && extents[level].constant() != written[level]) {
            return false;
        }
    }
    if (lengths != nullptr) {
        *lengths = std::move(written);
    }
    if (leaves != nullptr) {
        *leaves = std::move(flattened);
    }
    return true;
}

/// Whether the composite literal `data` can be given `type`: the walk of compositeDataInType,
/// unchanged, for the positions a literal may enter (design principles 5.1). The two must agree;
/// a literal this accepts is converted by compositeDataInType, which reports what does not fit.
bool compositeEntersIn(const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return false;
    }
    if (type->assignableFrom(data->type())) {
        return true;
    }
    if (type->code() == TypeCode::Array && data->type()->code() == TypeCode::Array) {
        const auto *arrayType = tt::as_ptr<ArrayType>(type);
        Type *elemType        = arrayType->elemType();
        const auto array      = std::dynamic_pointer_cast<ArrayData>(data);
        if (elemType == nullptr || array == nullptr) {
            return false;
        }
        const GCT::Tree *tree = origin != nullptr ? GCT::Tree::of(origin) : nullptr;
        std::vector<int64_t> lengths;
        std::vector<LiteralLeaf> leaves;
        if (!arrayLiteralFitsShape(tree, arrayType, data, &lengths, &leaves)) {
            return false;
        }
        if (array->raw().empty()) {
            // `[]`: it has no elements, so only a position that states no length, or the length
            // zero, took it — which the check above decided.
            return true;
        }
        if (arrayType->extents().size() >= 2) {
            // The elements of the shape the literal writes, in the order the flattened value
            // holds them (design principles 5.1, 5.4).
            for (const LiteralLeaf &leaf : leaves) {
                if (!elementEntersIn(leaf.first, elemType, origin, writtenValueOf(leaf.second))) {
                    return false;
                }
            }
            return true;
        }
        const auto *written = tree != nullptr ? tree->literalElementsOf(data) : nullptr;
        for (size_t index = 0; index < array->raw().size(); ++index) {
            if (!elementEntersIn(
                    array->raw()[index],
                    elemType,
                    origin,
                    writtenValueOf(literalElementAt(written, index)))) {
                return false;
            }
        }
        return true;
    }
    if (type->code() == TypeCode::Struct && data->type()->code() == TypeCode::Struct) {
        auto *structType  = tt::as_ptr<StructType>(type);
        const auto object = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || object == nullptr ||
            object->raw().size() != structType->size()) {
            return false;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        size_t index = 0;
        for (const auto &[name, value] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return false;
            }
            const GCT::Tree::LiteralElement *element = literalElementAt(written, index);
            ++index;
            if (!elementEntersIn(
                    value,
                    structType->typeAt(*fieldIndex),
                    origin,
                    writtenValueOf(element))) {
                return false;
            }
        }
        return true;
    }
    if (type->code() == TypeCode::Tuple && data->type()->code() == TypeCode::Tuple) {
        auto *tupleType   = tt::as_ptr<TupleType>(type);
        const auto object = std::dynamic_pointer_cast<TupleData>(data);
        if (tupleType == nullptr || object == nullptr ||
            object->raw().size() != tupleType->size()) {
            return false;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        for (size_t index = 0; index < object->raw().size(); ++index) {
            if (!elementEntersIn(
                    object->raw()[index],
                    tupleType->typeAt(index),
                    origin,
                    writtenValueOf(literalElementAt(written, index)))) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool elementEntersIn(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin,
    const literal::Value *written) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return false;
    }
    if (type->assignableFrom(data->type())) {
        return true;
    }
    if (isComposite(data->type()->code())) {
        return compositeEntersIn(data, type, origin);
    }
    if (data->type()->code() == TypeCode::String) {
        // A text never becomes the type of its position (design principles 5.1), but it is the
        // element's own type mismatch: the conversion reports it at the element
        // (literalElementInType), instead of the whole literal being a value of another type.
        return true;
    }
    const std::optional<literal::Value> fallback =
        written == nullptr ? exactValueOfData(data) : std::nullopt;
    const literal::Value *value = written != nullptr ? written : fallback ? &*fallback : nullptr;
    return value != nullptr && isNumericType(type) && isNumericType(data->type()) &&
           literal::kindEnters(*value, type);
}

} // namespace

node_handle_t Builder::convertStructValue(
    node_handle_t value, StructType *target, const GCT::node_ptr_t &origin) {
    Type *sourceType = valueTypeOf(nodeTypeOf(value));
    if (target == nullptr || sourceType == nullptr || sourceType->code() != TypeCode::Struct ||
        target->size() == 0) {
        return nullptr;
    }
    auto *source = tt::as_ptr<StructType>(sourceType);
    // The fields must be the same ones, by name, before anything is copied: the relation of the
    // type layer (relateStructs) says so, as it does for every other conversion of a struct.
    if (source == nullptr || relateStructs(target, source) == StructRelation::None) {
        return nullptr;
    }
    // A struct of `target`'s shape, its slots filled from the source's fields in the target's
    // order: the copy is what makes `{ y: 1, x: 2 }` a `Point` (fields by name, declaration
    // order) and what converts between two named types that write their fields in another
    // order (design principles 5.2).
    std::vector<size_t> slots(target->size());
    std::iota(slots.begin(), slots.end(), 0);
    const slot_t empty =
        camel::core::rtdata::toSlot(::Struct::create(target->size(), camel::core::mm::autoSpace()));
    auto &draft        = currGraph_->transaction().edit();
    node_handle_t base = draft.ref(draft.materializeStaticValue(empty, target));
    node_handle_t out  = currGraph_->addFillNode(
        target,
        runtime::makeFillPayload(runtime::GCFillKind::Struct, slots));
    linkNodes(LinkType::Norm, base, out);
    for (size_t i = 0; i < target->size(); ++i) {
        const std::string name(target->fieldName(i));
        const size_t from     = *source->findField(name);
        Type *sourceFieldType = source->typeAt(from);
        Type *targetFieldType = target->typeAt(i);
        node_handle_t access = memberReadNodeOf(
            valueOf(value), sourceFieldType, std::string(source->fieldName(from)));
        node_handle_t field = access;
        if (targetFieldType != nullptr && sourceFieldType != nullptr &&
            !targetFieldType->equals(sourceFieldType)) {
            if (targetFieldType->code() == TypeCode::Struct &&
                sourceFieldType->code() == TypeCode::Struct) {
                if (node_handle_t inner = convertStructValue(
                        access,
                        tt::as_ptr<StructType>(targetFieldType),
                        origin)) {
                    field = inner;
                } else if (!targetFieldType->assignableFrom(sourceFieldType)) {
                    return nullptr;
                }
            } else if (
                isUntypedArrayType(sourceFieldType) &&
                targetFieldType->code() == TypeCode::Array) {
                // The field is a position with an expected type like any other, and the empty
                // array literal takes the element type of the type it stands in (design
                // principles 5.1, 5.2) — the same rule, and the same call, that types `[]` when
                // the whole literal is a constant (compositeDataInType). A field beside it whose
                // value the program computes is what kept this path from being reached before.
                node_handle_t typed = coerceTo(access, targetFieldType, origin);
                if (typed == nullptr) {
                    return nullptr;
                }
                field = typed;
            } else if (isRankZeroEdge(targetFieldType, sourceFieldType)) {
                // The field's type goes from `T` to `T[..]` here (design principles 5.4): the value
                // is the element, and the slot holds the rank-zero array that *is* the element.
                // Same question and same call as every other position.
                node_handle_t wrapped = coerceTo(access, targetFieldType, origin);
                if (wrapped == nullptr) {
                    return nullptr;
                }
                field = wrapped;
            } else if (!targetFieldType->assignableFrom(sourceFieldType)) {
                // A field of another type (a refined module type is assignable; a number of
                // another type is not).
                return nullptr;
            }
        }
        linkNodes(LinkType::With, valueOf(field), out);
    }
    registerNodeOrigin(context_, out, origin, "gir.cast.struct");
    return out;
}

node_handle_t Builder::coerceTo(node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    Type *source = valueTypeOf(nodeTypeOf(node));
    if (target->assignableFrom(source)) {
        // An unnamed struct value whose fields are those of a named type, in the declaration's
        // order, is a value of that type from here on: the conversion moves nothing (the
        // fields keep their slots), and the value is used as the type it enters (design
        // principles 5.2).
        if (target->code() == TypeCode::Struct && !target->equals(source) &&
            target->castSafetyFrom(source) == CastSafety::Safe) {
            return castNodeTo(valueOf(node), target, origin);
        }
        // The value's type goes from `T` to `T[..]` here, and that is where the run's
        // representation is made: the slot holds the rank-zero array that *is* the element (design
        // principles 5.4), so the program's value, its printing and its comparison do not change,
        // while the slot has a rank to read. This is the one place the conversion is inserted --
        // every position that gives a value its type comes through here, and a call argument, which
        // does not, asks the same question before it enters (builder_operators.cpp).
        if (isRankZeroEdge(target, source)) {
            return castNodeTo(valueOf(node), target, origin);
        }
        return node;
    }
    // A literal takes the type of its context (design principles 5.1): `let x: float = 1`,
    // `f(2)` with `f(x: f32)`, `return 2` under `f32`. A value that does not fit the target
    // type is reported here — never cut to size. What already has a type is not converted:
    // `let x: float = n` with `n: int` is still an error.
    if (const std::optional<literal::Value> value = literalValueOfExpr(origin);
        value.has_value() && target->isPrimitive() && literal::kindEnters(*value, target)) {
        return literalInType(*value, target, origin, true);
    }
    // A literal in a position of `T[..]` is the element (design principles 5.4): the value takes
    // the element's type first, and the wrap above makes the rank-zero value the slot holds. The
    // position states the element type, not a dimension, so nothing about the literal loses.
    if (const std::optional<literal::Value> value = literalValueOfExpr(origin); value.has_value()) {
        if (Type *element = rankZeroElementOf(target);
            element != nullptr && literal::kindEnters(*value, element)) {
            node_handle_t elementNode = literalInType(*value, element, origin, true);
            return elementNode != nullptr ? castNodeTo(elementNode, target, origin) : nullptr;
        }
    }
    // An array or struct literal takes the element (field) type its position gives it (design
    // principles 5.1): `f([1, 2])` where `f` takes a `float[]`, `return { a: 1 }` under
    // `{ a: float }`. Its elements are positions with an expected type as well, and a literal
    // element that the type cannot hold is reported here.
    if (node_handle_t typed = compositeLiteralInType(node, target, origin); typed != nullptr) {
        return typed;
    }
    // A value that already has an unnamed struct type enters a named type whose fields are
    // its fields, by name, even when they are written in another order: the value is copied
    // field by field (design principles 5.2). Between two named types nothing enters by
    // itself: an `as` is required, and that is the explicit path (visitCastNode).
    if (target->code() == TypeCode::Struct && source->code() == TypeCode::Struct) {
        auto *targetStruct = tt::as_ptr<StructType>(target);
        auto *sourceStruct = tt::as_ptr<StructType>(source);
        if (targetStruct != nullptr && sourceStruct != nullptr && targetStruct->named() &&
            !sourceStruct->named()) {
            if (node_handle_t copied = convertStructValue(node, targetStruct, origin);
                copied != nullptr) {
                return copied;
            }
        }
    }
    // An error value in a position of a `T!` is that type's failed form: the written failed arm
    // (design principles 3.9). It is how `return if c then error(...) else v` is accepted
    // under a declared `T!` -- the position's type is what each arm takes, and the error arm is
    // the failing one. Without an expected type nothing joins the error value with the other
    // arm, and the branch is still refused. This is a rule of the position, like the literal
    // rules above, not a general entrance: `entersImplicitly` is unchanged.
    if (TupleType *fallible = asFallible(target);
        fallible != nullptr && source->equals(errorType())) {
        const std::string name = bindTemporary(valueOf(node));
        node_handle_t failed   = applyTagOperator(camel::core::oper::TagThrow, name, origin);
        return nodeTypeOf(failed)->equals(fallible) ? failed : castNodeTo(failed, fallible, origin);
    }
    if (!entersImplicitly(target, source)) {
        // The other direction of the same edge: a value of `T[..]` read as `T` is the element it
        // holds (design principles 5.4), so the conversion hands the element on. `T[]` and
        // `T[3, ..]` are not this edge -- they state dimensions the element has not -- and they
        // stay refused, as the table refuses them.
        if (isRankZeroEdge(target, source)) {
            return castNodeTo(valueOf(node), target, origin);
        }
        return nullptr;
    }
    node = valueOf(node); // what a variable holds, not the variable
    if (TupleType *tagged = asTagged(target)) {
        // `null` is the null slot of every `T?`; any other value enters the payload first.
        if (tagged->isNullable() && source->code() == TypeCode::Void) {
            node_handle_t cast = castNodeTo(node, tagged, origin);
            // The cast of `null` into a `T?` moves no bits and computes nothing: what the node
            // stands for is the null constant of the type it is read as. A position that requires
            // a compile-time constant -- a parameter default is one -- reads the value here
            // (design principles 4.4), and a `null` that is not a constant (a value the program
            // computed as `void`) stays a value with no constant.
            if (const data_ptr_t written = constantOfNode(node);
                written != nullptr && written->isNull()) {
                rememberConstant(cast, Data::null(tagged));
            }
            return cast;
        }
        node_handle_t inner = coerceTo(node, tagged->payloadType(), origin);
        return inner != nullptr ? castNodeTo(inner, tagged, origin) : nullptr;
    }
    // The empty literal `[]`: the same literal with the element type its position gives it.
    GUARD(target->code() == TypeCode::Array, "Only an empty array literal is left here.");
    node_handle_t typed = currGraph_->addStaticDataNode(
        tt::as_shared<Data>(ArrayData::create(tt::as_ptr<ArrayType>(target)->elemType())));
    registerNodeOrigin(context_, typed, origin, "gir.data");
    return typed;
}

node_handle_t
Builder::checkValue(node_handle_t node, Type *expected, const GCT::node_ptr_t &origin) {
    if (node == nullptr || expected == nullptr) {
        return node;
    }
    // A name of a generic function has no type of its own until the position says which
    // instance is wanted (design principles 5.5); genericValueFor reports when it cannot.
    if (genericDrefs_.contains(node)) {
        node = genericValueFor(node, expected, origin);
    }
    return coerceTo(node, expected, origin);
}

node_handle_t Builder::visitInPosition(const GCT::node_ptr_t &gct, Type *expected) {
    ExpectedValueScope scope(expectedValueType_, expected);
    if (node_handle_t literal = literalDataInType(gct, expected); literal != nullptr) {
        return literal;
    }
    node_handle_t node = valueNodeOf(visit(gct));
    if (node_handle_t checked = checkValue(node, expected, gct); checked != nullptr) {
        return checked;
    }
    // The value does not take the position's type. The position's own check reports it with
    // the diagnostic that names the place (a declared return type, a branch's arms); leaving
    // the value as it is keeps that one report.
    return node;
}

node_handle_t
Builder::compositeLiteralInType(node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    if (origin == nullptr || target == nullptr || asTagged(target) != nullptr ||
        origin->type() != GCT::NodeKind::DATA) {
        return nullptr;
    }
    // Only the constant the literal was lowered to: a value computed by the program is not a
    // literal, and takes no type from its position. A constant lives in a static slot, whose
    // index is negative (a runtime slot index is not).
    if (!nodeIsKind(node, runtime::GCNodeKind::Data) || nodeHeaderOf(node)->dataIndex >= 0) {
        return nullptr;
    }
    const data_ptr_t &data = origin->data();
    if (data == nullptr || !isComposite(data->type()->code())) {
        return nullptr;
    }
    if (!tt::as_shared<CompositeData>(data)->resolved()) {
        // An element is computed (`{ a: x, b: 2.0 }`): the literal is not one value, and the
        // path of the computed elements (a FILL node) decides.
        return nullptr;
    }
    const data_ptr_t typed = compositeDataInType(data, target, origin);
    if (typed == nullptr) {
        return nullptr;
    }
    node_handle_t result = currGraph_->addStaticDataNode(typed);
    if (varied_ && currGraph_->outerGraph().get() != nullptr) {
        node_handle_t copyNode = currGraph_->addCopyNode(typed->type());
        linkNodes(LinkType::Norm, result, copyNode);
        result = copyNode;
    }
    registerNodeOrigin(context_, result, origin, "gir.data.literal");
    return result;
}

bool Builder::compositeLiteralEntersIn(
    node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    if (origin == nullptr || target == nullptr || asTagged(target) != nullptr ||
        origin->type() != GCT::NodeKind::DATA) {
        return false;
    }
    // Only the constant the literal was lowered to, as in compositeLiteralInType: a value the
    // program computes is not a literal, and a runtime slot's value is not one either.
    if (!nodeIsKind(node, runtime::GCNodeKind::Data) || nodeHeaderOf(node)->dataIndex >= 0) {
        return false;
    }
    const data_ptr_t &data = origin->data();
    if (data == nullptr || !isComposite(data->type()->code()) ||
        !tt::as_shared<CompositeData>(data)->resolved()) {
        return false;
    }
    return compositeEntersIn(data, target, origin);
}

data_ptr_t
Builder::compositeDataInType(const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return nullptr;
    }
    // A position that states an extent takes the value it states, and a source that leaves the
    // same dimension open does not settle it: `f32[3]` from a `f32[]` is assignable (an extent a
    // source does not state is accepted as any length), and returning the value unchanged left it
    // the `f32[]` it was written as -- which is why a rank-one `return` refused a literal that
    // fits while the rank-two spelling, whose nested literal states the shape itself, passed. The
    // walk below is the one that builds the value element by element from the values that were
    // written (design principles 5.1), and this is the one combination it is asked for here: a
    // stated extent against a dimension the source leaves open. Every other pair keeps the answer
    // it had, which is why `assignableFrom` itself is not touched (a source that states its
    // extent, or a target that states none, is still taken as it was).
    const bool positionStatesWhatTheValueLeavesOpen = [&] {
        const auto *target = tt::as_ptr<ArrayType>(type);
        const auto *source =
            data->type()->code() == TypeCode::Array ? tt::as_ptr<ArrayType>(data->type()) : nullptr;
        if (target == nullptr || source == nullptr ||
            target->extents().size() != source->extents().size()) {
            return false;
        }
        for (size_t i = 0; i < target->extents().size(); ++i) {
            if (target->extents()[i].isConstant() && !source->extents()[i].isConstant()) {
                return true;
            }
        }
        return false;
    }();
    if (type->assignableFrom(data->type()) && !positionStatesWhatTheValueLeavesOpen) {
        return data;
    }
    if (type->code() == TypeCode::Array && data->type()->code() == TypeCode::Array) {
        const auto *arrayType = tt::as_ptr<ArrayType>(type);
        Type *elemType        = arrayType->elemType();
        const auto array      = std::dynamic_pointer_cast<ArrayData>(data);
        if (elemType == nullptr || array == nullptr) {
            return nullptr;
        }
        const GCT::Tree *tree = origin != nullptr ? GCT::Tree::of(origin) : nullptr;
        std::vector<int64_t> lengths;
        std::vector<LiteralLeaf> leaves;
        if (!arrayLiteralFitsShape(tree, arrayType, data, &lengths, &leaves)) {
            return nullptr;
        }
        // The exact value each element was written as, when the GCT kept them (a composite
        // literal of literals): an element is then converted from the value written, not from
        // the double it was lowered to (design principles 5.1).
        if (array->raw().empty()) {
            // `[]`: the elements it does not have are the only thing its type leaves open, so
            // the empty array takes the element type of the position it stands in (design
            // principles 5.1, 5.2) — `[]` where an `int[]` is expected is the empty `int[]`,
            // whether the position is a declaration, a body's value or a loop state. A literal
            // that has elements keeps their join, so this branch only covers `[]`, and a
            // position that states a length other than zero did not take it (the check above).
            return ArrayData::create(elemType);
        }
        if (arrayType->extents().size() >= 2) {
            // A position that states a rank of its own takes the nested literal as one array of
            // the shape the literal writes, whose elements are the elements of that shape in
            // row-major order (design principles 5.1, 5.4) — the array is one value, not an
            // array of its rows.
            ArrayDataFactory factory;
            factory.setElemType(elemType);
            factory.setExtents(lengths);
            for (const LiteralLeaf &leaf : leaves) {
                data_ptr_t converted =
                    literalElementInType(leaf.first, elemType, origin, leaf.second);
                if (converted == nullptr) {
                    return nullptr;
                }
                factory.add(converted);
            }
            return factory.build();
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        ArrayDataFactory factory;
        // A rank-one position that states its extent keeps it in the value it builds. `lengths`
        // cannot say so here: the fit check above settles a rank-one position with
        // `holdsStatedLength` and never fills it, which is why the value built from its elements
        // alone came out the open `f32[]` it was written as. A position that states no extent
        // still lets the elements decide, so only a constant extent is asked for.
        if (arrayType->extents().size() == 1 && arrayType->extents().front().isConstant()) {
            const int64_t statedLength[1] = {arrayType->extents().front().constant()};
            factory.setExtents(statedLength);
        }
        size_t index = 0;
        for (const auto &element : array->raw()) {
            data_ptr_t converted =
                literalElementInType(element, elemType, origin, literalElementAt(written, index++));
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(converted);
        }
        return factory.build();
    }
    if (type->code() == TypeCode::Struct && data->type()->code() == TypeCode::Struct) {
        auto *structType  = tt::as_ptr<StructType>(type);
        const auto object = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || object == nullptr ||
            object->raw().size() != structType->size()) {
            return nullptr;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        StructDataFactory factory;
        size_t index = 0;
        for (const auto &[name, value] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return nullptr;
            }
            data_ptr_t converted = literalElementInType(
                value,
                structType->typeAt(*fieldIndex),
                origin,
                literalElementAt(written, index++));
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(name, converted);
        }
        // The literal is a value of the type the position gives it: a named struct type stays
        // named, and its fields are in the declaration's order however they were written
        // (design principles 5.2).
        return factory.buildAs(structType);
    }
    if (type->code() == TypeCode::Tuple && data->type()->code() == TypeCode::Tuple) {
        // The missing branch of the element walk: a tuple literal used as an argument had no
        // way into a tuple parameter, so a text element made the whole call a NoMatchingFunction
        // instead of naming the element. Elements are converted in written order, which
        // is a tuple's order.
        auto *tupleType   = tt::as_ptr<TupleType>(type);
        const auto object = std::dynamic_pointer_cast<TupleData>(data);
        if (tupleType == nullptr || object == nullptr ||
            object->raw().size() != tupleType->size()) {
            return nullptr;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        TupleDataFactory factory;
        size_t index = 0;
        for (const auto &element : object->raw()) {
            data_ptr_t converted = literalElementInType(
                element,
                tupleType->typeAt(index),
                origin,
                literalElementAt(written, index));
            ++index;
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(converted);
        }
        return factory.build();
    }
    return nullptr;
}

data_ptr_t Builder::compositeWithHolesInType(
    const data_ptr_t &data, Type *position, const GCT::node_ptr_t &origin,
    const std::vector<std::string> &refNames, const type_vec_t &refTypes,
    const std::vector<size_t> &refHoles, std::vector<size_t> &holeSlots) {
    // Design principles 5.1: a literal in a position takes the position's type element by
    // element. A literal with a computed element is a FILL, not one constant, so
    // compositeDataInType cannot walk it — and its written elements used to keep their default
    // types (`{ n: x, t: (1.0, 2.0), v: [1.0] }` was `(float, float)` and `float[]` where the
    // declared fields are `(f32, f32)` and `f32[]`). The computed element is a value of its own
    // type and enters only when the type it has is one the position takes there; when it is
    // not, the position does not fit and the literal keeps the type its own elements give it.
    if (data == nullptr || position == nullptr || asTagged(position) != nullptr ||
        origin == nullptr) {
        return nullptr;
    }
    auto composite = std::dynamic_pointer_cast<CompositeData>(data);
    if (composite == nullptr || position->code() != data->type()->code()) {
        return nullptr;
    }
    const auto *written = GCT::Tree::of(origin)->literalElementsOf(data);
    // The type the position gives each of the literal's computed elements, per kind: an array or
    // a tuple is positional (its holes are element indexes), a struct is by the placeholder the
    // computed field left behind.
    const auto holeTypesByIndex = [&]() {
        std::vector<Type *> types(
            data->type()->code() == TypeCode::Tuple
                ? std::dynamic_pointer_cast<TupleData>(data)->raw().size()
                : std::dynamic_pointer_cast<ArrayData>(data)->raw().size(),
            nullptr);
        const std::vector<size_t> holes = composite->holes();
        for (size_t i = 0; i < holes.size() && i < refTypes.size(); ++i) {
            if (holes[i] < types.size()) {
                types[holes[i]] = valueTypeOf(refTypes[i]);
            }
        }
        return types;
    };
    // A computed element is a value of its own type: it takes no type from the position, it is
    // checked against the type the position gives that element (design principles 5.1). Failing
    // that check is reported here, at the element, and not left to the place the literal stands
    // in: the argument position, the declaration position and the receiver of `->` then report
    // the same diagnostic, pointing at the computed element, instead of the call ending in "no
    // matching function" because the literal keeps its own type. An array element, a tuple
    // element and a struct field are the same position here: a composite literal enters a
    // position element by element whatever it is made of.
    const auto rejectHole = [&](const std::string &kind, size_t index, Type *have, Type *wanted) {
        const GCT::Tree::LiteralElement *element = literalElementAt(written, index);
        const bool writtenHere =
            element != nullptr && element->origin != camel::source::kInvalidOriginId;
        diags_->of(SemanticDiag::ElementTypeMismatch)
            .atOrigin(writtenHere ? element->origin : origin->origin())
            .commit(kind, have->toString(), wanted->toString());
        throw BuildAbortException();
    };
    // Whether the literal wrote an element of its own: a literal whose elements are all computed
    // has no element type either — nothing in it took the element type this position gives the
    // elements it wrote — so it does not fit the position at all and stays a name without a
    // matching function, as `pick([n])` does. Once it wrote one, the elements it wrote take this
    // element type, and a computed element of another type is the mismatch reported at it. A
    // nested literal is such an element (its own computed elements are placeholders of the
    // enclosing literal's type, not values of a type of their own), so it is built from what its
    // names stand for and keeps the reading it had.
    const auto wroteAnElement = [](const std::vector<Type *> &holeTypes) {
        for (Type *holeType : holeTypes) {
            if (holeType == nullptr) {
                return true;
            }
        }
        return false;
    };
    const auto holeTypeNamed = [&](const data_ptr_t &value) -> Type * {
        // The placeholder a computed element left in the literal: its name is the reference the
        // GCT gave it, and the node it stands for is the one holding that reference's value.
        if (value == nullptr || value->type()->code() != TypeCode::Ref) {
            return nullptr;
        }
        const std::string &ref = tt::as_shared<RefData>(value)->ref();
        for (size_t i = 0; i < refNames.size() && i < refTypes.size(); ++i) {
            if (refNames[i] == ref) {
                return valueTypeOf(refTypes[i]);
            }
        }
        return nullptr;
    };
    if (position->code() == TypeCode::Array) {
        const auto *arrayType = tt::as_ptr<ArrayType>(position);
        const auto array      = std::dynamic_pointer_cast<ArrayData>(data);
        Type *elemType        = arrayType != nullptr ? arrayType->elemType() : nullptr;
        if (array == nullptr || elemType == nullptr || elemType->code() == TypeCode::Void ||
            !arrayLiteralFitsShape(
                origin != nullptr ? GCT::Tree::of(origin) : nullptr,
                arrayType,
                data)) {
            return nullptr;
        }
        const std::vector<Type *> holeTypes = holeTypesByIndex();
        if (holeTypes.size() != array->raw().size()) {
            return nullptr;
        }
        ArrayDataFactory factory;
        factory.setElemType(elemType);
        const bool wrote = wroteAnElement(holeTypes);
        for (size_t i = 0; i < array->raw().size(); ++i) {
            if (holeTypes[i] != nullptr) {
                if (!elemType->assignableFrom(holeTypes[i])) {
                    if (!wrote) {
                        return nullptr;
                    }
                    // The element the literal computed is not of the type this position gives
                    // that element: the element is checked, not converted, and the mismatch is
                    // reported at it (design principles 5.1).
                    rejectHole("array", i, holeTypes[i], elemType);
                }
                factory.add(array->raw()[i]);
                continue;
            }
            data_ptr_t converted = literalElementInType(
                array->raw()[i],
                elemType,
                origin,
                literalElementAt(written, i));
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(converted);
        }
        // Where the computed elements sit in the value the fill reads. An array is positional, so
        // the slot of a computed element is the index it was written at -- the same answer the
        // struct branch works out by name below. Said here rather than left to the caller's
        // pre-seeded copy: a function that has worked out where the values go hands that out, so a
        // caller that does not pre-seed cannot end up reading the literal's own indexes instead.
        holeSlots = refHoles;
        return factory.build();
    }
    if (position->code() == TypeCode::Struct) {
        auto *structType    = tt::as_ptr<StructType>(position);
        const auto *ownType = tt::as_ptr<StructType>(data->type());
        const auto object   = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || ownType == nullptr || object == nullptr ||
            object->raw().size() != structType->size() || refHoles.size() != refNames.size()) {
            return nullptr;
        }
        // Where the computed fields sit in the position: the literal's own type and the position
        // state the same fields, each in its own order (design principles 5.2).
        std::vector<size_t> slots;
        slots.reserve(refHoles.size());
        for (size_t index : refHoles) {
            if (index >= ownType->size()) {
                return nullptr;
            }
            const std::optional<size_t> at = structType->findField(ownType->fieldName(index));
            if (!at.has_value()) {
                return nullptr;
            }
            slots.push_back(*at);
        }
        StructDataFactory factory;
        size_t index = 0;
        for (const auto &[name, value] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return nullptr;
            }
            Type *fieldType = structType->typeAt(*fieldIndex);
            if (Type *hole = holeTypeNamed(value); hole != nullptr) {
                if (fieldType == nullptr) {
                    return nullptr;
                }
                if (!fieldType->assignableFrom(hole)) {
                    // A struct field is a position with an expected type like an array element or
                    // a tuple element (design principles 5.1): the field the literal computed is
                    // checked against the type the position gives that field, not converted, and
                    // the mismatch is reported at the field with the same diagnostic -- the field
                    // it is about named in the message. A literal whose fields are all computed
                    // is judged the same way.
                    rejectHole(
                        "field '" + name + "' of '" + structType->toString() + "'",
                        index,
                        hole,
                        fieldType);
                }
                factory.add(name, value);
            } else {
                data_ptr_t converted = literalElementInType(
                    value,
                    fieldType,
                    origin,
                    literalElementAt(written, index));
                if (converted == nullptr) {
                    return nullptr;
                }
                factory.add(name, converted);
            }
            ++index;
        }
        // A value of the position itself: the literal is the named type the position names, and
        // its fields are in that type's order (design principles 5.2).
        std::shared_ptr<StructData> built = factory.buildAs(structType);
        if (built == nullptr) {
            return nullptr;
        }
        holeSlots = std::move(slots);
        return built;
    }
    if (position->code() == TypeCode::Tuple) {
        const auto *tupleType = tt::as_ptr<TupleType>(position);
        const auto object     = std::dynamic_pointer_cast<TupleData>(data);
        if (tupleType == nullptr || object == nullptr ||
            object->raw().size() != tupleType->size()) {
            return nullptr;
        }
        const std::vector<Type *> holeTypes = holeTypesByIndex();
        if (holeTypes.size() != object->raw().size()) {
            return nullptr;
        }
        TupleDataFactory factory;
        const bool wrote = wroteAnElement(holeTypes);
        for (size_t i = 0; i < object->raw().size(); ++i) {
            Type *elemType = tupleType->typeAt(i);
            if (holeTypes[i] != nullptr) {
                if (elemType == nullptr) {
                    return nullptr;
                }
                if (!elemType->assignableFrom(holeTypes[i])) {
                    if (!wrote) {
                        return nullptr;
                    }
                    // As at an array element: the computed element is checked against the type
                    // this position gives it, and the mismatch is reported at it.
                    rejectHole("tuple", i, holeTypes[i], elemType);
                }
                factory.add(object->raw()[i]);
                continue;
            }
            data_ptr_t converted = literalElementInType(
                object->raw()[i],
                elemType,
                origin,
                literalElementAt(written, i));
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(converted);
        }
        return factory.build();
    }
    return nullptr;
}

data_ptr_t Builder::literalElementInType(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin,
    const GCT::Tree::LiteralElement *written) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return nullptr;
    }
    // The element in a position of `T[..]` is the rank-zero value that position promises (design
    // principles 5.4): the same value, kept as the value with no dimension it is, so the run has an
    // array to read and the program still sees the element. This is the constant side of the edge
    // the compiler converts at a computed value (`Builder::coerceTo`): one rule, two entrances -- a
    // value the program computes keeps its computation and is converted as it enters, a literal
    // becomes the value at this level, because there is nothing to compute.
    if (Type *rankZeroElement = rankZeroElementOf(type); rankZeroElement != nullptr) {
        auto *rankZeroValueType = tt::as_ptr<ArrayType>(type);
        if (rankZeroElement->assignableFrom(data->type())) {
            return ArrayData::rankZero(rankZeroValueType, data);
        }
        // A number written where the position states the element type: it takes that type as it
        // was written (design principles 5.1), exactly as a field of a named type does, and the
        // element is then the rank-zero value. Without this a literal `{ v: 1.5 }` could not be a
        // value of `{ v: f32[..] }` while `{ v: one }` with `one: f32` could.
        const literal::Value *exact = writtenValueOf(written);
        const std::optional<literal::Value> fallback =
            exact == nullptr ? exactValueOfData(data) : std::nullopt;
        const literal::Value *value = exact != nullptr ? exact : fallback ? &*fallback : nullptr;
        if (value != nullptr && isNumericType(rankZeroElement) && isNumericType(data->type()) &&
            literal::kindEnters(*value, rankZeroElement)) {
            if (const auto element = value->toData(rankZeroElement); element.has_value()) {
                return ArrayData::rankZero(rankZeroValueType, *element);
            }
        }
        return nullptr;
    }
    if (type->assignableFrom(data->type())) {
        return data;
    }
    if (isComposite(data->type()->code())) {
        // A nested literal: an array in an array, a struct in an array, and so on.
        return compositeDataInType(data, type, origin);
    }
    const literal::Value *exact = writtenValueOf(written);
    const std::optional<literal::Value> fallback =
        exact == nullptr ? exactValueOfData(data) : std::nullopt;
    const literal::Value *value = exact != nullptr ? exact : fallback ? &*fallback : nullptr;
    // The element's own position and its source text, as in a declared position (design
    // principles 5.1); the whole literal's position only when the GCT kept no element.
    const bool ownPosition =
        written != nullptr && written->origin != camel::source::kInvalidOriginId;
    const camel::source::origin_id_t where = ownPosition ? written->origin : origin->origin();
    const camel::source::origin_id_t writtenAt =
        ownPosition ? written->origin : camel::source::kInvalidOriginId;
    if (data->type()->code() == TypeCode::String) {
        // A text where another type is expected is the element's own type mismatch, reported at
        // the element (design principles 5.1). There is no text-to-number conversion.
        rejectLiteralData(data, type, where, nullptr, writtenAt);
    }
    if (value == nullptr || !isNumericType(type) || !isNumericType(data->type())) {
        // Not a numeric literal (a bool, for one): the type is not one it can be given by
        // taking the position's type, so the value is left as it is.
        return nullptr;
    }
    if (!literal::kindEnters(*value, type)) {
        rejectLiteralData(data, type, where, nullptr, writtenAt);
    }
    if (const auto converted = value->toData(type); converted.has_value()) {
        return *converted;
    }
    rejectLiteralData(data, type, where, nullptr, writtenAt);
}

} // namespace camel::compile::gir
