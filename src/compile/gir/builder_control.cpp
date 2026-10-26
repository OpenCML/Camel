// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Control flow: branches, the arms of a match, loops, and the exit each of them gives.
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
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

node_handle_t
Builder::castNodeTo(node_handle_t value, Type *target, const GCT::node_ptr_t &origin) {
    node_handle_t castNode = currGraph_->addCastNode(target);
    linkNodes(LinkType::Norm, value, castNode);
    registerNodeOrigin(context_, castNode, origin, "gir.cast");
    return castNode;
}

namespace {

/// The value a body hands on, read on the graph rather than by how the source spelled it.
///
/// A name is not a computation: `let r = (xs->append<7>, n + 1)` followed by `r` leaves the
/// tuple's own FILL as the body's value, exactly as writing the tuple in place does, so a name
/// needs no case of its own here. What a block can add around the value is an anchor: a GATE
/// that orders the value after the block's effects forwards it unchanged (`sync { …; r }` ends
/// in one), and the criterion is the value behind it. A CALL is not looked through: what a call
/// returns is computed in another graph (a function's), whose state
/// this is not — a loop state is only written in place when the graph that returns it also
/// builds it.
node_handle_t loopStateValueOf(node_handle_t node) {
    // A gate chain is at most the number of blocks the value passed through; a bound keeps a
    // malformed cycle from looping forever.
    for (size_t depth = 0; node != nullptr && depth < 16; ++depth) {
        if (!nodeIsKind(node, runtime::GCNodeKind::Gate)) {
            return node;
        }
        const auto inputs = normInputsOf(node);
        if (inputs.size() != 1) {
            return node; // a gate that waits on nothing has no value to forward
        }
        node = nodeDraftOf(node)->ref(inputs.front());
    }
    return node;
}

/// Gives `node` a new value type, moving it to a slot of that type. Used when the element type
/// of an empty-array loop state is learned from the body's append after the graph was built.
void retypeValueNode(DraftGraphBuilder *graph, node_handle_t node, Type *type) {
    if (graph == nullptr || node == nullptr || type == nullptr) {
        return;
    }
    runtime::GraphDraft &draft = graph->draft();
    const auto id              = nodeIdOf(node);
    draft.setNodeDataType(id, type);
    if (nodeIsKind(node, runtime::GCNodeKind::Data)) {
        return; // a static slot, not a frame slot
    }
    draft.setNodeDataIndex(id, graph->addRuntimeSlot(type));
}

/// A state initialised by the empty array literal and nothing else: the only shape whose
/// element type the loop may still have to learn from the body.
bool isEmptyArrayLiteral(node_handle_t node) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Fill) &&
        !nodeIsKind(node, runtime::GCNodeKind::Data)) {
        return false;
    }
    const Type *type = valueTypeOf(nodeTypeOf(node));
    if (type == nullptr || type->code() != TypeCode::Array) {
        return false;
    }
    if (isUntypedArrayType(type)) {
        return true; // the untyped `[]`: it has no elements by construction
    }
    if (nodeIsKind(node, runtime::GCNodeKind::Data)) {
        // A typed `[]` is a static array: its length says whether the literal was empty.
        const runtime::GraphDraft &draft = *nodeDraftOf(node);
        const int32_t dataIndex          = draft.node(nodeIdOf(node))->header.dataIndex;
        if (dataIndex < 0 && static_cast<size_t>(-dataIndex) < draft.staticSlots().size()) {
            const auto *array = camel::core::rtdata::fromSlot<::Array *>(
                draft.staticSlots()[static_cast<size_t>(-dataIndex)]);
            return array == nullptr || array->size() == 0;
        }
        return false;
    }
    return withInputsOf(node).empty();
}

/// The type of the untyped empty array literal `[]`: an array whose element type nobody wrote.
/// Its element type is the only thing it leaves open, so every position it stands in fixes it.
bool isEmptyArrayLiteralType(const Type *type) { return isUntypedArrayType(type); }

/// Whether a value of this type is a struct or tuple whose parts can be the components of a
/// loop state. A cell and the tagged forms are representations, not collections of values:
/// their parts are not independent values of the programme.
bool isSplittableAggregate(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::Struct) {
        const auto *strct = tt::as_ptr<const StructType>(type);
        return strct->resolved() && strct->size() > 0;
    }
    if (type->code() == TypeCode::Tuple) {
        const auto *tuple = tt::as_ptr<const TupleType>(type);
        return tuple->resolved() && tuple->size() > 0 && !tuple->isCell() && !tuple->isTagged();
    }
    return false;
}

size_t aggregatePartCount(const Type *type) {
    if (type->code() == TypeCode::Struct) {
        return tt::as_ptr<const StructType>(type)->size();
    }
    return tt::as_ptr<const TupleType>(type)->size();
}

Type *aggregatePartType(const Type *type, size_t index) {
    if (type->code() == TypeCode::Struct) {
        return tt::as_ptr<const StructType>(type)->typeAt(index);
    }
    return tt::as_ptr<const TupleType>(type)->typeAt(index);
}

std::string aggregatePartName(const Type *type, size_t index) {
    if (type->code() == TypeCode::Struct) {
        return std::string(tt::as_ptr<const StructType>(type)->fieldName(index));
    }
    return std::to_string(index);
}

/// The part of `type` an ACCS node reads, when it reads one: `s.x` of a struct, `t.0` of a
/// tuple, of exactly the part's type.
std::optional<size_t> accessedPartOf(node_handle_t node, const Type *type) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Accs) || type == nullptr) {
        return std::nullopt;
    }
    const auto norm = normInputsOf(node);
    if (norm.size() != 1) {
        return std::nullopt;
    }
    const auto payload = nodeDraftOf(node)->payloadOf(nodeIdOf(node));
    if (payload.size() < sizeof(runtime::GCAccsBody)) {
        return std::nullopt;
    }
    const auto *body = reinterpret_cast<const runtime::GCAccsBody *>(payload.data());
    size_t index     = 0;
    if (type->code() == TypeCode::Struct) {
        if (body->accsKind != runtime::GCAccsKind::StructKey) {
            return std::nullopt;
        }
        const auto field = tt::as_ptr<const StructType>(type)->findField(body->key());
        if (!field.has_value()) {
            return std::nullopt;
        }
        index = *field;
    } else if (type->code() == TypeCode::Tuple) {
        if (body->accsKind != runtime::GCAccsKind::TupleIndex ||
            body->value >= tt::as_ptr<const TupleType>(type)->size()) {
            return std::nullopt;
        }
        index = body->value;
    } else {
        return std::nullopt;
    }
    Type *part = aggregatePartType(type, index);
    Type *read = valueTypeOf(nodeTypeOf(node));
    if (part == nullptr || read == nullptr || (read != part && !read->equals(part))) {
        return std::nullopt;
    }
    return index;
}

/// The next value of the part `step` of `value` (`value` is a struct or tuple FILL, or any value
/// of that type). A FILL's parts are its With inputs, which is what lets an `append` among them
/// keep growing the state's array in place; a part the FILL does not write is read from it.
node_handle_t projectPart(node_handle_t value, const Type *type, size_t step) {
    if (value == nullptr || type == nullptr) {
        return nullptr;
    }
    node_handle_t source = value;
    // A gate forwards its value unchanged; an identity CAST between two spellings of the same
    // struct layout does not move a field (design principles 5.2). Both are looked through, so
    // that a struct written in a `sync` body or under a named type still gives its parts.
    for (size_t depth = 0; depth < 16; ++depth) {
        if (nodeIsKind(source, runtime::GCNodeKind::Gate)) {
            const auto inputs = normInputsOf(source);
            if (inputs.size() != 1) {
                break;
            }
            source = nodeDraftOf(source)->ref(inputs.front());
            continue;
        }
        if (nodeIsKind(source, runtime::GCNodeKind::Cast)) {
            const auto inputs = normInputsOf(source);
            Type *from = inputs.empty()
                             ? nullptr
                             : valueTypeOf(nodeTypeOf(nodeDraftOf(source)->ref(inputs.front())));
            Type *to   = valueTypeOf(nodeTypeOf(source));
            if (inputs.size() == 1 && from != nullptr && to != nullptr &&
                from->code() == TypeCode::Struct && to->code() == TypeCode::Struct &&
                to->castSafetyFrom(from) == CastSafety::Safe) {
                source = nodeDraftOf(source)->ref(inputs.front());
                continue;
            }
        }
        break;
    }
    if (nodeIsKind(source, runtime::GCNodeKind::Fill)) {
        const auto payload = nodeDraftOf(source)->payloadOf(nodeIdOf(source));
        if (payload.size() >= sizeof(runtime::GCFillBody)) {
            const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
            const bool structFill =
                fill->fillKind == runtime::GCFillKind::Struct && type->code() == TypeCode::Struct;
            const bool tupleFill =
                fill->fillKind == runtime::GCFillKind::Tuple && type->code() == TypeCode::Tuple;
            if (structFill || tupleFill) {
                const auto slots = fill->slots();
                const auto with  = withInputsOf(source);
                for (size_t i = 0; i < slots.size() && i < with.size(); ++i) {
                    if (slots[i] == step) {
                        return nodeDraftOf(source)->ref(with[i]);
                    }
                }
            }
        }
    }
    return nullptr;
}

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
    if (source == nullptr || source->size() != target->size()) {
        return nullptr;
    }
    // The fields must be the same ones, by name, before anything is copied.
    for (size_t i = 0; i < target->size(); ++i) {
        if (!source->findField(target->fieldName(i)).has_value()) {
            return nullptr;
        }
    }
    // A struct of `target`'s shape, its slots filled from the source's fields in the target's
    // order: the copy is what makes `{ y: 1, x: 2 }` a `Point` (fields by name, declaration
    // order) and what converts between two named types that write their fields in another
    // order (design principles 5.2).
    std::vector<size_t> slots(target->size());
    std::iota(slots.begin(), slots.end(), 0);
    const slot_t empty =
        camel::core::rtdata::toSlot(::Struct::create(target->size(), camel::core::mm::autoSpace()));
    auto &draft        = currGraph_->draft();
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
        node_handle_t access =
            currGraph_->addAccsNode(sourceFieldType, std::string(source->fieldName(from)));
        linkNodes(LinkType::Norm, valueOf(value), access);
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
            } else if (isEmptyArrayLiteralType(sourceFieldType) &&
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
    if (!entersImplicitly(target, source)) {
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
    ASSERT(target->code() == TypeCode::Array, "Only an empty array literal is left here.");
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
    if (type->assignableFrom(data->type())) {
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
                origin != nullptr ? GCT::Tree::of(origin) : nullptr, arrayType, data)) {
            return nullptr;
        }
        const std::vector<Type *> holeTypes = holeTypesByIndex();
        if (holeTypes.size() != array->raw().size()) {
            return nullptr;
        }
        ArrayDataFactory factory;
        factory.setElemType(elemType);
        for (size_t i = 0; i < array->raw().size(); ++i) {
            if (holeTypes[i] != nullptr) {
                if (!elemType->assignableFrom(holeTypes[i])) {
                    return nullptr;
                }
                factory.add(array->raw()[i]);
                continue;
            }
            data_ptr_t converted = literalElementInType(
                array->raw()[i], elemType, origin, literalElementAt(written, i));
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(converted);
        }
        return factory.build();
    }
    if (position->code() == TypeCode::Struct) {
        auto *structType       = tt::as_ptr<StructType>(position);
        const auto *ownType    = tt::as_ptr<StructType>(data->type());
        const auto object      = std::dynamic_pointer_cast<StructData>(data);
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
                if (fieldType == nullptr || !fieldType->assignableFrom(hole)) {
                    return nullptr;
                }
                factory.add(name, value);
            } else {
                data_ptr_t converted = literalElementInType(
                    value, fieldType, origin, literalElementAt(written, index));
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
        for (size_t i = 0; i < object->raw().size(); ++i) {
            Type *elemType = tupleType->typeAt(i);
            if (holeTypes[i] != nullptr) {
                if (elemType == nullptr || !elemType->assignableFrom(holeTypes[i])) {
                    return nullptr;
                }
                factory.add(object->raw()[i]);
                continue;
            }
            data_ptr_t converted = literalElementInType(
                object->raw()[i], elemType, origin, literalElementAt(written, i));
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

void Builder::coerceArmExit(const arm_ptr_t &arm, Type *target, const GCT::node_ptr_t &origin) {
    Type *exitType = arm->exitType();
    if (exitType != nullptr && target->assignableFrom(exitType)) {
        return;
    }
    // The nodes are added to the arm's own region; no name is looked up, so the scopes of the
    // position the branch stands at do no harm.
    runtime::GraphDraft &draft = arm->graph->draft();
    const uint32_t outerRegion = draft.insertRegion();
    draft.setInsertRegion(arm->region);
    node_handle_t output  = arm->output;
    node_handle_t coerced = coerceTo(output, target, origin);
    ASSERT(coerced != nullptr, "A branch arm is joined only with a type its value enters.");
    if (normInputsOf(coerced).empty() && !ctrlInputsOf(output).empty()) {
        // A literal took the place of the output (`[]`): what the output waited for (the
        // statements of a sync arm) is still waited for.
        node_handle_t gate = currGraph_->addGateNode(target);
        linkNodes(LinkType::Norm, coerced, gate);
        linkNodes(LinkType::Ctrl, output, gate);
        coerced = gate;
    }
    draft.setInsertRegion(outerRegion);
    arm->output = coerced;
    arm->value  = coerced;
    arm->type   = target;
}

Type *Builder::declaredExitTypeHere() const {
    if (activeRestBody_ != nullptr && activeStaticBody_ == nullptr) {
        // The body of a rest function declares its own result type, whichever graph it is
        // expanded into.
        return activeRestBody_->exitType;
    }
    FunctionType *funcType = currGraph_ != nullptr ? currGraph_->funcType() : nullptr;
    // An undeclared return type is set from the first value returned (see
    // checkDeclaredReturn): before that, a type that is present was declared.
    if (funcType == nullptr || !funcType->hasExitType() ||
        currGraph_->draft().exitNode() != runtime::kInvalidNodeRef) {
        return nullptr;
    }
    return funcType->exitType();
}

Type *Builder::ownDeclaredExitType() const {
    if (activeArm() != nullptr) {
        return nullptr; // an arm declares no type of its own
    }
    FunctionType *funcType = currGraph_ != nullptr ? currGraph_->funcType() : nullptr;
    if (funcType == nullptr || !funcType->hasExitType() ||
        currGraph_->draft().exitNode() != runtime::kInvalidNodeRef) {
        return nullptr;
    }
    return funcType->exitType();
}

Type *Builder::tailPositionType() const {
    // A function's last expression is the value it returns; a lambda's is its own. An arm's
    // body has no declared type of its own: the branch's position is what the arm stands at.
    Type *declared = ownDeclaredExitType();
    return declared != nullptr ? declared : expectedValueType_;
}

Type *Builder::expectedExitTypeHere() const {
    if (activeArm() != nullptr || activeStaticBody_ != nullptr) {
        // Inside a branch arm, or the selected body of a `match type`, the value of a `return`
        // is that construct's value, so its position is the one the construct stands at; the
        // function's declared return type has nothing to do with it (design principles 5.1,
        // language reference 7.3).
        return expectedValueType_;
    }
    return declaredExitTypeHere();
}

node_handle_t Builder::adaptExitValue(const GCT::node_ptr_t &gct, node_handle_t value) {
    Type *declared   = declaredExitTypeHere();
    Type *valueType  = valueTypeOf(nodeTypeOf(value));
    const auto where = gct->origin() != 0 ? gct->origin() : gct->at(0)->origin();
    if (gct->exitType() == ExitType::Throw) {
        // `throw e`: the exit `return` is, with the failed form of the declared `T!` as its
        // value (design principles 3.9). OPER __throw__ makes the failed value, the CAST
        // gives it the declared type; both are the same object.
        TupleType *fallible = asFallible(declared);
        if (fallible == nullptr) {
            diags_->of(SemanticDiag::ThrowOutsideFallible)
                .atOrigin(where)
                .commit(
                    declared == nullptr
                        ? "the function around it declares no return type"
                        : "the function around it returns '" + declared->toString() + "'");
            throw BuildAbortException();
        }
        // Design principles 3.9: `throw` takes an error value (made by `error(kind,
        // message)` or bound by `case error e`), nothing else.
        if (!valueType->equals(errorType())) {
            diags_->of(SemanticDiag::ThrowNotAnError).atOrigin(where).commit(valueType->toString());
            throw BuildAbortException();
        }
        node_handle_t thrown =
            applyTagOperator(camel::core::oper::TagThrow, bindTemporary(value), gct->at(0));
        return fallible->equals(nodeTypeOf(thrown)) ? thrown
                                                    : castNodeTo(thrown, fallible, gct->at(0));
    }
    // `return v` under a declared `T?` / `T!` puts `v` into that form: the counterpart of
    // `throw`, and one of the positions where a value enters by itself (design principles
    // 3.8; coerceTo). The declared type is also the name the value is used as: an unnamed
    // struct value in a declared named type is given that type here (a CAST that moves
    // nothing, or a copy when the fields are written in another order; design principles
    // 5.1, 5.2), so a named type is never erased to its shape. Applying the position's type
    // unless the value already has it is the same rule checkValue applies everywhere else;
    // a value that fits neither way is left alone for checkDeclaredReturn to report. In a
    // branch arm the position is the arm's (design principles 5.1: a `return` there is not the
    // function's return).
    Type *position = expectedExitTypeHere();
    if (position != nullptr && !position->equals(valueType)) {
        if (node_handle_t entered = coerceTo(value, position, gct->at(0))) {
            return entered;
        }
    }
    return value;
}

void Builder::enterBlockScope() {
    nodeScope_      = nodeScope_->enter("");
    graphScope_     = graphScope_->enter("");
    decoratedScope_ = decoratedScope_->enter("");
    templateScope_  = templateScope_->enter("");
}

void Builder::leaveBlockScope() {
    nodeScope_      = nodeScope_->leave();
    graphScope_     = graphScope_->leave();
    decoratedScope_ = decoratedScope_->leave();
    templateScope_  = templateScope_->leave();
}

void Builder::noteVariable(node_handle_t variable) {
    ArmBuild *arm = activeArm();
    if (arm == nullptr || nodeGraphOf(variable) != arm->graph) {
        return;
    }
    const auto [at, fresh] = arm->touched.try_emplace(variable);
    if (!fresh) {
        return;
    }
    arm->touchedOrder.push_back(variable);
    if (auto writer = modifierOf(variable); writer.has_value()) {
        at->second.writerBefore = nodeIdOf(*writer);
    }
    if (auto uses = pendingUses_.find(variable); uses != pendingUses_.end()) {
        at->second.usesBefore = uses->second;
    }
}

Builder::arm_ptr_t Builder::buildArm(
    const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body,
    runtime::GCRegionKind kind, const std::function<void(ArmBuild &)> &declare) {
    auto arm                   = std::make_shared<ArmBuild>();
    runtime::GraphDraft &draft = currGraph_->draft();
    const uint32_t outerRegion = draft.insertRegion();
    arm->graph                 = currGraph_.get();
    arm->region                = draft.addRegion(kind, outerRegion);
    arm->kind                  = kind;
    arm->origin                = exec != nullptr ? exec->origin() : camel::source::kInvalidOriginId;
    arm->firstNode             = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
    if (context_ && context_->sourceContext()) {
        const auto origin = deriveGirOrigin(
            context_,
            exec,
            camel::source::OriginKind::Graph,
            kind == runtime::GCRegionKind::Arm ? "gir.brch.case.graph" : "gir.loop.graph",
            {});
        if (origin != camel::source::kInvalidOriginId) {
            draft.setRegionOrigin(arm->region, static_cast<uint32_t>(origin));
        }
    }
    enterBlockScope();
    draft.setInsertRegion(arm->region);
    armStack_.push_back(arm.get());
    // A body given by the caller is a body of its own like any other (see visitBodyNode).
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto finish             = [&] {
        for (node_handle_t variable : arm->touchedOrder) {
            ArmBuild::Touched &touched = arm->touched[variable];
            if (auto writer = modifierOf(variable);
                writer.has_value() &&
                (!touched.writerBefore.has_value() || *touched.writerBefore != nodeIdOf(*writer))) {
                touched.writerAfter = *writer;
            }
            if (auto uses = pendingUses_.find(variable); uses != pendingUses_.end()) {
                touched.usesAfter = uses->second;
            }
            auto &modifiers = nodeModifierMaps_[nodeGraphOf(variable)];
            if (touched.writerBefore.has_value()) {
                modifiers[nodeIdOf(variable)] = *touched.writerBefore;
            } else {
                modifiers.erase(nodeIdOf(variable));
            }
            if (touched.usesBefore.empty()) {
                pendingUses_.erase(variable);
            } else {
                pendingUses_[variable] = touched.usesBefore;
            }
        }
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        armStack_.pop_back();
        draft.setInsertRegion(outerRegion);
        leaveBlockScope();
    };
    try {
        if (declare) {
            declare(*arm);
        }
        if (body) {
            synced_         = false;
            lastSyncedNode_ = {};
            setArmResult(*arm, body());
        } else {
            setArmResult(*arm, visitBodyNode(exec));
        }
    } catch (...) {
        finish();
        throw;
    }
    finish();
    return arm;
}

void Builder::setArmResult(ArmBuild &arm, node_handle_t result) {
    if (arm.returned) {
        return;
    }
    if (result == nullptr) {
        result     = currGraph_->addStaticDataNode(Data::null());
        arm.output = result;
        arm.value  = result;
        arm.type   = Type::Void();
        return;
    }
    // A sync block that ends in a value hands it back behind a GATE (see visitExecNode): the
    // GATE is what the arm gives, the value it forwards is the arm's value.
    node_handle_t value = result;
    if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
        value = nodeDraftOf(result)->ref(normInputsOf(result).front());
    }
    arm.output = result;
    arm.value  = value;
    arm.type   = nodeTypeOf(value);
}

node_handle_t Builder::visitTaggedBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    const Kind kind    = gct->branchKind();
    const bool onNull  = kind == Kind::NullThen;
    const char *symbol = onNull ? "??" : kind == Kind::ErrorThen ? "!!" : "?->";

    // The left side is a `T?` (`T!`) as the name it is, and the right side is a `T` or the
    // same `T?` (design principles 3.8): neither is in the position the whole expression
    // stands at, so neither takes that position's type. The position's type applies to the
    // branch's value once, where the branch stands (`checkValue`); leaking it down here gave
    // the subject's arms a `float?` and the right side the form of the whole result.
    node_handle_t subject = nullptr;
    {
        ExpectedValueScope subjectScope(expectedValueType_, nullptr);
        subject = valueNodeOf(visit(gct->at(0)));
    }
    if (onNull && asTagged(valueTypeOf(nodeTypeOf(subject))) == nullptr &&
        isNarrowedName(gct->at(0))) {
        // `x ?? d` where an `if` around it already narrowed `x` to a `T` (design principles
        // 3.8): allowed, with a warning.
        subject = reopenNarrowedName(gct->at(0), subject, gct, "??");
    }
    Type *subjectType = valueTypeOf(nodeTypeOf(subject));
    TupleType *tagged = asTagged(subjectType);
    if (tagged == nullptr || tagged->isNullable() != onNull) {
        // DESIGN-QUESTION(nullable-error) Q6: `?->` is defined for errors only.
        diags_->of(SemanticDiag::TaggedOperandMismatch)
            .atOrigin(gct->origin())
            .commit(
                symbol,
                "its left side has type '" + subjectType->toString() + "', which is not a '" +
                    (onNull ? "T?" : "T!") + "'");
        throw BuildAbortException();
    }
    Type *payload          = tagged->payloadType();
    const std::string name = bindTemporary(subject);
    node_handle_t tagNode  = applyTagOperator(
        onNull ? camel::core::oper::TagIsNull : camel::core::oper::TagIsError,
        name,
        gct);
    const camel::core::OperId valueOp =
        onNull ? camel::core::oper::TagNullValue : camel::core::oper::TagErrorValue;
    const GCT::node_ptr_t exec = gct->at(1)->at(0);
    std::vector<arm_ptr_t> arms(2);

    if (kind != Kind::ErrorPass) {
        // Arm 0 is the right-hand side, a subgraph of its own: it is entered, and so
        // evaluated, only when the tag selects it. Arm 1 takes the value out. The right
        // side's position is the payload type: a plain `T` is what it is, and a `T?` is what
        // the chained form hands on (a value of another form is left as it is, and the test
        // below names it).
        {
            ExpectedValueScope fallbackScope(expectedValueType_, payload);
            arms[0] = buildArm(exec);
        }
        // Design principles 3.8: the right-hand side is a `T`, and the result is a `T`; or
        // it is itself a `T?` (`T!` for `!!`), and the result is that: `a ?? b ?? 9` tries
        // `a`, then `b`, then gives the default. Arm 1 then hands on `a` as it is instead of
        // taking the value out. Either way it is one branch.
        Type *fallback   = arms[0]->exitType();
        const bool plain = fallback != nullptr && (payload->unify(fallback) != nullptr ||
                                                   entersImplicitly(payload, fallback));
        const bool chained =
            !plain && fallback != nullptr && entersImplicitly(subjectType, fallback);
        if (plain) {
            if (payload->unify(fallback) == nullptr) {
                coerceArmExit(arms[0], payload, gct);
            }
            arms[1] = buildArm(exec, [&] { return applyTagOperator(valueOp, name, gct); });
        } else if (chained) {
            coerceArmExit(arms[0], subjectType, gct);
            arms[1] = buildArm(exec, [&] {
                const GCT::node_ptr_t dref = tree_->makeDRef(name);
                dref->setOrigin(gct->origin());
                return valueNodeOf(visitDRefNode(dref));
            });
        } else {
            diags_->of(SemanticDiag::TaggedOperandMismatch)
                .atOrigin(gct->origin())
                .commit(
                    symbol,
                    "its right side has type '" +
                        (fallback ? fallback->toString() : std::string("void")) +
                        "', but the left side holds a '" + payload->toString() + "'");
            throw BuildAbortException();
        }
        return buildBranch(gct, tagNode, &arms);
    }

    // `x ?-> f`: arm 1 calls `f` with the value and yields a `U!` (put into that form when
    // `f` returns a plain `U`); arm 0 hands the failed `x` on as that `U!`. Arm 1 is built
    // first because it is what determines `U`.
    Type *resultType = nullptr;
    arms[1]          = buildArm(exec, [&] {
        insertNode(GCT::kPassedValueName, applyTagOperator(valueOp, name, gct));
        node_handle_t result = visitBodyNode(exec);
        ASSERT(result != nullptr, "The call of '?->' produced no node.");
        if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
            result = nodeDraftOf(result)->ref(normInputsOf(result).front());
        }
        Type *type = valueTypeOf(nodeTypeOf(result));
        if (asFallible(type) != nullptr) {
            resultType = type;
            return result;
        }
        resultType = TupleType::fallible(type);
        return castNodeTo(result, resultType, gct);
    });
    arms[0]          = buildArm(exec, [&] {
        const GCT::node_ptr_t dref = tree_->makeDRef(name);
        dref->setOrigin(gct->origin());
        node_handle_t failed = valueNodeOf(visitDRefNode(dref));
        return resultType->equals(subjectType) ? failed : castNodeTo(failed, resultType, gct);
    });
    return buildBranch(gct, tagNode, &arms);
}

node_handle_t
Builder::visitTaggedMatch(const GCT::node_ptr_t &gct, node_handle_t scrutinee, TupleType *type) {
    using CaseType       = GCT::CaseType;
    const auto malformed = [&](const std::string &why) {
        diags_->of(SemanticDiag::TaggedMatchCases)
            .atOrigin(gct->origin())
            .commit(type->toString(), why);
        throw BuildAbortException();
    };
    // DESIGN-QUESTION(nullable-error) Q4: exactly the tagged case and one other.
    // A case with a guard is taken only where its guard holds, so it is not the case that
    // opens the value: each side needs one case without a guard, and the cases written before
    // it may carry guards.
    std::vector<GCT::node_ptr_t> taggedSide, restSide;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        const bool isNull    = caseType == CaseType::Value &&
                               caseNode->at(0)->type() == GCT::NodeKind::DATA &&
                               caseNode->at(0)->data()->type()->code() == TypeCode::Void;
        if (isNull || caseType == CaseType::Error) {
            if (isNull != type->isNullable()) {
                malformed(
                    isNull ? "'case null' needs a value that may be null"
                           : "'case error' needs a value that may have failed");
            }
            taggedSide.push_back(caseNode);
        } else if (caseType == CaseType::Bind || caseType == CaseType::Else) {
            restSide.push_back(caseNode);
        } else {
            malformed("a literal case cannot be compared with a value that is not opened yet");
        }
    }
    const bool anyGuard = hasGuardedCase(gct, 1);
    if (!anyGuard) {
        // Without a guard the cases of a side are one and the same case: what the old form
        // says, and what its reports name.
        if (taggedSide.size() > 1) {
            malformed(std::string("it has more than one '") +
                      (type->isNullable() ? "case null" : "case error") + "'");
        }
        if (restSide.size() > 1) {
            malformed("it has more than one case for the value");
        }
    }
    const auto openingCase = [&](const std::vector<GCT::node_ptr_t> &side) -> GCT::node_ptr_t {
        // The case that opens the value of its side: the one without a guard. Every case
        // written after it for the same side can never be taken.
        GCT::node_ptr_t opening = nullptr;
        for (const GCT::node_ptr_t &caseNode : side) {
            const bool guarded = tree_->caseGuardOf(caseNode) != nullptr;
            if (opening != nullptr && !guarded) {
                malformed("a case of one side follows the case that takes that side");
            }
            if (!guarded && opening == nullptr) {
                opening = caseNode;
            }
        }
        return opening;
    };
    const GCT::node_ptr_t taggedCase = openingCase(taggedSide);
    const GCT::node_ptr_t restCase   = openingCase(restSide);
    if (taggedCase == nullptr) {
        malformed(type->isNullable() ? "'case null' is missing" : "'case error e' is missing");
    }
    if (restCase == nullptr) {
        malformed("the case for the value is missing ('case v' or 'case _')");
    }

    const std::string name = bindTemporary(scrutinee);
    const auto tagOf       = [&](const GCT::node_ptr_t &origin) {
        return applyTagOperator(
            type->isNullable() ? camel::core::oper::TagIsNull : camel::core::oper::TagIsError,
            name,
            origin);
    };
    if (anyGuard) {
        // The cases in written order, as one decision list: each tests the side it takes, and
        // the cases written after it are the `else` of its step, so a guard that does not hold
        // leaves the value to them.
        std::vector<CaseStep> steps;
        for (size_t i = 1; i < gct->size(); ++i) {
            const GCT::node_ptr_t &caseNode = gct->at(i);
            const auto caseType             = caseNode->caseType();
            const bool isNull               = caseType == CaseType::Value &&
                               caseNode->at(0)->type() == GCT::NodeKind::DATA &&
                               caseNode->at(0)->data()->type()->code() == TypeCode::Void;
            const bool onTagged = isNull || caseType == CaseType::Error;
            CaseStep caseStep;
            caseStep.guard = tree_->caseGuardOf(caseNode);
            caseStep.exec  = caseNode->at(caseNode->size() - 1);
            if (caseStep.guard != nullptr || i + 1 < gct->size()) {
                // The last case is the one the list ends at, and it needs no test: nothing
                // written after it can meet the value. Every other case says which side it
                // takes, so that the value a guard of a case before it refused reaches it.
                node_handle_t tag   = tagOf(caseNode);
                caseStep.condition = onTagged
                                          ? tag
                                          : negateCondition(tag, caseNode);
            }
            caseStep.bind = [&, caseNode, onTagged, isNull, caseType] {
                if (onTagged) {
                    if (type->isFallible()) {
                        insertNode(
                            caseNode->binding(),
                            applyTagOperator(
                                camel::core::oper::TagErrorInfo, name, caseNode));
                    }
                } else if (caseType == CaseType::Bind) {
                    insertNode(
                        caseNode->binding(),
                        applyTagOperator(
                            type->isNullable() ? camel::core::oper::TagNullValue
                                               : camel::core::oper::TagErrorValue,
                            name,
                            caseNode));
                }
            };
            steps.push_back(std::move(caseStep));
        }
        return buildCaseChain(steps, gct);
    }

    node_handle_t tagNode = tagOf(gct);
    const GCT::node_ptr_t taggedExec = taggedCase->at(taggedCase->size() - 1);
    const GCT::node_ptr_t restExec   = restCase->at(0);
    std::vector<arm_ptr_t> arms(2);
    arms[0] = buildArm(taggedExec, [&] {
        if (type->isFallible()) {
            insertNode(
                taggedCase->binding(),
                applyTagOperator(camel::core::oper::TagErrorInfo, name, taggedCase));
        }
        return visitBodyNode(taggedExec);
    });
    arms[1] = buildArm(restExec, [&] {
        if (restCase->caseType() == CaseType::Bind) {
            insertNode(
                restCase->binding(),
                applyTagOperator(
                    type->isNullable() ? camel::core::oper::TagNullValue
                                       : camel::core::oper::TagErrorValue,
                    name,
                    restCase));
        }
        return visitBodyNode(restExec);
    });
    return buildBranch(gct, tagNode, &arms);
}

namespace {

bool isNullLiteral(const GCT::node_ptr_t &expr) {
    return expr->type() == GCT::NodeKind::DATA && expr->data()->type()->code() == TypeCode::Void;
}

bool isLogicalBranch(const GCT::node_ptr_t &expr, GCT::BranchKind kind) {
    return expr->type() == GCT::NodeKind::BRCH && expr->branchKind() == kind;
}

/// The right side of the `&&` / `||` branch `brch` (see GCT::BranchKind for its layout).
GCT::node_ptr_t rightSideOf(const GCT::node_ptr_t &brch) {
    const bool isAnd = brch->branchKind() == GCT::BranchKind::LogicalAnd;
    return brch->at(isAnd ? 1 : 2)->at(0)->at(0);
}

/// One comparison of a name with `null` inside a condition.
struct NullTest {
    std::string name;
    GCT::node_ptr_t expr;    // the comparison
    GCT::node_ptr_t subject; // the name in it
    /// The name is not null where the condition the test is part of holds (`x != null`), as
    /// opposed to where it does not (`x == null`).
    bool nonNullWhenTrue;
    /// Not under a `!` (`!(x == null)` is not one of the forms narrowing follows).
    bool direct;
};

/// `expr` as the comparison of a name with `null`, in either order.
std::optional<NullTest> nullTestOf(const GCT::node_ptr_t &expr) {
    if (expr->type() != GCT::NodeKind::LINK || expr->size() != 3 ||
        expr->at(0)->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string op = expr->at(0)->name();
    if (op != "__eq__" && op != "__neq__") {
        return std::nullopt;
    }
    const bool nullFirst     = isNullLiteral(expr->at(1));
    const auto &subject      = expr->at(nullFirst ? 2 : 1);
    const auto &comparedWith = expr->at(nullFirst ? 1 : 2);
    if (!isNullLiteral(comparedWith) || subject->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    return NullTest{subject->name(), expr, subject, op == "__neq__", true};
}

/// Every comparison of a name with `null` the condition `expr` is made of, looking through
/// `&&`, `||` and `!`. This is wider than what narrowing follows (see conjunctsOf): the
/// tests found here and not followed are the ones a diagnostic explains.
void collectNullTests(const GCT::node_ptr_t &expr, bool negated, std::vector<NullTest> &tests) {
    if (auto test = nullTestOf(expr)) {
        test->nonNullWhenTrue = test->nonNullWhenTrue != negated;
        test->direct          = test->direct && !negated;
        tests.push_back(std::move(*test));
    } else if (
        expr->type() == GCT::NodeKind::LINK && expr->size() == 2 &&
        expr->at(0)->type() == GCT::NodeKind::DREF && expr->at(0)->name() == "__not__") {
        collectNullTests(expr->at(1), !negated, tests);
    } else if (
        isLogicalBranch(expr, GCT::BranchKind::LogicalAnd) ||
        isLogicalBranch(expr, GCT::BranchKind::LogicalOr)) {
        collectNullTests(expr->at(0), negated, tests);
        collectNullTests(rightSideOf(expr), negated, tests);
    }
}

/// The operands that `&&` joins in `expr` (`expr` itself when it is not an `&&`). Each of
/// them holds where `expr` does: a test against null among them narrows the then arm
/// (design principles 3.8).
void conjunctsOf(const GCT::node_ptr_t &expr, std::vector<const GCT::Node *> &conjuncts) {
    if (isLogicalBranch(expr, GCT::BranchKind::LogicalAnd)) {
        conjunctsOf(expr->at(0), conjuncts);
        conjunctsOf(rightSideOf(expr), conjuncts);
    } else {
        conjuncts.push_back(expr);
    }
}

/// Whether the name is written as a DREF anywhere in `node`. A use of another declaration of
/// the same name counts too, and a name kept in a constant (an element of an array literal)
/// is not seen: the answer only decides whether a diagnostic gets a note.
bool mentionsName(const GCT::node_ptr_t &node, const std::string &name) {
    if (node == nullptr) {
        return false;
    }
    if (node->type() == GCT::NodeKind::DREF && node->name() == name) {
        return true;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (mentionsName(node->at(i), name)) {
            return true;
        }
    }
    return false;
}

} // namespace

node_handle_t Builder::nullComparison(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
    const type_vec_t &types) {
    if ((group != "__eq__" && group != "__neq__") || operands.size() != 2) {
        return nullptr;
    }
    const bool nullFirst = isNullLiteral(operands[0].expr);
    if (nullFirst == isNullLiteral(operands[1].expr)) {
        return nullptr; // no `null` written, or two
    }
    // Only a `T?` has the tag asked about. Two `T?` values are not compared (no `null` is
    // written then).
    const size_t subject = nullFirst ? 1 : 0;
    Type *subjectType    = valueTypeOf(types[subject]);
    if (asNullable(subjectType) == nullptr) {
        // A value whose type is never null (`5`, an `int`, a name already narrowed to a `T`):
        // the test is allowed and its result is fixed, which is worth a warning (design
        // principles 3.8). The value itself is not needed, so nothing evaluates it. `null`
        // itself (type `void`) is left to the comparison of two `null`s, which is refused.
        if (subjectType == nullptr || subjectType->code() == TypeCode::Void) {
            return nullptr;
        }
        const bool fixed      = group == "__neq__";
        const auto &written   = operands[subject].expr;
        const std::string who = written->type() == GCT::NodeKind::DREF
                                    ? std::format("'{}'", written->name())
                                    : std::string("This value");
        diags_->of(SemanticDiag::NullTestNeverNull)
            .atOrigin(gct->origin())
            .commit(who, subjectType->toString(), fixed ? "true" : "false");
        const GCT::node_ptr_t literal =
            tree_->makeData(tt::as_shared<Data>(std::make_shared<BoolData>(fixed)));
        literal->setOrigin(gct->origin());
        return visitDataNode(literal);
    }
    node_handle_t isNull = applyTagOperator(
        camel::core::oper::TagIsNull,
        bindTemporary(valueOf(operands[subject].node)),
        gct);
    return group == "__eq__" ? isNull : applyTagOperator("__not__", bindTemporary(isNull), gct);
}

bool Builder::isNarrowedName(const GCT::node_ptr_t &expr) {
    if (expr->type() != GCT::NodeKind::DREF) {
        return false;
    }
    // The scope maps the name to the value the narrowing `if` bound in its arm, also from an
    // arm or a function nested in that arm (which reaches it through a capture).
    const auto bound = nodeScope_->get(expr->name());
    return bound.has_value() && narrowedValues_.contains(*bound);
}

node_handle_t Builder::reopenNarrowedName(
    const GCT::node_ptr_t &expr, node_handle_t subject, const GCT::node_ptr_t &at,
    const char *construct) {
    // Design principles 3.8: the construct is still allowed on the name; it is applied to the
    // value put back into `T?`, so that it is checked and built exactly as on the name before
    // narrowing (the right side of `??` still has to fit, the cases of the `match` still have
    // to be its fixed patterns), and only its null arm can never be taken.
    Type *type = valueTypeOf(nodeTypeOf(subject));
    diags_->of(SemanticDiag::KnownNotNull)
        .atOrigin(at->origin())
        .commit(expr->name(), type->toString(), construct);
    node_handle_t reopened = coerceTo(subject, TupleType::nullable(type), at);
    ASSERT(reopened != nullptr, "A value always enters the nullable form of its own type.");
    return reopened;
}

node_handle_t Builder::visitConditionBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    using CaseType     = GCT::CaseType;
    const Kind kind    = gct->branchKind();
    const bool logical = kind == Kind::LogicalAnd || kind == Kind::LogicalOr;
    if (gct->size() != 3 || gct->at(1)->caseType() != CaseType::True ||
        gct->at(2)->caseType() != CaseType::Else) {
        ASSERT(!logical, "'&&' and '||' are lowered with a true arm and an else arm.");
        return nullptr; // a `match`
    }
    const GCT::node_ptr_t &cond = gct->at(0);
    std::vector<NullTest> tests;
    collectNullTests(cond, false, tests);
    if (!logical && tests.empty()) {
        return nullptr; // an `if` that tests no name against null
    }

    // `&&` and `||` are branches (design principles 4.2), but their operands are not in the
    // position the whole expression stands at: the condition and both sides are `bool`
    // whatever that position wants. The position's type applies to the branch's value once,
    // where the branch stands (`checkValue`); leaking it down here made a `bool?` operand out
    // of the literal `false` in `let o: bool? = true || false`.
    Type *const operandType = logical ? Type::Bool() : expectedValueType_;
    ExpectedValueScope operandScope(expectedValueType_, operandType);

    // Narrowing (design principles 3.8). In the arm of `if x != null then A else B`
    // that excludes null, `x` is a `T`: the arm starts by taking the value out of `x`, and
    // the name `x` stands for that value inside the arm, as `v` does in
    // `match x { case null => B  case v => A }`. The value is taken out inside the arm
    // because doing so has a meaning only where the arm is selected.
    //
    // It applies to a name bound by `let` or a parameter (a `var` may be rewritten between
    // the test and the use), and to the direct forms only: `x != null` or `null != x` that
    // is the whole condition or one of the operands `&&` joins narrows the then arm
    // (`if x != null && ready then ...`); `x == null` or `null == x` that is the whole
    // condition narrows the else arm. Nothing under `!` or `||` narrows the arms of the `if`,
    // and the else arm of a conjunction learns nothing. A test that is found but not
    // followed is kept as the explanation of an error in the arm it would have narrowed.
    struct Opened {
        std::string name;
        GCT::node_ptr_t subject; // the name as the condition wrote it
        std::string temporary;   // what the arm captures (bindTemporary)
    };
    struct Refused {
        std::string name;
        std::string type;
        DiagLabel label;
    };
    struct Arm {
        GCT::node_ptr_t exec;
        std::vector<Opened> opened;
        std::vector<Refused> refused;
    };
    Arm thenArm{gct->at(1)->at(0), {}, {}}, elseArm{gct->at(2)->at(0), {}, {}};
    const std::optional<NullTest> whole = nullTestOf(cond);
    std::vector<const GCT::Node *> conjuncts;
    conjunctsOf(cond, conjuncts);
    for (const NullTest &test : tests) {
        const auto bound    = nodeAt(test.name);
        TupleType *nullable = bound.has_value() && *bound != nullptr
                                  ? asNullable(valueTypeOf(nodeTypeOf(*bound)))
                                  : nullptr;
        if (nullable == nullptr) {
            continue; // not a `T?`: nothing to narrow (and the comparison itself is an error)
        }
        Arm &arm            = test.nonNullWhenTrue ? thenArm : elseArm;
        const bool conjunct = std::ranges::find(conjuncts, test.expr) != conjuncts.end();
        const bool follows =
            test.direct && conjunct && (test.nonNullWhenTrue || conjuncts.size() == 1);
        const bool mutableName = isVariable(*bound);
        if (follows && !mutableName) {
            if (std::ranges::find(arm.opened, test.name, &Opened::name) == arm.opened.end()) {
                arm.opened.push_back({test.name, test.subject, {}});
            }
            continue;
        }
        const char *why =
            mutableName ? "a 'var' may be rewritten between the test and the use (bind its value "
                          "with 'let' and test that)"
            : test.direct && conjunct
                ? "where the operands of '&&' do not all hold, nothing is known about "
                  "any one of them"
                : "narrowing follows a test that is the whole condition or an operand "
                  "of '&&', not one under '||' or '!'";
        arm.refused.push_back(
            {test.name,
             nullable->toString(),
             DiagLabel{
                 SourceRange{test.subject->origin()},
                 std::format(
                     "'{}' still has the type '{}' in this arm; the test of it against null "
                     "does not narrow it: {}",
                     test.name,
                     nullable->toString(),
                     why),
                 ""}});
    }

    // The condition. One followed test that is the whole condition selects the arm by the
    // tag itself, which is the branch of the `match` above: arm 0 is the null arm. Any other
    // condition is a bool as in every `if`: arm 0 is the then arm.
    const bool byTag = whole.has_value() && thenArm.opened.size() + elseArm.opened.size() == 1;
    for (Arm *arm : {&thenArm, &elseArm}) {
        for (Opened &opened : arm->opened) {
            opened.temporary = bindTemporary(valueNodeOf(visit(opened.subject)));
        }
    }
    node_handle_t condNode =
        byTag ? applyTagOperator(
                    camel::core::oper::TagIsNull,
                    (thenArm.opened.empty() ? elseArm : thenArm).opened.front().temporary,
                    cond)
              : valueNodeOf(visit(cond));

    const char *symbol = kind == Kind::LogicalAnd ? "&&" : "||";
    // Reported at the operand that is not a bool.
    const auto requireBool = [&](Type *type, const char *side, const GCT::node_ptr_t &operand) {
        type = valueTypeOf(type);
        if (type == nullptr || type->code() != TypeCode::Bool) {
            const auto origin = operand->origin();
            diags_->of(SemanticDiag::LogicalOperandNotBool)
                .atOrigin(origin != 0 ? origin : gct->origin())
                .commit(symbol, side, type != nullptr ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
    };
    if (logical) {
        requireBool(nodeTypeOf(condNode), "left", cond);
    }

    const auto armOf = [&](const Arm &arm) {
        const size_t errorsBefore = diags_->count(Severity::Error);
        const size_t diagsBefore  = diags_->count();
        try {
            if (arm.opened.empty()) {
                return buildArm(arm.exec);
            }
            return buildArm(arm.exec, [&] {
                for (const Opened &opened : arm.opened) {
                    // Opened whether or not the arm seems to use the name: a name can be
                    // written where no DREF shows it (the elements of an array literal are
                    // names kept in the constant), and an unused node costs nothing at run
                    // time, as nothing depends on it.
                    node_handle_t value = applyTagOperator(
                        camel::core::oper::TagNullValue,
                        opened.temporary,
                        opened.subject);
                    [[maybe_unused]] const bool bound = insertNode(opened.name, value);
                    ASSERT(bound, "A narrowed name is the first name of its arm.");
                    narrowedValues_.insert(value);
                }
                return visitBodyNode(arm.exec);
            });
        } catch (const BuildAbortException &) {
            // An error in an arm that uses a name the condition tested without narrowing it,
            // and that speaks of the name's type: most likely the name was used as a `T`.
            const auto errors = diags_->errors();
            const auto first =
                errors.begin() + static_cast<std::ptrdiff_t>(std::min(errorsBefore, errors.size()));
            for (const Refused &refused : arm.refused) {
                if (mentionsName(arm.exec, refused.name) &&
                    std::any_of(first, errors.end(), [&](const Diagnostic *d) {
                        return d->message.find(refused.type) != std::string::npos;
                    })) {
                    diags_->addRelatedToErrorsFrom(diagsBefore, refused.label, 3);
                }
            }
            throw;
        }
    };
    // Arm 0 is the one the condition selects first: the null arm by tag, the then arm else.
    const bool thenFirst = !byTag || thenArm.opened.empty();
    std::vector<arm_ptr_t> arms(2);
    arms[thenFirst ? 0 : 1] = armOf(thenArm);
    arms[thenFirst ? 1 : 0] = armOf(elseArm);
    if (logical) {
        // The right side is the then arm of `&&` and the else arm of `||`.
        const bool rightIsThen = kind == Kind::LogicalAnd;
        requireBool(arms[rightIsThen == thenFirst ? 0 : 1]->exitType(), "right", rightSideOf(gct));
    }
    // An `if` without `else` has no value (design principles 3.7; see buildBranch).
    const bool valueless = !logical && elseArm.exec->size() == 0;
    return buildBranch(gct, condNode, &arms, valueless);
}

node_handle_t Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto kind = gct->branchKind();
    if (kind == GCT::BranchKind::NullThen || kind == GCT::BranchKind::ErrorThen ||
        kind == GCT::BranchKind::ErrorPass) {
        node_handle_t node = visitTaggedBrchNode(gct);
        LEAVE("BRCH");
        return node;
    }
    // `&&`, `||`, and an `if` whose condition compares names with `null` (design principles
    // 3.8). Any other `if`, and every `match`, is built below.
    if (node_handle_t node = visitConditionBrchNode(gct)) {
        LEAVE("BRCH");
        return node;
    }
    // The condition of an `if` is a `bool` (design principles 3.7), and the value a `match`
    // selects on is of its own type: neither is in the position the branch stands at, so the
    // position's type must not leak into what is built there. The position applies to
    // the branch's value once, where the branch stands.
    const bool ifBranch = gct->size() > 1 && gct->at(1)->caseType() == GCT::CaseType::True;
    node_handle_t condNode{};
    if (ifBranch) {
        condNode = visitInPosition(gct->at(0), Type::Bool());
    } else {
        ExpectedValueScope scrutineeScope(expectedValueType_, nullptr);
        condNode = valueNodeOf(visit(gct->at(0)));
    }
    node_handle_t node{};
    if (asTagged(valueTypeOf(nodeTypeOf(condNode))) == nullptr && isNarrowedName(gct->at(0))) {
        // `match x { case null => ...  case v => ... }` where an `if` around it already
        // narrowed `x` to a `T` (design principles 3.8): allowed, with a warning. A match
        // without `case null` is an ordinary match on the `T`.
        const bool nullCase = std::any_of(gct->begin() + 1, gct->end(), [](const auto &c) {
            return c->caseType() == GCT::CaseType::Value && isNullLiteral(c->at(0));
        });
        if (nullCase) {
            condNode = reopenNarrowedName(gct->at(0), condNode, gct, "match");
        }
    }
    if (TupleType *tagged = asTagged(valueTypeOf(nodeTypeOf(condNode)))) {
        node = visitTaggedMatch(gct, condNode, tagged);
    } else if (EnumType *enumType = asEnumType(valueTypeOf(nodeTypeOf(condNode)))) {
        node = visitEnumMatch(gct, condNode, enumType);
    } else {
        for (size_t i = 1; i < gct->size(); ++i) {
            const GCT::node_ptr_t &boundCase = gct->at(i);
            if (boundCase->caseType() == GCT::CaseType::Bind ||
                boundCase->caseType() == GCT::CaseType::Error) {
                // The two patterns that bind are the fixed forms of `T?` and `T!` (design
                // principles 3.8): over any other type there is no mark to take apart.
                const bool isError = boundCase->caseType() == GCT::CaseType::Error;
                diags_->of(SemanticDiag::IdentifierPatternNeedsNullableOrErrorType)
                    .atOrigin(boundCase->origin())
                    .commit(
                        (isError ? "error " : "") + boundCase->binding(),
                        nodeTypeOf(condNode)->toString());
                throw BuildAbortException();
            }
        }
        requireMatchDefault(gct, condNode);
        node = buildBranch(gct, condNode);
    }
    LEAVE("BRCH");
    return node;
}

void Builder::requireMatchDefault(const GCT::node_ptr_t &gct, node_handle_t scrutinee) {
    using CaseType    = GCT::CaseType;
    bool hasTrue = false, hasFalse = false, anyCase = false;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        if (caseType == CaseType::Else) {
            if (!caseNode->guardedCase()) {
                return; // an unguarded `case _` is the default of the match
            }
            // `case _ when c`: the branch is entered only when `c` holds, so it is not a
            // default. The cases written after it are still reached (the guarded wildcard does
            // not end the match), and the match still needs one.
            continue;
        }
        if (caseType != CaseType::Value) {
            continue;
        }
        anyCase = true;
        if (caseNode->guardedCase()) {
            // A guarded case covers nothing on its own (and Rust the same): the value a false guard refuses is matched by the
            // cases written after it, so it counts for neither `case _` nor a bool's values.
            continue;
        }
        if (caseNode->at(0)->type() == GCT::NodeKind::DATA) {
            const auto literal = std::dynamic_pointer_cast<BoolData>(caseNode->at(0)->data());
            if (literal) {
                (literal->data() ? hasTrue : hasFalse) = true;
            }
        }
    }
    if (!anyCase) {
        return; // an `if`
    }
    Type *type = valueTypeOf(nodeTypeOf(scrutinee));
    if (type == Type::Bool() && hasTrue && hasFalse) {
        return; // every value of the type has a case
    }
    // Design principles 3.7: a `match` must have a default case, and the compiler adds no
    // arm the program does not show. A `match` without `case _` is therefore refused here
    // (the fixed patterns over `T?` / `T!` never reach this function).
    diags_->of(SemanticDiag::MatchWithoutDefault).atOrigin(gct->origin()).commit(type->toString());
    throw BuildAbortException();
}

bool Builder::hasGuardedCase(const GCT::node_ptr_t &gct, size_t first) {
    for (size_t i = first; i < gct->size(); ++i) {
        if (tree_->caseGuardOf(gct->at(i)) != nullptr) {
            return true;
        }
    }
    return false;
}

node_handle_t Builder::negateCondition(node_handle_t value, const GCT::node_ptr_t &origin) {
    // `!b` is the branch `if b then false else true`: `not` over a bool, with no new node kind.
    const auto constant = [&](bool each) {
        node_handle_t node = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(each)));
        registerNodeOrigin(context_, node, origin, "gir.data");
        return node;
    };
    std::vector<arm_ptr_t> two(2);
    two[0] = buildArm(origin, [&] { return constant(false); });
    two[1] = buildArm(origin, [&] { return constant(true); });
    return buildBranch(origin, value, &two);
}

node_handle_t
Builder::buildCaseChain(std::vector<CaseStep> &steps, const GCT::node_ptr_t &origin) {
    node_handle_t always = currGraph_->addStaticDataNode(
        tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(true)));
    registerNodeOrigin(context_, always, origin, "gir.data");
    const auto boolConstant = [&](bool value) {
        node_handle_t node = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(value)));
        registerNodeOrigin(context_, node, origin, "gir.data");
        return node;
    };
    // The condition of a case: the test of its pattern and, when it has a guard, that guard as
    // well. The guard is read in the arm the pattern's test enters, so it sees the names the
    // pattern binds and is not evaluated at all for a value the pattern does not match
    // (design principles 4.2: `a && b` is `if a then b else false`).
    const auto conditionOf = [&](CaseStep &step) {
        const auto guardValue = [&] {
            node_handle_t guard = valueNodeOf(visitInPosition(step.guard, Type::Bool()));
            Type *guardType     = valueTypeOf(nodeTypeOf(guard));
            if (guardType == nullptr || guardType->code() != TypeCode::Bool) {
                diags_->of(SemanticDiag::ConditionNotBool)
                    .atOrigin(step.guard->origin())
                    .commit(guardType == nullptr ? "?" : guardType->toString());
                throw BuildAbortException();
            }
            return guard;
        };
        if (step.condition == nullptr) {
            // `case _ when c`: the case tests nothing itself, so the guard is its whole
            // condition. The names the pattern binds are taken out all the same.
            std::vector<arm_ptr_t> one(1);
            one[0] = buildArm(step.exec, [&] { step.bind(); return guardValue(); });
            return buildBranch(origin, always, &one);
        }
        std::vector<arm_ptr_t> held(2);
        held[0] = buildArm(step.exec, [&] { step.bind(); return guardValue(); });
        held[1] = buildArm(step.exec, [&] { return boolConstant(false); });
        return buildBranch(origin, step.condition, &held);
    };

    // `step(i)` is the value of the list from case i on. The cases after a case are the `else`
    // of its own step, and are built there: a guard that does not hold hands the value to them,
    // and they are reached no other way.
    std::function<node_handle_t(size_t)> step = [&](size_t i) -> node_handle_t {
        CaseStep &caseStep        = steps[i];
        node_handle_t condition   = caseStep.condition;
        if (caseStep.guard != nullptr) {
            condition = conditionOf(caseStep);
        }
        if (condition == nullptr) {
            // The case that ends the list: an unguarded `case _` (the checks of the form
            // refused a `match` that can reach a value none of its cases takes). Its arm is the
            // only one, and the runtime takes it: the branch has no case value.
            std::vector<arm_ptr_t> one(1);
            one[0] =
                buildArm(caseStep.exec, [&] { caseStep.bind(); return visitBodyNode(caseStep.exec); });
            return buildBranch(origin, always, &one);
        }
        std::vector<arm_ptr_t> two(2);
        two[0] = buildArm(caseStep.exec, [&] {
            caseStep.bind();
            return visitBodyNode(caseStep.exec);
        });
        two[1] = buildArm(caseStep.exec, [&] {
            if (i + 1 < steps.size()) {
                return step(i + 1);
            }
            // Nothing is written after this case: no value reaches here (the checks of the form
            // refused a `match` that can fall off its end), but the arm the test does not take
            // is still needed -- the branch offers the join one value of the arms' type. It is
            // the body of this case's own arm, as the arm an exhaustive `match` over an enum
            // adds for the value it cannot meet.
            caseStep.bind();
            return visitBodyNode(caseStep.exec);
        });
        return buildBranch(origin, condition, &two);
    };
    return step(0);
}

node_handle_t Builder::buildGuardedMatch(const GCT::node_ptr_t &gct, node_handle_t condNode) {
    // The value is read once, into a name every case is compared with: a case value is a
    // literal, and the comparison is the one `==` of that type makes, so `case 3` against an
    // `f32` compares it with 3.0 (design principles 5.1) exactly as the branch that dispatches
    // by case value does, and a case value of another type is the same report that branch gives.
    const std::string name = bindTemporary(condNode);
    Type *subjectType = valueTypeOf(nodeTypeOf(condNode));
    std::vector<CaseStep> steps;
    for (size_t i = 1; i < gct->size(); ++i) {
        const GCT::node_ptr_t &caseNode = gct->at(i);
        CaseStep caseStep;
        caseStep.guard = tree_->caseGuardOf(caseNode);
        caseStep.bind  = [] {};
        switch (caseNode->caseType()) {
        case GCT::CaseType::Value: {
            // The case value as its own node: the CASE keeps the one it was built with, and a
            // node has one parent.
            node_handle_t valueNode  = valueNodeOf(visit(caseNode->at(0)));
            Type *caseType           = valueTypeOf(nodeTypeOf(valueNode));
            GCT::node_ptr_t operand  = caseNode->at(0);
            bool converted           = false;
            if (const std::optional<literal::Value> pattern = literalValueOfExpr(caseNode->at(0));
                pattern.has_value() && subjectType != nullptr && subjectType->isPrimitive() &&
                !subjectType->assignableFrom(caseType) && literal::kindEnters(*pattern, subjectType)) {
                if (const auto data = pattern->toData(subjectType); data.has_value()) {
                    operand   = tree_->makeData(*data);
                    converted = true;
                }
            }
            if (!converted &&
                (subjectType == nullptr || !subjectType->assignableFrom(caseType))) {
                diags_->of(SemanticDiag::MatchCaseTypeMismatch)
                    .atOrigin(caseNode->at(0)->origin())
                    .commit(subjectType == nullptr ? std::string("?") : subjectType->toString(),
                            caseType->toString());
                throw BuildAbortException();
            }
            caseStep.condition =
                applyInternalOperator("__eq__", nullptr, {tree_->makeDRef(name), operand}, caseNode);
            caseStep.exec = caseNode->at(1);
            break;
        }
        case GCT::CaseType::Else:
            // `case _`, guarded or not: it tests nothing itself. A guarded one leaves the value
            // to the cases written after it when its guard does not hold.
            caseStep.exec = caseNode->at(0);
            break;
        default:
            ASSERT(false, "A guarded match case is a value or `_`.");
        }
        steps.push_back(std::move(caseStep));
    }
    return buildCaseChain(steps, gct);
}

node_handle_t Builder::buildBranch(
    const GCT::node_ptr_t &gct, node_handle_t condNode, const std::vector<arm_ptr_t> *arms,
    bool valueless) {
    if (arms == nullptr && hasGuardedCase(gct, 1)) {
        return buildGuardedMatch(gct, condNode);
    }
    node_handle_t joinNode = currGraph_->addJoinNode(nullptr, nullptr, 0);
    node_handle_t brchNode = currGraph_->addBrchNode(Type::Int64(), joinNode, 0);
    registerNodeOrigin(context_, brchNode, gct, "gir.brch");
    registerNodeOrigin(
        context_,
        joinNode,
        gct,
        "gir.join",
        {semanticPart(
            camel::source::SemanticRole::ValueProducer,
            camel::source::kInvalidOriginId,
            -1,
            "join")},
        {});

    Type *joinType = nullptr;
    // The arm being added is the one an `if` without `else` has in its place. With arms the
    // caller built, the caller says whether the branch is such an `if` (`valueless`): the
    // join then takes nothing from any of them.
    bool absentElse = valueless;
    bool ifArmSeen  = false;
    // Whether the branch is an `if` without `else`, known before its arms are added: its arms
    // give values nothing takes (rejectDiscardedValue in addArm).
    bool dropsArmValues = valueless;
    if (arms == nullptr && gct->size() >= 3) {
        const auto &first = gct->at(1);
        const auto &last  = gct->at(gct->size() - 1);
        dropsArmValues    = first->caseType() == GCT::CaseType::True &&
                            last->caseType() == GCT::CaseType::Else && last->size() == 1 &&
                            last->at(0)->size() == 0;
    }

    linkNodes(LinkType::Norm, condNode, brchNode);
    std::vector<arm_ptr_t> armList;

    // Makes `arm` the next arm of the branch.
    const auto addArm = [&](const arm_ptr_t &arm) {
        if (dropsArmValues) {
            // The arm of an `if` without `else` gives a value nothing takes.
            rejectDiscardedValue(arm.get());
        }
        bodyValueStatement_.erase(arm.get());
        armList.push_back(arm);
        Type *exitType = arm->exitType();

        if (absentElse) {
            // Design principles 3.7: an `if` without `else` has no value. Its arm may be of
            // any type and what the arm gives is dropped: the join takes nothing. The arm
            // written is evaluated as any arm is; the one not written is an empty arm.
            joinType = Type::Void();
            setNodeType(joinNode, joinType);
        } else if (joinType == nullptr) {
            joinType = exitType;
            setNodeType(joinNode, joinType);
        } else {
            // Arms must agree up to refinement: unify widens (for example two
            // tensors whose shapes differ in one extent) or fails. Failing that, one arm may
            // enter the form of the other (design principles 3.8): `if c then 1 else null`
            // is an `int?`, `if c then v else failed` a `T!`. The arm that is not of the
            // joined type gets the CAST at its exit once the type is known (below).
            Type *unified = joinTypes(joinType, exitType);
            if (unified != nullptr && unified != joinType) {
                joinType = unified;
                setNodeType(joinNode, joinType);
            }
            if (unified == nullptr) {
                // Two arrays whose element types do not unify have no common type either, and the
                // element type of a value is part of its type (design principles 5.1): the join
                // names the element type each arm gives, with the conversion that would make the
                // two one, rather than only reporting that the arms disagree.
                if (joinType->code() == TypeCode::Array && exitType->code() == TypeCode::Array) {
                    diags_->of(SemanticDiag::BranchElementTypeMismatch)
                        .atOrigin(gct->origin())
                        .commit(
                            tt::as_ptr<ArrayType>(joinType)->elemType()->toString(),
                            tt::as_ptr<ArrayType>(exitType)->elemType()->toString());
                    throw BuildAbortException();
                }
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .atOrigin(gct->origin())
                    .commit(
                        currGraph_->name() + ": " + currGraph_->funcType()->toString(),
                        joinType->toString(),
                        exitType->toString());
                throw BuildAbortException();
            }
        }
    };
    const size_t armEnd = arms != nullptr ? arms->size() + 1 : gct->size();
    for (size_t i = 1; i < armEnd; i++) {
        arm_ptr_t subGraph;
        if (arms != nullptr) {
            // Arms the caller built (the tagged forms): a boolean condition and two arms.
            subGraph = (*arms)[i - 1];
        } else {
            const auto &caseNode         = gct->at(i);
            GCT::node_ptr_t caseExecNode = nullptr;

            switch (caseNode->caseType()) {
            case GCT::CaseType::True: {
                ASSERT(caseNode->size() == 1, "True case should have only one child.");
                // An `if`: the condition is a bool (language reference 6.8.1: no truth-value
                // conversion). Anything else was compared bit for bit with `true`.
                Type *condType = valueTypeOf(nodeTypeOf(condNode));
                if (condType->code() != TypeCode::Bool) {
                    diags_->of(SemanticDiag::ConditionNotBool)
                        .atOrigin(gct->at(0)->origin())
                        .commit(condType->toString());
                    throw BuildAbortException();
                }
                caseExecNode = caseNode->at(0);
                ifArmSeen    = true;
                break;
            }
            case GCT::CaseType::Else:
                ASSERT(i == gct->size() - 1, "Else case should be the last case.");
                ASSERT(caseNode->size() == 1, "Else case should have only one child.");
                caseExecNode = caseNode->at(0);
                absentElse   = ifArmSeen && caseExecNode->size() == 0;
                break;
            case GCT::CaseType::Value: {
                ASSERT(caseNode->size() == 2, "Value case should have two children.");
                node_handle_t valueNode = valueNodeOf(visit(caseNode->at(0)));
                // A case is compared with the value matched, by the comparison of that type:
                // a case of another type was compared bit for bit (`case 1` on a string read
                // the integer as a string, and crashed).
                Type *subjectType = valueTypeOf(nodeTypeOf(condNode));
                // A pattern literal takes the type of the value it is matched against (design
                // principles 5.1): `match x { case 3 => ... }` with `x: f32` compares the f32
                // with 3.0, not with a pattern of another numeric type.
                if (const std::optional<literal::Value> pattern =
                        literalValueOfExpr(caseNode->at(0));
                    pattern.has_value() && subjectType != nullptr && subjectType->isPrimitive() &&
                    !subjectType->assignableFrom(valueTypeOf(nodeTypeOf(valueNode))) &&
                    literal::kindEnters(*pattern, subjectType)) {
                    valueNode = literalInType(*pattern, subjectType, caseNode->at(0), true);
                }
                Type *caseType = valueTypeOf(nodeTypeOf(valueNode));
                if (!subjectType->assignableFrom(caseType)) {
                    diags_->of(SemanticDiag::MatchCaseTypeMismatch)
                        .atOrigin(caseNode->at(0)->origin())
                        .commit(subjectType->toString(), caseType->toString());
                    throw BuildAbortException();
                }
                linkNodes(LinkType::With, valueNode, brchNode);
                caseExecNode = caseNode->at(1);
                break;
            }
            default:
                ASSERT(false, "Unknown case type in BRCH node.");
            }

            subGraph = buildArm(caseExecNode);
        }
        addArm(subGraph);
    }
    if (joinType != nullptr && !absentElse) {
        for (const arm_ptr_t &arm : armList) {
            Type *exitType = arm->exitType();
            if (exitType != nullptr && !joinType->assignableFrom(exitType) &&
                entersImplicitly(joinType, exitType)) {
                coerceArmExit(arm, joinType, gct);
            }
        }
    }
    // The JOIN takes what each arm gives; each arm is the region it was built in, which the
    // BRCH owns now that it is there.
    runtime::GraphDraft &draft = currGraph_->draft();
    for (size_t i = 0; i < armList.size(); ++i) {
        linkNodes(LinkType::With, armList[i]->output, joinNode);
        draft.setRegionOwner(armList[i]->region, nodeIdOf(brchNode), static_cast<uint32_t>(i));
        draft.setRegionResult(armList[i]->region, nodeIdOf(armList[i]->output));
    }
    const size_t armCount = armList.size();
    runtime::DraftBrchPayload payload{
        .join     = nodeIdOf(joinNode),
        .armCount = static_cast<runtime::gc_cnt_t>(armCount)};
    std::vector<std::byte> brchBytes(sizeof(runtime::DraftBrchPayload));
    std::memcpy(brchBytes.data(), &payload, sizeof(payload));
    std::vector<draft_node_ref_t> brchNormInputs(
        normInputsOf(brchNode).begin(),
        normInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchWithInputs(
        withInputsOf(brchNode).begin(),
        withInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchCtrlInputs(
        ctrlInputsOf(brchNode).begin(),
        ctrlInputsOf(brchNode).end());
    const auto *brchHeader   = nodeHeaderOf(brchNode);
    const auto brchDataIndex = brchHeader->dataIndex != 0
                                   ? brchHeader->dataIndex
                                   : currGraph_->addRuntimeSlot(Type::Int64());
    currGraph_->draft().rewriteNode(
        nodeIdOf(brchNode),
        runtime::DraftNodeInit{
            .dataIndex    = brchDataIndex,
            .dataType     = Type::Int64(),
            .runtimeFlags = brchHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Brch,
            .payload      = std::span<const std::byte>(brchBytes.data(), brchBytes.size()),
            .normInputs   = brchNormInputs,
            .withInputs   = brchWithInputs,
            .ctrlInputs   = brchCtrlInputs});
    runtime::GCJoinBody joinBody{
        .brch     = nodeIdOf(brchNode),
        .armCount = static_cast<runtime::gc_cnt_t>(armCount)};
    std::vector<draft_node_ref_t> joinNormInputs(
        normInputsOf(joinNode).begin(),
        normInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinWithInputs(
        withInputsOf(joinNode).begin(),
        withInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinCtrlInputs(
        ctrlInputsOf(joinNode).begin(),
        ctrlInputsOf(joinNode).end());
    const auto *joinHeader = nodeHeaderOf(joinNode);
    const auto joinDataIndex =
        joinHeader->dataIndex != 0 ? joinHeader->dataIndex : currGraph_->addRuntimeSlot(joinType);
    currGraph_->draft().rewriteNode(
        nodeIdOf(joinNode),
        runtime::DraftNodeInit{
            .dataIndex    = joinDataIndex,
            .dataType     = joinType,
            .runtimeFlags = joinHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Join,
            .payload      = std::as_bytes(std::span{&joinBody, 1}),
            .normInputs   = joinNormInputs,
            .withInputs   = joinWithInputs,
            .ctrlInputs   = joinCtrlInputs});

    linkNodes(LinkType::Norm, brchNode, joinNode);
    linkArmEntries(armList, brchNode);
    // The arms of this branch exclude each other: a stream consumed in one of them is not
    // consumed twice by a use in another (design principles 5.4).
    noteArmGroup(armList);

    // The branch is the user of the variables its arms used, and the writer of those they
    // wrote. It begins at BRCH and is complete at JOIN.
    std::vector<node_handle_t> capturedWrites;
    mergeArmVariables(armList, joinNode, capturedWrites);
    const auto originOf = [&](node_handle_t variable) {
        const auto origin = captureOriginOf_.find(variable);
        return origin != captureOriginOf_.end() ? origin->second : variable;
    };
    std::vector<DraftGraphBuilder *> armCallees;
    std::set<node_handle_t> directWrites;
    for (const arm_ptr_t &arm : armList) {
        armCallees.insert(armCallees.end(), arm->callees.begin(), arm->callees.end());
        for (node_handle_t variable : arm->directWrites) {
            directWrites.insert(originOf(variable));
        }
    }
    if (ArmBuild *outer = activeArm()) {
        // The arms of this branch are part of the arm it is in.
        outer->callees.insert(outer->callees.end(), armCallees.begin(), armCallees.end());
        for (const arm_ptr_t &arm : armList) {
            outer->directWrites.insert(arm->directWrites.begin(), arm->directWrites.end());
        }
    }
    if (!capturedWrites.empty() && !synced_) {
        std::vector<node_handle_t> origins;
        for (node_handle_t variable : capturedWrites) {
            origins.push_back(originOf(variable));
        }
        unit_->pendingIgnoredWrites.push_back(
            {armCallees, origins, directWrites, gct->origin(), "The branch", "an arm writes"});
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, brchNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, brchNode);
        }
        // A branch is one statement of the block and the join is where that statement's chain goes
        // on: the statements written after the branch are written directly after it, and the arm
        // regions hang on the branch itself (docs/design/effects/edges.cn.md 1.5, 5.3, 5.7). The
        // statement written directly before the branch is the chain's tail there, so the join follows
        // it -- the shape a loop has, where the node that stands for the statement follows the
        // statement written before it. Without the link nothing orders the statements after a branch
        // after the ones before it once the control edges go, and the layout reads it this way
        // already: it lays the statement written before a branch out before the arms
        // (src/execute/graph_runtime_support.cpp). A sync edge from the branch to its join instead is
        // not this link: the layout reads the join's sync predecessor as the statement written
        // before the branch, and every `??` of test/cases/consistency/entering/fallback_chain.cml
        // then takes the arm that branch did not.
        if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, joinNode)) {
            markSyncStatement(lastSyncedNode_);
            markSyncStatement(joinNode);
            linkNodes(LinkType::Sync, lastSyncedNode_, joinNode);
        }
        lastSyncedNode_ = joinNode;
        chainedNodes_.insert(brchNode);
        chainedNodes_.insert(joinNode);
    }

    return joinNode;
}

void Builder::linkArmEntries(const std::vector<arm_ptr_t> &arms, node_handle_t brchNode) {
    // The nodes of an arm that read nothing the arm computes wait for the BRCH: the arm runs
    // only when it is entered.
    runtime::GraphDraft &draft = currGraph_->draft();
    const draft_node_ref_t end = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
    for (const arm_ptr_t &arm : arms) {
        const auto inArm = [&](draft_node_ref_t id) {
            return draft.alive(id) && draft.regionWithin(draft.nodeRegion(id), arm->region);
        };
        for (draft_node_ref_t id = arm->firstNode; id < end; ++id) {
            if (!draft.alive(id) || draft.nodeRegion(id) != arm->region) {
                continue;
            }
            switch (draft.header(id)->kind) {
            case runtime::GCNodeKind::Data:
            case runtime::GCNodeKind::Port:
            case runtime::GCNodeKind::Dref:
                continue;
            default:
                break;
            }
            bool computed = false;
            for (auto inputs :
                 {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
                for (draft_node_ref_t input : inputs) {
                    computed = computed || inArm(input);
                }
            }
            node_handle_t node = draft.ref(id);
            if (!computed && linkCheek(brchNode, node)) {
                linkNodes(LinkType::Ctrl, brchNode, node);
            }
        }
    }
}

void Builder::mergeArmVariables(
    const std::vector<arm_ptr_t> &arms, node_handle_t joinNode,
    std::vector<node_handle_t> &written) {
    std::vector<node_handle_t> order;
    std::unordered_map<node_handle_t, bool> wrote;
    for (const arm_ptr_t &arm : arms) {
        for (node_handle_t variable : arm->touchedOrder) {
            const ArmBuild::Touched &touched = arm->touched.at(variable);
            auto known                       = wrote.find(variable);
            if (known == wrote.end()) {
                known = wrote.emplace(variable, false).first;
                order.push_back(variable);
            }
            if (touched.writerAfter.has_value()) {
                known->second = true;
                if (linkCheek(*touched.writerAfter, joinNode)) {
                    linkNodes(LinkType::Ctrl, *touched.writerAfter, joinNode);
                }
            }
        }
    }
    for (node_handle_t variable : order) {
        if (wrote[variable]) {
            written.push_back(variable);
            recordWrite(variable, joinNode, true);
        } else {
            pendingUsesOf(variable).push_back({joinNode, false});
        }
    }
}

node_handle_t Builder::intervalSequenceValue(
    node_handle_t from, node_handle_t to, const GCT::node_ptr_t &expr) {
    // The interval in the sequence position of a loop is the pair of its two bounds, not the
    // array of its elements (design principles 4.2): the pair stands where any other sequence stands — Norm
    // input 0 of the LOOP node, one value — and a back end takes the element of the round from
    // the bounds and the round number, so nothing of the interval's size is ever made. The
    // interval is the `int[]` of `intervalValue` in every other position.
    std::vector<Type *> bounds{Type::Int64(), Type::Int64()};
    TupleType *boundsType       = TupleType::create(bounds);
    const std::vector<size_t> slots{0, 1};
    runtime::GraphDraft &draft = currGraph_->draft();
    node_handle_t result        = currGraph_->addFillNode(
        boundsType, runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
    const slot_t empty =
        camel::core::rtdata::toSlot(::Tuple::create(2, camel::core::mm::autoSpace()));
    node_handle_t source = draft.ref(draft.materializeStaticValue(empty, boundsType));
    linkNodes(LinkType::Norm, source, result);
    linkNodes(LinkType::With, from, result);
    linkNodes(LinkType::With, to, result);
    registerNodeOrigin(context_, result, expr, "gir.loop.interval");
    return result;
}

node_handle_t Builder::visitLoopNode(const GCT::node_ptr_t &gct) {
    ENTER("LOOP");
    using camel::core::LoopKind;
    const LoopKind kind    = gct->loopKind();
    const size_t k         = gct->loopStateCount();
    const bool hasIterable = camel::core::loopHasIterable(kind);

    // The names of the head are declared once, together (design principles 3.10).
    {
        std::unordered_set<std::string> names;
        for (size_t j = 0; j < k + (hasIterable ? 1 : 0); ++j) {
            const std::string &name = j < k ? gct->loopStateName(j) : gct->loopElementName();
            if (!names.insert(name).second) {
                diags_->of(SemanticDiag::LoopNameRepeated).atOrigin(gct->origin()).commit(name);
                throw BuildAbortException();
            }
        }
    }

    // Whether the first value written at `expr` is a numeric literal the position `type` holds.
    // Such a literal takes that type while it is made (visitInPosition, literalDataInType), as it
    // does in the arm of an `if`. A value that is not a literal is never given this type: the
    // loop's value is the state's type, and the position's own check reports the mismatch.
    //
    // A composite literal is not one of these, and neither is the empty array literal. A
    // composite has a shape of its own, and the loop's state keeps it: the position then converts
    // the loop's value (`loop s = {}` under `Nothing` keeps the state `{  }` and a CAST names it;
    // cases/loops/zero_leaf_state.cml). The empty array literal has no element type at all, and
    // takes the position's element type below, once the body is built and the elements it appends
    // are known (inferLoopStateFromAppend).
    const auto literalEnters = [&](const GCT::node_ptr_t &expr, Type *type) -> bool {
        if (expr == nullptr || type == nullptr || asTagged(type) != nullptr) {
            return false;
        }
        const std::optional<literal::Value> exact = literalValueOfExpr(expr);
        return exact.has_value() && literal::kindEnters(*exact, type);
    };

    // The first values and the array: evaluated once, where the loop is (they do not see the
    // names of the head). The loop's value is the state's type -- the tuple of the state types
    // when there are several -- so each first value stands at the loop's own position, and a
    // numeric literal written there takes the position's type (design principles 5.1): `let n:
    // f32 = loop s = 0.0 while s < 3.0 do s + 1.0` is a loop of `f32`, and the body's and the
    // condition's literals follow from the state's type. A state written as the empty literal is
    // the other way round: it takes the element type of the position the loop's value goes to
    // (`loop losses = [] for ...` where the loop gives a `float[]`; design principles 5.1, 5.2).
    // A state whose first value is anything else keeps its own type, whatever the position says.
    std::vector<Type *> stateExpected(k, nullptr);
    std::vector<bool> literalAtPosition(k, false);
    if (expectedValueType_ != nullptr) {
        Type *position = valueTypeOf(expectedValueType_);
        for (size_t j = 0; j < k; ++j) {
            Type *state = position;
            if (k > 1) {
                // The loop's value is a tuple of the state types: one state's position is one
                // element of it, and a position that is not that tuple says nothing per state.
                if (position == nullptr || position->code() != TypeCode::Tuple ||
                    tt::as_ptr<TupleType>(position)->size() != k) {
                    continue;
                }
                state = tt::as_ptr<TupleType>(position)->typeAt(j);
            }
            if (state == nullptr) {
                continue;
            }
            if (k == 1 && state->code() == TypeCode::Array) {
                stateExpected[j] = state;
                continue;
            }
            if (literalEnters(gct->loopInitial(j), state)) {
                stateExpected[j] = state;
                literalAtPosition[j] = true;
            }
        }
    }
    std::vector<node_handle_t> initial;
    std::vector<Type *> stateTypes;
    std::vector<bool> emptyInitial(k, false);
    bool deferredEmpty = false;
    for (size_t j = 0; j < k; ++j) {
        const auto &expr      = gct->loopInitial(j);
        Type *loopExpected    = stateExpected[j];
        node_handle_t node;
        {
            ExpectedValueScope stateScope(expectedValueType_, loopExpected);
            // A literal in the position is made in the position's type; the empty literal is
            // left as it is and takes that type below.
            node_handle_t built{};
            if (literalAtPosition[j]) {
                built = visitInPosition(expr, loopExpected);
            } else {
                built = valueNodeOf(visit(expr));
            }
            node = valueOf(functionValueOf(built, expr));
        }
        if (loopExpected != nullptr && !literalAtPosition[j]) {
            // A value that is not the empty literal is converted only when its position
            // accepts it; one that does not fit stays as it is and is reported below.
            if (node_handle_t typed = checkValue(node, loopExpected, expr); typed != nullptr) {
                node = typed;
            }
        }
        Type *type         = valueTypeOf(nodeTypeOf(node));
        const bool untyped = type == nullptr || type->code() == TypeCode::Void ||
                             isUntypedArrayType(type);
        if (untyped) {
            // `loop losses = [] for ... do losses->append<l>`: the element type is the value
            // the body appends, and it is known once the body is built (design principles 5.1,
            // 5.2). One state gives the loop's value directly; with several, the loop's value
            // is a tuple and does not say what this state's element type is, so it must be
            // written.
            if (k == 1 && isEmptyArrayLiteral(node)) {
                deferredEmpty = true;
            } else {
                diags_->of(SemanticDiag::LoopStateWithoutType)
                    .atOrigin(expr->origin())
                    .commit(gct->loopStateName(j), type ? type->toString() : std::string("void"));
                throw BuildAbortException();
            }
        }
        emptyInitial[j] = isEmptyArrayLiteral(node);
        initial.push_back(node);
        stateTypes.push_back(type);
    }
    node_handle_t sequence = nullptr;
    Type *elementType      = nullptr;
    bool intervalIterates  = false;
    // The element type of a sequence a loop iterates: an array gives its own elements along its
    // first dimension, and a string and a map give theirs through the `__iter__` operator their
    // type declares (design principles 3.10, 11.1, 11.3): the code points of a string as
    // one-character strings, and the (key, value) pairs of a map in key order. The operator is
    // resolved like any other, by the type of the sequence; the loop then iterates the array it
    // gives, on every back end, with one driver.
    //
    // A `for` over an array of any rank is the core's, and no module takes part in it: `t[i]`
    // takes the i-th along the first dimension (design principles 5.4), so the round of a rank-1
    // array is its element and the round of a higher-rank one is the row one dimension shorter
    // (ArrayType::firstDimensionElement). The tensor module used to declare `__iter__` for a
    // numeric array (`tensor:iter`, sub-arrays along the first dimension), and asking it here
    // made the iterable position an OPER node: that is what hid a constant array's row count
    // from `core.loopDomain` (runtime/core_facts.cpp, constantLength). Design principles 5.4
    // states it once, in the core, for every array; the module's `iter` went with the P4 alias
    // (one thing, one spelling).
    //
    // `__iter__` reaches a compilation unit only if it imports the module that declares it, and
    // the standard library's own array code compiles without it.
    const auto iterableElementType =
        [&](node_handle_t &iterable, const GCT::node_ptr_t &expr) -> Type * {
        Type *type = valueTypeOf(nodeTypeOf(iterable));
        if (type == nullptr) {
            diags_->of(SemanticDiag::LoopSequenceNotIterable)
                .atOrigin(expr->origin())
                .commit(sourceTextOf(expr), std::string("void"));
            throw BuildAbortException();
        }
        if (type->code() != TypeCode::Array) {
            const std::vector<StaticArg> statics(1);
            std::optional<oper_idx_ptr_t> elements;
            // `__iter__` is a group of several overloads (`:struct/iter_str`,
            // `:coll/map_items`), one per iterable type: no one OperId names it, so it is still
            // looked up by the group's name.
            if (const auto group = importedEntity("__iter__");
                group.has_value() && group->isOperGroup()) {
                elements = group->operGroup()->resolve({}, {type}, statics, Modifier::None);
            }
            if (!elements.has_value()) {
                diags_->of(SemanticDiag::LoopSequenceNotIterable)
                    .atOrigin(expr->origin())
                    .commit(sourceTextOf(expr), type->toString());
                throw BuildAbortException();
            }
            node_handle_t array = currGraph_->addOperNode(*elements, 0);
            stampPermissions(array);
            registerNodeOrigin(context_, array, expr, "gir.loop.iter");
            linkNodes(LinkType::Norm, iterable, array);
            iterable = array;
            type     = valueTypeOf(nodeTypeOf(iterable));
            ASSERT(
                type != nullptr && type->code() == TypeCode::Array,
                "The elements of an iterable must be an array.");
        }
        return tt::as_ptr<ArrayType>(type)->firstDimensionElement();
    };
    // `for i in a..b` (design principles 4.2: "`for i in 0..n` is fused by the compiler, without
    // materialising an array"). The interval in the sequence position of a loop is the loop's own
    // form: its two bounds are the loop's iterable, and a round takes `from + round`
    // (camel::core::LoopIterableForm::Interval). In every other position `a..b` is still the
    // `int[]` the rule gives it, and the loop iterates that value like any other sequence.
    if (hasIterable && isIntervalExpr(gct->loopSequence())) {
        const auto &interval = gct->loopSequence();
        node_handle_t from   = argumentNodeOf(visit(interval->at(1)), interval->at(1));
        node_handle_t to     = argumentNodeOf(visit(interval->at(2)), interval->at(2));
        if (intervalBounds(from, to)) {
            sequence         = intervalSequenceValue(from, to, interval);
            elementType      = Type::Int64();
            intervalIterates = true;
        } else {
            // Two functions, or two operands `..` has no reading for: the composition (or the
            // refusal it gives everywhere else), built from the operands already in hand so that
            // neither of them is built twice.
            sequence = valueOf(
                functionValueOf(valueNodeOf(buildCompositionWith(interval, from, to)), interval));
            elementType = iterableElementType(sequence, interval);
        }
    } else if (hasIterable) {
        const auto &expr = gct->loopSequence();
        sequence         = valueOf(functionValueOf(valueNodeOf(visit(expr)), expr));
        Type *sequenceType = valueTypeOf(nodeTypeOf(sequence));
        // `for x in s` where `s` is a stream: the rounds are the pulls of the stream's step
        // function, and the graph already has the loop for that (the `while`, with a condition
        // and no iterable), so nothing about the driver or a back end is new.
        if (sequenceType != nullptr && sequenceType->code() == TypeCode::Stream && k > 0) {
            LEAVE("LOOP");
            return buildStreamLoop(gct, sequence, initial, stateTypes);
        }
        elementType      = iterableElementType(sequence, expr);
    }
    Type *stateType =
        k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));

    // The names of the state's components. They are the state's one name until splits a
    // struct state into its parts; then they are the paths of those parts (`s.inner.x`), which
    // is what the graph shows and the diagnostics name.
    std::vector<std::string> stateNames(k);
    for (size_t j = 0; j < k; ++j) {
        stateNames[j] = gct->loopStateName(j);
    }

    // The condition and the body are regions of the current graph, built where they are
    // written. Their ports (the element, the values of the state) are Port nodes of the graph
    // that are not its parameters; the captured values are read where they are.
    // The names and types the region declares for its ports, from the ports themselves.
    const auto refreshPorts = [&](ArmBuild &part, bool withElement) {
        FunctionTypeFactory params;
        std::vector<runtime::gc_node_ref_t> ids;
        for (size_t i = 0; i < part.ports.size(); ++i) {
            const size_t state = i - (withElement ? 1 : 0);
            const std::string &name =
                withElement && i == 0 ? gct->loopElementName() : stateNames[state];
            ids.push_back(nodeIdOf(part.ports[i]));
            params.addNormArg(name, nodeTypeOf(part.ports[i]), false);
        }
        params.setExitType(Type::Void());
        part.graph->draft().setRegionPorts(part.region, ids);
        part.graph->draft().setRegionParams(part.region, params.build());
    };
    const auto declarePorts = [&](ArmBuild &part, bool withElement, const GCT::node_ptr_t &exec) {
        const auto add = [&](const std::string &name, Type *type) {
            node_handle_t port = currGraph_->addRegionPortNode(type, name);
            // A port belongs to its region, as the nodes that read it do.
            part.graph->draft().setNodeRegion(nodeIdOf(port), part.region);
            insertNode(name, port);
            if (recorder_) {
                recordDeclaration(name, port, exec);
            }
            part.ports.push_back(port);
        };
        if (withElement) {
            add(gct->loopElementName(), elementType);
        }
        for (size_t j = 0; j < k; ++j) {
            add(stateNames[j], stateTypes[j]);
        }
        refreshPorts(part, withElement);
    };
    arm_ptr_t condPart;
    arm_ptr_t bodyPart;
    {
        ExpectedValueScope partScope(expectedValueType_, nullptr);
        if (camel::core::loopHasCondition(kind)) {
            condPart = buildArm(
                gct->loopCondition(),
                {},
                runtime::GCRegionKind::LoopCond,
                [&](ArmBuild &part) { declarePorts(part, false, gct->loopCondition()); });
            Type *condType = condPart->exitType();
            if (condType == nullptr || condType->code() != TypeCode::Bool) {
                diags_->of(SemanticDiag::LoopConditionNotBool)
                    .atOrigin(gct->loopCondition()->origin())
                    .commit(condType ? condType->toString() : std::string("void"));
                throw BuildAbortException();
            }
        }
        bodyPart =
            buildArm(gct->loopBody(), {}, runtime::GCRegionKind::LoopBody, [&](ArmBuild &part) {
                declarePorts(part, hasIterable, gct->loopBody());
            });
    }
    if (deferredEmpty) {
        node_handle_t next = loopStateValueOf(bodyPart->value);
        if (bodyPart->returned ||
            !inferLoopStateFromAppend(bodyPart, condPart, gct, initial, stateTypes, 0, next)) {
            diags_->of(SemanticDiag::LoopStateWithoutType)
                .atOrigin(gct->loopInitial(0)->origin())
                .commit(
                    gct->loopStateName(0),
                    stateTypes.front() ? stateTypes.front()->toString() : std::string("void"));
            throw BuildAbortException();
        }
        stateType = stateTypes.front();
        refreshPorts(*bodyPart, hasIterable);
        if (condPart) {
            refreshPorts(*condPart, false);
        }
    }
    // The body gives the next state: of the state's type, or a value that enters it (an
    // `int` where the state is an `int?`, design principles 3.8).
    Type *bodyType = bodyPart->exitType();
    if (bodyType == nullptr || !stateType->assignableFrom(bodyType)) {
        if (bodyType != nullptr && entersImplicitly(stateType, bodyType)) {
            coerceArmExit(bodyPart, stateType, gct->loopBody());
        } else {
            diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                .atOrigin(gct->loopBody()->origin())
                .commit(
                    bodyType ? bodyType->toString() : std::string("void"),
                    stateType->toString());
            throw BuildAbortException();
        }
    }

    // A struct's fields are the loop state's components. When the state is one struct (or
    // one tuple) and the whole value never escapes the loop's parts -- every use of the state,
    // in the body and in the condition, is a part access -- the parts of that value become the
    // state's components, as if the loop had declared one state per part. The body hands back a
    // tuple of their next values, and the struct is rebuilt once, after the loop.
    //
    // What this buys is the in-place guarantee of design principles 10.5 for an array field: the
    // field is a state of its own, so `s.xs->append<v>` grows the loop's own array each round
    // instead of copying the struct's array. A part that never changes is still handed on, and
    // nothing observable changes: an array part is copied once when the loop is entered, and the
    // struct a user sees is the one built at the end. A body that uses the struct as a whole (a
    // call, a branch, `return s`) is left alone.
    struct SplitPart {
        Type *type = nullptr;
        std::vector<size_t> steps;
        std::string name;
        bool split = false;
        std::vector<node_handle_t> bodyNodes;
        std::vector<node_handle_t> condNodes;
        std::vector<SplitPart> children;
        size_t component = 0;
    };
    SplitPart splitRoot;
    std::vector<SplitPart *> splitComponents;
    bool splitApplied    = false;
    Type *splitValueType = nullptr;
    std::vector<bool> privateInitial;

    // A part access (`s.x`, `t.0`) of the value a node holds; added to the insert region.
    const auto addPartAccess = [&](Type *type, node_handle_t value, size_t step) -> node_handle_t {
        Type *partType = aggregatePartType(type, step);
        node_handle_t access =
            type->code() == TypeCode::Struct
                ? currGraph_->addAccsNode(partType, aggregatePartName(type, step))
                : currGraph_->addAccsNode(partType, static_cast<uint32_t>(step));
        linkNodes(LinkType::Norm, value, access);
        registerNodeOrigin(context_, access, gct, "gir.loop.split");
        return access;
    };

    const auto splitStructState = [&]() -> bool {
        if (k != 1 || stateTypes.size() != 1 || bodyPart->returned || bodyPart->value == nullptr ||
            bodyPart->output == nullptr || !isSplittableAggregate(stateType)) {
            return false;
        }
        runtime::GraphDraft &draft = currGraph_->draft();
        const size_t statePortAt   = camel::core::loopHasIterable(kind) ? 1 : 0;
        if (bodyPart->ports.size() != statePortAt + 1) {
            return false;
        }
        if (condPart != nullptr && condPart->ports.size() != 1) {
            return false;
        }
        const node_handle_t bodyStatePort = bodyPart->ports[statePortAt];
        const node_handle_t condStatePort = condPart != nullptr ? condPart->ports.front() : nullptr;

        std::function<bool(
            SplitPart &,
            const std::vector<node_handle_t> &,
            const std::vector<node_handle_t> &)>
            buildPart;
        buildPart = [&](SplitPart &part,
                        const std::vector<node_handle_t> &bodyNodes,
                        const std::vector<node_handle_t> &condNodes) -> bool {
            part.bodyNodes = bodyNodes;
            part.condNodes = condNodes;
            if (!isSplittableAggregate(part.type) || (bodyNodes.empty() && condNodes.empty())) {
                return true; // a component, or a part nothing reads: kept whole
            }
            // Every use has to be a part access. One use of the whole value (as a call's
            // argument, a branch's arm, another struct) makes the parts inseparable, so this
            // part stays whole and the split stops here.
            std::map<size_t, std::vector<node_handle_t>> bodyUses;
            std::map<size_t, std::vector<node_handle_t>> condUses;
            std::vector<node_handle_t> all = bodyNodes;
            all.insert(all.end(), condNodes.begin(), condNodes.end());
            for (node_handle_t source : all) {
                for (const auto users : {normUsersOf(source), withUsersOf(source)}) {
                    for (draft_node_ref_t userRef : users) {
                        node_handle_t user = nodeDraftOf(source)->ref(userRef);
                        if (user == nullptr) {
                            return true;
                        }
                        const auto index = accessedPartOf(user, part.type);
                        const auto norms = normInputsOf(user);
                        if (!index.has_value() || norms.size() != 1 ||
                            nodeDraftOf(user)->ref(norms.front()) != source) {
                            return true;
                        }
                        const uint32_t region = draft.nodeRegion(nodeIdOf(user));
                        std::map<size_t, std::vector<node_handle_t>> *uses = nullptr;
                        if (draft.regionWithin(region, bodyPart->region)) {
                            uses = &bodyUses;
                        } else if (
                            condPart != nullptr && draft.regionWithin(region, condPart->region)) {
                            uses = &condUses;
                        } else {
                            return true;
                        }
                        auto &list = (*uses)[*index];
                        if (std::find(list.begin(), list.end(), user) == list.end()) {
                            list.push_back(user);
                        }
                    }
                }
            }
            part.split         = true;
            const size_t count = aggregatePartCount(part.type);
            for (size_t i = 0; i < count; ++i) {
                SplitPart child;
                child.type  = aggregatePartType(part.type, i);
                child.steps = part.steps;
                child.steps.push_back(i);
                const std::string stepName = aggregatePartName(part.type, i);
                child.name        = part.name.empty() ? stepName : part.name + "." + stepName;
                const auto bodyIt = bodyUses.find(i);
                const auto condIt = condUses.find(i);
                const std::vector<node_handle_t> none;
                if (!buildPart(
                        child,
                        bodyIt == bodyUses.end() ? none : bodyIt->second,
                        condIt == condUses.end() ? none : condIt->second)) {
                    return false;
                }
                part.children.push_back(std::move(child));
            }
            return true;
        };

        const Type *rootStateType = stateType;
        splitRoot.type            = stateType;
        splitRoot.name            = stateNames.front();
        splitRoot.steps.clear();
        splitRoot.children.clear();
        splitRoot.split = false;
        const std::vector<node_handle_t> noCond;
        const std::vector<node_handle_t> condRoot =
            condStatePort != nullptr ? std::vector<node_handle_t>{condStatePort} : noCond;
        if (!buildPart(splitRoot, {bodyStatePort}, condRoot) || !splitRoot.split) {
            return false;
        }
        splitComponents.clear();
        std::function<void(SplitPart &)> collectParts = [&](SplitPart &part) {
            if (!part.split) {
                part.component = splitComponents.size();
                splitComponents.push_back(&part);
                return;
            }
            for (SplitPart &child : part.children) {
                collectParts(child);
            }
        };
        collectParts(splitRoot);
        if (splitComponents.empty()) {
            return false;
        }
        std::vector<Type *> leafTypes;
        for (SplitPart *component : splitComponents) {
            leafTypes.push_back(component->type);
        }
        Type *valueType = leafTypes.size() == 1 ? leafTypes.front() : TupleType::create(leafTypes);

        const uint32_t outerRegion = draft.insertRegion();

        // The body's ports: the element, then one port per component, in the same order as the
        // states. The old state port goes; nothing but the accesses of its parts read it.
        draft.setInsertRegion(bodyPart->region);
        std::vector<node_handle_t> bodyPorts(bodyPart->ports.begin(), bodyPart->ports.end());
        bodyPorts.erase(bodyPorts.begin() + static_cast<std::ptrdiff_t>(statePortAt));
        std::vector<node_handle_t> bodyComponentPorts;
        for (SplitPart *component : splitComponents) {
            node_handle_t port = currGraph_->addRegionPortNode(component->type, component->name);
            draft.setNodeRegion(nodeIdOf(port), bodyPart->region);
            bodyComponentPorts.push_back(port);
            bodyPorts.push_back(port);
        }
        bodyPart->ports = bodyPorts;
        std::vector<node_handle_t> condComponentPorts;
        if (condPart != nullptr) {
            std::vector<node_handle_t> condPorts(condPart->ports.begin(), condPart->ports.end());
            condPorts.erase(condPorts.begin());
            for (SplitPart *component : splitComponents) {
                node_handle_t port =
                    currGraph_->addRegionPortNode(component->type, component->name);
                draft.setNodeRegion(nodeIdOf(port), condPart->region);
                condComponentPorts.push_back(port);
                condPorts.push_back(port);
            }
            condPart->ports = condPorts;
        }

        // The next value of each component, worked out from the body's next struct: a part of a
        // struct FILL is the value written there (so an `append` stays the next state itself,
        // and can grow in place); a part the FILL does not write, or a struct given by another
        // value (a call, a branch), is read from it.
        std::vector<node_handle_t> nextValues(splitComponents.size(), nullptr);
        std::function<void(const SplitPart &, node_handle_t, bool)> assignNext;
        assignNext = [&](const SplitPart &part, node_handle_t value, bool unchanged) {
            if (!part.split) {
                nextValues[part.component] = unchanged ? nullptr : value;
                return;
            }
            bool same = unchanged || value == nullptr;
            for (node_handle_t node : part.bodyNodes) {
                if (node == value) {
                    same = true;
                    break;
                }
            }
            for (const SplitPart &child : part.children) {
                const size_t step        = child.steps.back();
                node_handle_t childValue = nullptr;
                if (!same) {
                    childValue = projectPart(value, part.type, step);
                    if (childValue == nullptr) {
                        childValue = addPartAccess(part.type, value, step);
                    }
                }
                assignNext(child, childValue, same);
            }
        };
        assignNext(splitRoot, bodyPart->value, false);

        std::vector<node_handle_t> resolved;
        for (size_t i = 0; i < splitComponents.size(); ++i) {
            resolved.push_back(nextValues[i] != nullptr ? nextValues[i] : bodyComponentPorts[i]);
        }
        node_handle_t result = nullptr;
        if (splitComponents.size() == 1) {
            result = resolved.front();
        } else {
            const size_t count = resolved.size();
            std::vector<size_t> slots(count);
            std::iota(slots.begin(), slots.end(), size_t{0});
            result = currGraph_->addFillNode(
                valueType,
                runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
            const slot_t empty =
                camel::core::rtdata::toSlot(::Tuple::create(count, camel::core::mm::autoSpace()));
            node_handle_t source = draft.ref(draft.materializeStaticValue(empty, valueType));
            linkNodes(LinkType::Norm, source, result);
            for (node_handle_t value : resolved) {
                linkNodes(LinkType::With, value, result);
            }
            registerNodeOrigin(context_, result, gct->loopBody(), "gir.loop.split");
        }
        if (bodyPart->output != bodyPart->value) {
            // A `sync` body's effects are ordered by the gate around the old value; the new
            // state comes after them.
            linkNodes(LinkType::Ctrl, bodyPart->output, result);
        }
        bodyPart->output = result;
        bodyPart->value  = result;
        bodyPart->type   = valueType;
        draft.setRegionResult(bodyPart->region, nodeIdOf(result));

        // The loop starts from the parts of its first value. The accesses are made outside the
        // loop (the insert region is the one around it).
        draft.setInsertRegion(outerRegion);
        std::vector<node_handle_t> projections;
        projections.reserve(splitComponents.size());
        for (SplitPart *component : splitComponents) {
            node_handle_t value = initial.front();
            Type *current       = const_cast<Type *>(rootStateType);
            for (size_t step : component->steps) {
                value   = addPartAccess(current, value, step);
                current = aggregatePartType(current, step);
            }
            projections.push_back(value);
        }

        // The reads of the old state are now reads of its parts. This is done after the next
        // values are written: a next value that is a read of the state (`n: s.n`) becomes the
        // part itself, which is the value it had.
        for (size_t i = 0; i < splitComponents.size(); ++i) {
            for (node_handle_t node : splitComponents[i]->bodyNodes) {
                draft.replaceAllValueUses(nodeIdOf(node), nodeIdOf(bodyComponentPorts[i]));
            }
            for (node_handle_t node : splitComponents[i]->condNodes) {
                draft.replaceAllValueUses(nodeIdOf(node), nodeIdOf(condComponentPorts[i]));
            }
        }

        stateTypes = std::move(leafTypes);
        stateNames.clear();
        for (SplitPart *component : splitComponents) {
            stateNames.push_back(component->name);
        }
        initial = std::move(projections);
        privateInitial.assign(splitComponents.size(), false);
        for (size_t i = 0; i < splitComponents.size(); ++i) {
            privateInitial[i] = splitComponents[i]->type != nullptr &&
                                splitComponents[i]->type->code() == TypeCode::Array;
        }
        stateType      = valueType;
        splitValueType = valueType;
        splitApplied   = true;
        refreshPorts(*bodyPart, hasIterable);
        if (condPart != nullptr) {
            refreshPorts(*condPart, false);
        }
        return true;
    };
    splitStructState();
    if (splitApplied) {
        // The empty-array collection is a slot beside one state; a struct state's parts are not
        // empty literals, so there is nothing to collect once it is split.
        emptyInitial.assign(stateTypes.size(), false);
    }

    markLoopStateInPlace(bodyPart, gct, initial, privateInitial);
    // An empty-array state whose only use is `append` also becomes a collection slot: the loop
    // then hands out the appended values as one array, built by the driver in one pass, beside
    // the state the program sees (design principles 3.10; the tape autodiff and the exporter
    // read). The state stays the program's value and its gradient channel; the slot is
    // internal, and the FILL below hides it from the loop's users.
    const std::vector<Type *> collected =
        collectLoopAppends(bodyPart, gct, stateTypes, emptyInitial);
    Type *loopValueType = splitApplied ? splitValueType : stateType;
    if (!collected.empty()) {
        std::vector<Type *> components = stateTypes;
        for (Type *element : collected) {
            components.push_back(ArrayType::create(element));
        }
        loopValueType = TupleType::create(components);
    }

    node_handle_t loopNode = currGraph_->addRegionLoopNode(kind, loopValueType);
    if (intervalIterates) {
        // The sequence the loop iterates is the pair of the interval's bounds, not an array
        // (camel::core::LoopIterableForm::Interval): a back end takes the element of a round from
        // the bounds and the round number, so nothing the size of the interval is ever made.
        currGraph_->draft().setLoopIterableInterval(nodeIdOf(loopNode));
    }
    if (!collected.empty()) {
        currGraph_->draft().setLoopCollectionCount(
            nodeIdOf(loopNode),
            static_cast<uint32_t>(collected.size()));
    }
    registerNodeOrigin(context_, loopNode, gct, "gir.loop");
    stampPermissions(loopNode);
    if (sequence != nullptr) {
        linkNodes(LinkType::Norm, sequence, loopNode);
    }
    for (node_handle_t node : initial) {
        linkNodes(LinkType::Norm, node, loopNode);
    }
    // The With inputs of the node are the results of its parts (the body's, then the
    // condition's) and the slot the rounds are counted in; the parts are its regions.
    std::vector<arm_ptr_t> parts{bodyPart};
    linkNodes(LinkType::With, bodyPart->output, loopNode);
    if (condPart) {
        parts.push_back(condPart);
        linkNodes(LinkType::With, condPart->output, loopNode);
    }
    linkNodes(LinkType::With, currGraph_->addRegionPortNode(Type::Int64(), ""), loopNode);
    runtime::GraphDraft &draft = currGraph_->draft();
    draft.setRegionOwner(bodyPart->region, nodeIdOf(loopNode), 1);
    draft.setRegionResult(bodyPart->region, nodeIdOf(bodyPart->output));
    if (condPart) {
        draft.setRegionOwner(condPart->region, nodeIdOf(loopNode), 0);
        draft.setRegionResult(condPart->region, nodeIdOf(condPart->output));
    }

    // Running the loop is like entering an arm: it uses every variable its parts used and
    // writes those they wrote.
    std::vector<node_handle_t> capturedWrites;
    mergeArmVariables(parts, loopNode, capturedWrites);
    const auto originOf = [&](node_handle_t variable) {
        const auto origin = captureOriginOf_.find(variable);
        return origin != captureOriginOf_.end() ? origin->second : variable;
    };
    std::vector<DraftGraphBuilder *> callees;
    std::set<node_handle_t> directWrites;
    for (const arm_ptr_t &part : parts) {
        callees.insert(callees.end(), part->callees.begin(), part->callees.end());
        for (node_handle_t variable : part->directWrites) {
            directWrites.insert(originOf(variable));
        }
    }
    if (ArmBuild *outer = activeArm()) {
        // The parts of this loop are part of the arm it is in.
        outer->callees.insert(outer->callees.end(), callees.begin(), callees.end());
        for (const arm_ptr_t &part : parts) {
            outer->directWrites.insert(part->directWrites.begin(), part->directWrites.end());
        }
    }
    if (!capturedWrites.empty() && !synced_) {
        std::vector<node_handle_t> origins;
        for (node_handle_t variable : capturedWrites) {
            origins.push_back(originOf(variable));
        }
        unit_->pendingIgnoredWrites.push_back(
            {callees, origins, directWrites, gct->origin(), "The loop", "its body writes"});
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, loopNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, loopNode);
        }
        lastSyncedNode_ = loopNode;
        chainedNodes_.insert(loopNode);
    }
    // The program sees the states, not the collection slots beside them. One state is its own
    // value; several are a tuple, whose FILL reads the state components and leaves the
    // collections to autodiff and the exporter.
    node_handle_t loopValue = loopNode;
    if (!collected.empty()) {
        if (stateTypes.size() == 1) {
            loopValue = currGraph_->addAccsNode(stateType, 0);
            linkNodes(LinkType::Norm, loopNode, loopValue);
            registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
        } else {
            std::vector<size_t> slots(stateTypes.size());
            std::iota(slots.begin(), slots.end(), size_t{0});
            loopValue = currGraph_->addFillNode(
                stateType,
                runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
            const slot_t empty = camel::core::rtdata::toSlot(
                ::Tuple::create(stateTypes.size(), camel::core::mm::autoSpace()));
            node_handle_t source = currGraph_->draft().ref(
                currGraph_->draft().materializeStaticValue(empty, stateType));
            linkNodes(LinkType::Norm, source, loopValue);
            for (size_t j = 0; j < stateTypes.size(); ++j) {
                node_handle_t component =
                    currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j));
                linkNodes(LinkType::Norm, loopNode, component);
                linkNodes(LinkType::With, component, loopValue);
            }
            registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
        }
    }
    if (splitApplied) {
        // The programme sees the struct, not its parts: it is rebuilt from the loop's value,
        // once, in the layout of the type the state had (nested structs included). Nothing of
        // the loop's own arrays is copied here; the parts are read out of the loop's value.
        std::vector<node_handle_t> componentValues(splitComponents.size(), nullptr);
        for (size_t j = 0; j < splitComponents.size(); ++j) {
            if (splitComponents.size() == 1) {
                componentValues[j] = loopValue;
                continue;
            }
            node_handle_t access =
                currGraph_->addAccsNode(splitComponents[j]->type, static_cast<uint32_t>(j));
            linkNodes(LinkType::Norm, loopValue, access);
            registerNodeOrigin(context_, access, gct, "gir.loop.value");
            componentValues[j] = access;
        }
        std::function<node_handle_t(const SplitPart &)> rebuildPart =
            [&](const SplitPart &part) -> node_handle_t {
            if (!part.split) {
                return componentValues[part.component];
            }
            std::vector<node_handle_t> values;
            for (const SplitPart &child : part.children) {
                values.push_back(rebuildPart(child));
            }
            const size_t count = values.size();
            std::vector<size_t> slots(count);
            std::iota(slots.begin(), slots.end(), size_t{0});
            const bool isStruct = part.type->code() == TypeCode::Struct;
            node_handle_t fill  = currGraph_->addFillNode(
                part.type,
                runtime::makeFillPayload(
                    isStruct ? runtime::GCFillKind::Struct : runtime::GCFillKind::Tuple,
                    slots));
            const slot_t empty   = isStruct
                                       ? camel::core::rtdata::toSlot(
                                             ::Struct::create(count, camel::core::mm::autoSpace()))
                                       : camel::core::rtdata::toSlot(
                                             ::Tuple::create(count, camel::core::mm::autoSpace()));
            node_handle_t source = currGraph_->draft().ref(
                currGraph_->draft().materializeStaticValue(empty, part.type));
            linkNodes(LinkType::Norm, source, fill);
            for (node_handle_t value : values) {
                linkNodes(LinkType::With, value, fill);
            }
            registerNodeOrigin(context_, fill, gct, "gir.loop.split");
            return fill;
        };
        loopValue = rebuildPart(splitRoot);
    }
    LEAVE("LOOP");
    return loopValue;
}

// `for x in s` where `s` is a stream. The stream's value is its step function
// (StreamType::stepType), so a round is one pull of it, and the graph already has the loop that
// takes one step per round: the `while`, whose condition and body are regions and whose state is
// what the next round starts from. The loop is therefore written here as that `while`, with the
// state the program wrote carried beside the pull:
//
//   state (next, acc); next is the last pull, acc the program's state
//   entering:   (s(), a0)
//   condition:  next != null, and the condition the head wrote after it
//   body:       the element is next.0, the program's body gives acc', and the round ends by
//               pulling next.1() into the next next
//
// `next` is the last pull -- the element with the rest of the stream, or `null` -- and the body
// reads the element out of it and pulls the next one from the rest at its end, after the
// statements it wrote ran. `for ... while c` folds `c` into the condition, after the test that
// the pull has a value: the stream ending is the loop ending, and `c` is asked only while there
// is something to iterate. Nothing here is a new node kind, a new region kind or a new loop
// form: a back end sees the LOOP node it always did, and the driver the same.
node_handle_t Builder::buildStreamLoop(
    const GCT::node_ptr_t &gct, node_handle_t sequence,
    const std::vector<node_handle_t> &initial, const std::vector<Type *> &stateTypes) {
    using camel::core::LoopKind;
    using camel::core::oper::TagIsNull;
    using camel::core::oper::TagNullValue;
    const LoopKind kind    = gct->loopKind();
    const size_t k         = initial.size();
    StreamType *stream     = tt::as_ptr<StreamType>(valueTypeOf(nodeTypeOf(sequence)));
    FunctionType *stepType = stream->stepType();
    Type *pullType         = stepType->exitType();
    Type *elementType      = stream->elemType();
    // A `for` over a stream is one consumer of it (design principles 5.4: a stream is read once on
    // each execution path).
    consumeStream(sequence, gct);

    // The names of the machine's parts. They are no names a program writes -- the names of the
    // head are the states and the element -- and the parts read the round through them.
    const std::string streamName = "$stream";
    const std::string doneName   = "$done";
    std::vector<std::string> stateNames(k);
    for (size_t j = 0; j < k; ++j) {
        stateNames[j] = gct->loopStateName(j);
    }

    // The state of the loop: the stream this round pulls from, whether that stream is over, then
    // the state the program wrote. The pull itself stands inside the round, after the condition:
    // every round judges the condition first and takes its element afterwards (design principles
    // 3.10), and a pull may have effects, so the round a condition refuses reads nothing.
    std::vector<Type *> machineTypes{stream, Type::Bool()};
    machineTypes.insert(machineTypes.end(), stateTypes.begin(), stateTypes.end());
    Type *machineType = static_cast<Type *>(TupleType::create(machineTypes));
    Type *stateType =
        k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));

    // One pull, where the round stands. The value of `T..` is that function value, so the call
    // reads the step signature and nothing is converted.
    const auto pullOf = [&](node_handle_t value, const GCT::node_ptr_t &at) {
        node_handle_t call = currGraph_->addCallNode(pullType);
        linkNodes(LinkType::With, value, call);
        registerNodeOrigin(context_, call, at, "gir.loop.pull");
        return call;
    };
    // A value on its way into the loop's state: one of the state's type as it is, a value that
    // enters it (an `int` where the state is an `int?`, design principles 3.8) converted, and
    // anything else the body's mismatch.
    const auto intoState = [&](node_handle_t value, Type *type) {
        if (value == nullptr || type == nullptr) {
            return value;
        }
        Type *valueType = valueTypeOf(nodeTypeOf(value));
        if (valueType != nullptr && type->assignableFrom(valueType)) {
            return value;
        }
        if (valueType != nullptr && entersImplicitly(type, valueType)) {
            if (node_handle_t coerced = coerceTo(value, type, gct->loopBody());
                coerced != nullptr) {
                return coerced;
            }
        }
        diags_->of(SemanticDiag::LoopBodyTypeMismatch)
            .atOrigin(gct->loopBody()->origin())
            .commit(valueType ? valueType->toString() : std::string("void"), type->toString());
        throw BuildAbortException();
    };
    // The state the next round starts from, in the order the loop's state holds it.
    const auto machineStateOf = [&](const std::vector<node_handle_t> &components) {
        std::vector<size_t> slots(components.size());
        std::iota(slots.begin(), slots.end(), size_t{0});
        node_handle_t fill = currGraph_->addFillNode(
            machineType, runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
        const slot_t empty = camel::core::rtdata::toSlot(
            ::Tuple::create(components.size(), camel::core::mm::autoSpace()));
        node_handle_t source = currGraph_->draft().ref(
            currGraph_->draft().materializeStaticValue(empty, machineType));
        linkNodes(LinkType::Norm, source, fill);
        for (node_handle_t component : components) {
            linkNodes(LinkType::With, component, fill);
        }
        registerNodeOrigin(context_, fill, gct, "gir.loop.state");
        return fill;
    };
    const auto booleanConstant = [&](bool each) {
        node_handle_t node = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(each)));
        registerNodeOrigin(context_, node, gct, "gir.data");
        return node;
    };

    // Entering the loop takes the stream as it is: the first pull waits for the first round, so a
    // loop whose condition refuses every round reads nothing at all.
    std::vector<node_handle_t> machineInitial{sequence, booleanConstant(false)};
    machineInitial.insert(machineInitial.end(), initial.begin(), initial.end());

    // The parts declare the pull and the states they read; the element is bound inside the body,
    // out of the pull, so that the body reads it as the name the program wrote.
    const auto refreshPorts = [&](ArmBuild &part) {
        FunctionTypeFactory params;
        std::vector<runtime::gc_node_ref_t> ids;
        for (size_t i = 0; i < part.ports.size(); ++i) {
            const std::string &name =
                i == 0 ? streamName : (i == 1 ? doneName : stateNames[i - 2]);
            params.addNormArg(name, nodeTypeOf(part.ports[i]), false);
            ids.push_back(nodeIdOf(part.ports[i]));
        }
        params.setExitType(Type::Void());
        part.graph->draft().setRegionPorts(part.region, ids);
        part.graph->draft().setRegionParams(part.region, params.build());
    };
    const auto declarePorts = [&](ArmBuild &part, const GCT::node_ptr_t &exec) {
        const auto add = [&](const std::string &name, Type *type) {
            node_handle_t port = currGraph_->addRegionPortNode(type, name);
            part.graph->draft().setNodeRegion(nodeIdOf(port), part.region);
            insertNode(name, port);
            if (recorder_) {
                recordDeclaration(name, port, exec);
            }
            part.ports.push_back(port);
        };
        add(streamName, stream);
        add(doneName, Type::Bool());
        for (size_t j = 0; j < k; ++j) {
            add(stateNames[j], stateTypes[j]);
        }
        refreshPorts(part);
    };

    arm_ptr_t condPart;
    arm_ptr_t bodyPart;
    {
        ExpectedValueScope partScope(expectedValueType_, nullptr);
        // The condition of the loop: where the stream is not over, the condition of the head --
        // and a `for` head wrote none, so there it is `true` and nothing is asked. A round whose
        // stream is over ends the loop without asking the head's condition again, so a condition
        // is asked once per round that has an element and once for the round that finds the end,
        // as the `while` of the array form is asked.
        //
        // The parts the condition is made of stand where the loop is: a `for` head wrote no
        // condition, so the body is what they take their position from.
        const GCT::node_ptr_t condExec =
            camel::core::loopHasCondition(kind) ? gct->loopCondition() : gct->loopBody();
        const auto conditionArms = [&]() -> node_handle_t {
            node_handle_t over = *nodeAt(doneName);
            std::vector<arm_ptr_t> arms(2);
            // The stream is over: the loop ends without asking the condition the head wrote.
            arms[0] = buildArm(condExec, [&] { return booleanConstant(false); });
            arms[1] = buildArm(condExec, [&] {
                return camel::core::loopHasCondition(kind) ? visitBodyNode(gct->loopCondition())
                                                           : booleanConstant(true);
            });
            return buildBranch(condExec, over, &arms);
        };
        condPart = buildArm(
            condExec,
            conditionArms,
            runtime::GCRegionKind::LoopCond,
            [&](ArmBuild &part) { declarePorts(part, condExec); });
        Type *condType = condPart->exitType();
        if (condType == nullptr || condType->code() != TypeCode::Bool) {
            diags_->of(SemanticDiag::LoopConditionNotBool)
                .atOrigin(condExec->origin())
                .commit(condType ? condType->toString() : std::string("void"));
            throw BuildAbortException();
        }
        // The body of the loop: the pull of this round -- the condition has already held, so this
        // is where the step function is called -- and then, where the pull answered an element,
        // the element, the program's body and the state of the next round.
        bodyPart = buildArm(
            gct->loopBody(),
            [&]() -> node_handle_t {
                node_handle_t pull = pullOf(*nodeAt(streamName), gct->loopBody());
                const std::string pullTemp = bindTemporary(pull);
                node_handle_t isNull = applyTagOperator(TagIsNull, pullTemp, gct->loopBody());
                std::vector<arm_ptr_t> arms(2);
                // The pull answered `null`: the stream is over. The program's body does not run,
                // the stream and the state stay as they are, and the round that begins after it
                // ends the loop without asking the condition again.
                arms[0] = buildArm(gct->loopBody(), [&] {
                    std::vector<node_handle_t> next{*nodeAt(streamName), booleanConstant(true)};
                    for (size_t j = 0; j < k; ++j) {
                        next.push_back(*nodeAt(stateNames[j]));
                    }
                    return machineStateOf(next);
                });
                arms[1] = buildArm(gct->loopBody(), [&] {
                    // The element this round is about and the rest of the stream, taken out of
                    // the pull that has just answered.
                    node_handle_t pair = applyTagOperator(TagNullValue, pullTemp, gct->loopBody());
                    node_handle_t element = currGraph_->addAccsNode(elementType, 0);
                    linkNodes(LinkType::Norm, pair, element);
                    registerNodeOrigin(context_, element, gct->loopBody(), "gir.loop.element");
                    insertNode(gct->loopElementName(), element);
                    if (recorder_) {
                        recordDeclaration(gct->loopElementName(), element, gct->loopBody());
                    }
                    node_handle_t rest = currGraph_->addAccsNode(stream, 1);
                    linkNodes(LinkType::Norm, pair, rest);
                    registerNodeOrigin(context_, rest, gct->loopBody(), "gir.loop.rest");

                    // What the program's body gives is the state of the next round.
                    node_handle_t state = intoState(
                        loopStateValueOf(visitBodyNode(gct->loopBody())), stateType);
                    if (state == nullptr) {
                        diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                            .atOrigin(gct->loopBody()->origin())
                            .commit(std::string("void"), stateType->toString());
                        throw BuildAbortException();
                    }
                    // The body runs after the pull it reads its element from.
                    linkNodes(LinkType::Ctrl, pull, state);
                    std::vector<node_handle_t> next{rest, booleanConstant(false)};
                    if (k == 1) {
                        next.push_back(state);
                    } else {
                        for (size_t j = 0; j < k; ++j) {
                            node_handle_t component = currGraph_->addAccsNode(
                                stateTypes[j], static_cast<uint32_t>(j));
                            linkNodes(LinkType::Norm, state, component);
                            registerNodeOrigin(
                                context_, component, gct->loopBody(), "gir.loop.state");
                            next.push_back(component);
                        }
                    }
                    return machineStateOf(next);
                });
                return buildBranch(gct->loopBody(), isNull, &arms);
            },
            runtime::GCRegionKind::LoopBody,
            [&](ArmBuild &part) { declarePorts(part, gct->loopBody()); });
    }

    node_handle_t loopNode = currGraph_->addRegionLoopNode(LoopKind::While, machineType);
    registerNodeOrigin(context_, loopNode, gct, "gir.loop");
    stampPermissions(loopNode);
    for (node_handle_t value : machineInitial) {
        linkNodes(LinkType::Norm, value, loopNode);
    }
    // The With inputs of the node are the results of its parts (the body's, then the
    // condition's) and the slot the rounds are counted in; the parts are its regions.
    std::vector<arm_ptr_t> parts{bodyPart};
    linkNodes(LinkType::With, bodyPart->output, loopNode);
    if (condPart) {
        parts.push_back(condPart);
        linkNodes(LinkType::With, condPart->output, loopNode);
    }
    linkNodes(LinkType::With, currGraph_->addRegionPortNode(Type::Int64(), ""), loopNode);
    runtime::GraphDraft &draft = currGraph_->draft();
    draft.setRegionOwner(bodyPart->region, nodeIdOf(loopNode), 1);
    draft.setRegionResult(bodyPart->region, nodeIdOf(bodyPart->output));
    if (condPart) {
        draft.setRegionOwner(condPart->region, nodeIdOf(loopNode), 0);
        draft.setRegionResult(condPart->region, nodeIdOf(condPart->output));
    }

    // Running the loop is like entering an arm: it uses every variable its parts used and
    // writes those they wrote.
    std::vector<node_handle_t> capturedWrites;
    mergeArmVariables(parts, loopNode, capturedWrites);
    const auto originOf = [&](node_handle_t variable) {
        const auto origin = captureOriginOf_.find(variable);
        return origin != captureOriginOf_.end() ? origin->second : variable;
    };
    std::vector<DraftGraphBuilder *> callees;
    std::set<node_handle_t> directWrites;
    for (const arm_ptr_t &part : parts) {
        callees.insert(callees.end(), part->callees.begin(), part->callees.end());
        for (node_handle_t variable : part->directWrites) {
            directWrites.insert(originOf(variable));
        }
    }
    if (ArmBuild *outer = activeArm()) {
        outer->callees.insert(outer->callees.end(), callees.begin(), callees.end());
        for (const arm_ptr_t &part : parts) {
            outer->directWrites.insert(part->directWrites.begin(), part->directWrites.end());
        }
    }
    if (!capturedWrites.empty() && !synced_) {
        std::vector<node_handle_t> origins;
        for (node_handle_t variable : capturedWrites) {
            origins.push_back(originOf(variable));
        }
        unit_->pendingIgnoredWrites.push_back(
            {callees, origins, directWrites, gct->origin(), "The loop", "its body writes"});
    }
    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, loopNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, loopNode);
        }
        lastSyncedNode_ = loopNode;
        chainedNodes_.insert(loopNode);
    }

    // The program sees the state it wrote, not the stream and the flag beside it: one state is
    // its own value, several are a tuple of them.
    node_handle_t loopValue = nullptr;
    if (k == 1) {
        loopValue = currGraph_->addAccsNode(stateTypes.front(), 2);
        linkNodes(LinkType::Norm, loopNode, loopValue);
        registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
    } else {
        std::vector<size_t> slots(k);
        std::iota(slots.begin(), slots.end(), size_t{0});
        loopValue = currGraph_->addFillNode(
            stateType, runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
        const slot_t empty = camel::core::rtdata::toSlot(
            ::Tuple::create(k, camel::core::mm::autoSpace()));
        node_handle_t source =
            currGraph_->draft().ref(currGraph_->draft().materializeStaticValue(empty, stateType));
        linkNodes(LinkType::Norm, source, loopValue);
        for (size_t j = 0; j < k; ++j) {
            node_handle_t component =
                currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j + 2));
            linkNodes(LinkType::Norm, loopNode, component);
            linkNodes(LinkType::With, component, loopValue);
        }
        registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
    }
    return loopValue;
}

void Builder::noteArmGroup(const std::vector<arm_ptr_t> &arms) {
    // One arm excludes nothing: an `if` without `else` has a path where no arm ran and the value
    // passed on, so only two arms or more say which uses cannot both happen.
    if (arms.size() < 2 || currGraph_ == nullptr) {
        return;
    }
    std::vector<uint32_t> regions;
    regions.reserve(arms.size());
    for (const arm_ptr_t &arm : arms) {
        regions.push_back(arm->region);
    }
    armGroups_.emplace_back(currGraph_.get(), std::move(regions));
}

void Builder::consumeStream(node_handle_t value, const GCT::node_ptr_t &at) {
    if (value == nullptr || currGraph_ == nullptr || at == nullptr) {
        return;
    }
    StreamConsumption use;
    use.graph  = currGraph_.get();
    use.value  = value;
    use.origin = at->origin();
    use.arms.reserve(armStack_.size());
    for (ArmBuild *arm : armStack_) {
        // A loop's condition and body run again on every round, so a use standing in one of them
        // consumes the value once per round; the ports of the region are the values the round
        // starts from (the element, the states), and one of them is the loop's own state, which
        // is what a value consumed per round has to be.
        const bool round = arm->kind == runtime::GCRegionKind::LoopCond ||
                           arm->kind == runtime::GCRegionKind::LoopBody;
        use.arms.push_back({arm->region, round, round ? arm->ports : std::vector<node_handle_t>{},
                            arm->origin});
    }
    streamConsumptions_.push_back(std::move(use));
}

void Builder::checkStreamConsumption(DraftGraphBuilder *graph) {
    // A value consumed in a loop's round region is consumed once per round (design principles
    // 5.4): under value semantics every round sees the same stream, so the value has to be the
    // loop's own state -- which the round takes from its port and gives back as the next state --
    // and a value written outside the loop is consumed again by each round.
    for (const StreamConsumption &use : streamConsumptions_) {
        if (use.graph != graph) {
            continue;
        }
        for (const StreamConsumption::ArmUse &arm : use.arms) {
            if (!arm.round ||
                std::find(arm.ports.begin(), arm.ports.end(), use.value) != arm.ports.end()) {
                continue;
            }
            Type *valueType = valueTypeOf(nodeTypeOf(use.value));
            diags_->of(SemanticDiag::StreamConsumedInLoop)
                .atOrigin(use.origin)
                .relatedOrigin(arm.origin, "the loop this use stands in")
                .commit(valueType != nullptr ? valueType->toString() : std::string("a stream"));
            throw BuildAbortException();
        }
    }
    // Two uses of one stream are one consumer when one of them stands in an arm the other does
    // not: only one arm of a branch runs, so the two uses cannot both happen on one path. The arms
    // of a branch are complete only once it is, and the uses stand in the arms while those are
    // built, so this runs where the graph is complete.
    const auto apart = [&](const StreamConsumption &left, const StreamConsumption &right) {
        for (const auto &group : armGroups_) {
            if (group.first != graph) {
                continue;
            }
            const auto regionOf = [&](const std::vector<StreamConsumption::ArmUse> &use) -> uint32_t {
                for (const StreamConsumption::ArmUse &arm : use) {
                    if (std::find(group.second.begin(), group.second.end(), arm.region) !=
                        group.second.end()) {
                        return arm.region;
                    }
                }
                return 0;
            };
            const uint32_t mine   = regionOf(left.arms);
            const uint32_t theirs = regionOf(right.arms);
            if (mine != 0 && theirs != 0 && mine != theirs) {
                return true;
            }
        }
        return false;
    };
    for (size_t i = 0; i < streamConsumptions_.size(); ++i) {
        const StreamConsumption &first = streamConsumptions_[i];
        if (first.graph != graph) {
            continue;
        }
        for (size_t j = i + 1; j < streamConsumptions_.size(); ++j) {
            const StreamConsumption &second = streamConsumptions_[j];
            if (second.graph != graph || second.value != first.value || apart(first, second)) {
                continue;
            }
            // The later use is the second consumer, and the first is where the stream was read.
            Type *valueType = valueTypeOf(nodeTypeOf(second.value));
            diags_->of(SemanticDiag::StreamConsumedTwice)
                .atOrigin(second.origin)
                .relatedOrigin(first.origin, "the stream is consumed here first")
                .commit(valueType != nullptr ? valueType->toString() : std::string("a stream"));
            throw BuildAbortException();
        }
    }
}

node_handle_t Builder::visitAnnoNode(const GCT::node_ptr_t &gct) {
    ENTER("ANNO");
    ASSERT(gct->size() == 1, "ANNO node should have exactly one child.");
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    LEAVE("ANNO");
    return node;
}

node_handle_t Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    ExpectedValueScope expectedScope(expectedValueType_, expectedExitTypeHere());
    // `return f`: the function value of a function named here. The name itself is not a
    // value, and left in place it reached the end of compilation as a DREF node.
    node_handle_t resNode = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
    if (genericDrefs_.contains(resNode)) {
        // `return id` under a declared function type: that type is the function wanted.
        resNode = genericValueFor(resNode, expectedExitTypeHere(), gct->at(0));
    }
    if (nodeIsKind(resNode, runtime::GCNodeKind::Dref)) {
        const auto &returned = gct->at(0);
        rejectNameWithoutValue(
            resNode,
            returned->type() == GCT::NodeKind::DREF ? returned->name()
                                                    : std::string("the returned name"),
            gct);
    }
    // A function that declares T? or T! returns its plain value boxed (see adaptExitValue).
    resNode                    = adaptExitValue(gct, resNode);
    node_handle_t outputAnchor = resNode;
    node_vec_t pendingCtrlInputs;
    if (auto modifier = modifierOf(resNode); modifier.has_value()) {
        pendingCtrlInputs.push_back(*modifier);
    }
    if (synced_ && lastSyncedNode_ != nullptr) {
        pendingCtrlInputs.push_back(lastSyncedNode_);
    }

    // The exit takes the value by a data edge, and the statement the chain ends with reaches it by
    // a sync edge: the value's own node is what the graph hands on, and only a value that does not
    // already read that statement needs the edge -- with it, a GATE to carry the order. A value
    // that came out of that statement (the `v` of `v = 2; v`) is ordered after it already, so it
    // takes no control edge and no GATE, and the tail `return v` and a body whose last statement
    // is `v` ask this once and build one graph (docs/design/effects/edges.cn.md 5.3).
    node_vec_t syncInputs;
    for (node_handle_t ctrlInput : pendingCtrlInputs) {
        if (!valueNeedsSyncEdge(resNode, ctrlInput)) {
            continue;
        }
        syncInputs.push_back(ctrlInput);
    }
    if (!syncInputs.empty() && !nodeIsKind(resNode, runtime::GCNodeKind::Gate)) {
        node_handle_t gatedValue = currGraph_->addGateNode(nodeTypeOf(resNode));
        linkNodes(LinkType::Norm, resNode, gatedValue);
        outputAnchor = gatedValue;
    }
    for (node_handle_t ctrlInput : syncInputs) {
        if (linkCheek(ctrlInput, outputAnchor) &&
            !runtime::hasControlDependency(
                *nodeDraftOf(outputAnchor),
                nodeIdOf(ctrlInput),
                nodeIdOf(outputAnchor))) {
            linkNodes(LinkType::Ctrl, ctrlInput, outputAnchor);
        }
    }
    if (activeStaticBody_ != nullptr) {
        // The selected body of a `match type` has no region of its own: its `return` gives the
        // value of the match (visitTypeMatchNode), not of the function around it.
        activeStaticBody_->value    = outputAnchor;
        activeStaticBody_->returned = true;
    } else if (activeRestBody_ != nullptr) {
        // A rest function is expanded into the caller's graph, so its `return` is not the
        // caller's: it gives the value of the expanded body (inlineRestCall).
        activeRestBody_->value    = outputAnchor;
        activeRestBody_->returned = true;
    } else if (ArmBuild *arm = activeArm()) {
        // The value of the arm, not of the function.
        arm->output = outputAnchor;
        arm->value  = resNode;
        arm->type   = isVoidGraphExitType(nodeTypeOf(resNode)) ? Type::Void() : nodeTypeOf(resNode);
        arm->returned = true;
    } else {
        checkDeclaredReturn(currGraph_, nodeTypeOf(resNode), gct->origin(), gct->at(0));
        if (!droppedGraphs_.contains(currGraph_.get())) {
            setGraphReturnFromResult(
                currGraph_,
                nodeIdOf(outputAnchor),
                nodeIdOf(resNode),
                nodeTypeOf(resNode));
        }
    }

    LEAVE("EXIT");
    return resNode;
}

node_handle_t Builder::visitBodyNode(const GCT::node_ptr_t &gct) {
    const bool outerSynced        = std::exchange(synced_, false);
    const node_handle_t outerLast = std::exchange(lastSyncedNode_, {});
    // The last statement of a body is the value the body hands on: a branch there (`if c then
    // … else …` as a function's whole body) stands in the position of that value, and so do
    // the `return`s written in its arms (design principles 5.1: an arm's `return` takes the type of
    // position the arm is at, never the declared type of the function around it). The position
    // is passed to visitExecNode, which builds that statement through the same expected-type
    // channel every other position uses.
    node_handle_t result{};
    try {
        result = visitExecNode(gct, tailPositionType());
        if (lastBlockStatement_.first != nullptr) {
            const void *body = activeArm() != nullptr ? static_cast<const void *>(activeArm())
                                                      : static_cast<const void *>(currGraph_.get());
            bodyValueStatement_[body] = lastBlockStatement_;
        }
    } catch (...) {
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        throw;
    }
    synced_         = outerSynced;
    lastSyncedNode_ = outerLast;
    return result;
}

bool Builder::inferLoopStateFromAppend(
    const arm_ptr_t &body, const arm_ptr_t &cond, const GCT::node_ptr_t &loop,
    std::vector<node_handle_t> &initial, std::vector<Type *> &stateTypes, size_t index,
    node_handle_t next) {
    if (next == nullptr || !nodeIsKind(next, runtime::GCNodeKind::Oper)) {
        return false;
    }
    const oper_idx_ptr_t op = nodeGraphOf(next)->operTarget(next);
    if (op == nullptr || op->uri() != ":struct/append_arr") {
        return false;
    }
    const auto normIns = normInputsOf(next);
    if (normIns.size() != 1) {
        return false;
    }
    const size_t portAt = index + (camel::core::loopHasIterable(loop->loopKind()) ? 1 : 0);
    node_handle_t port  = nodeDraftOf(next)->ref(normIns.front());
    if (port == nullptr || portAt >= body->ports.size() || body->ports[portAt] != port) {
        return false;
    }
    const auto withIns = withInputsOf(next);
    if (withIns.size() != 1) {
        return false;
    }
    Type *element = nodeTypeOf(nodeDraftOf(next)->ref(withIns.front()));
    if (element == nullptr || element->code() == TypeCode::Void) {
        return false;
    }
    Type *newType = ArrayType::create(element);
    // The parts were built with the placeholder `void[]`: the append's operand, the ports it
    // reads and the empty literal all have to become the element array's type, and the call is
    // resolved again so its instance carries the element layout.
    DraftGraphBuilder *graph = currGraph_.get();
    retypeValueNode(graph, port, newType);
    retypeValueNode(graph, next, newType);
    retypeValueNode(nodeGraphOf(initial[index]), initial[index], newType);
    if (body->output != nullptr && body->output != next) {
        retypeValueNode(graph, body->output, newType);
    }
    body->type = newType;
    if (cond && index < cond->ports.size()) {
        retypeValueNode(graph, cond->ports[index], newType);
    }
    const auto group = importedOperatorGroupNamed("append");
    if (group != nullptr) {
        const std::vector<StaticArg> statics(1);
        const auto resolved = group->resolve({element}, {newType}, statics, Modifier::None);
        if (resolved.has_value()) {
            graph->retargetOperNode(next, *resolved);
        }
    }
    stateTypes[index] = newType;
    return true;
}

std::vector<Type *> Builder::collectLoopAppends(
    const arm_ptr_t &body, const GCT::node_ptr_t &loop, const std::vector<Type *> &stateTypes,
    const std::vector<bool> &emptyInitial) {
    const size_t stateCount = stateTypes.size();
    if (stateCount == 0 || emptyInitial.size() != stateCount || body->returned) {
        return {};
    }
    runtime::GraphDraft &draft = currGraph_->draft();
    node_handle_t returned     = loopStateValueOf(body->value);
    if (returned == nullptr) {
        return {};
    }
    const size_t portBase = camel::core::loopHasIterable(loop->loopKind()) ? 1 : 0;
    std::vector<node_handle_t> next(stateCount, nullptr);
    if (stateCount == 1) {
        next[0] = returned;
    } else {
        if (!nodeIsKind(returned, runtime::GCNodeKind::Fill)) {
            return {};
        }
        const Type *returnType = valueTypeOf(nodeTypeOf(returned));
        if (returnType == nullptr || returnType->code() != TypeCode::Tuple) {
            return {};
        }
        const auto withIns = withInputsOf(returned);
        const auto payload = nodeDraftOf(returned)->payloadOf(nodeIdOf(returned));
        if (payload.size() < sizeof(runtime::GCFillBody)) {
            return {};
        }
        const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
        if (fill->fillKind != runtime::GCFillKind::Tuple) {
            return {};
        }
        const auto slots = fill->slots();
        if (slots.size() != withIns.size()) {
            return {};
        }
        for (size_t i = 0; i < slots.size(); ++i) {
            if (static_cast<size_t>(slots[i]) >= stateCount) {
                return {};
            }
            next[slots[i]] = nodeDraftOf(returned)->ref(withIns[i]);
        }
    }
    std::vector<Type *> collected;
    std::vector<node_handle_t> values;
    for (size_t j = 0; j < stateCount; ++j) {
        if (!emptyInitial[j] || next[j] == nullptr ||
            !nodeIsKind(next[j], runtime::GCNodeKind::Oper)) {
            continue;
        }
        const oper_idx_ptr_t op = nodeGraphOf(next[j])->operTarget(next[j]);
        if (op == nullptr ||
            (op->uri() != ":struct/append_arr" && op->uri() != ":struct/append_arr_owned")) {
            continue;
        }
        const auto normIns = normInputsOf(next[j]);
        if (normIns.size() != 1) {
            continue;
        }
        node_handle_t port = nodeDraftOf(next[j])->ref(normIns.front());
        if (port == nullptr || portBase + j >= body->ports.size() ||
            body->ports[portBase + j] != port) {
            continue;
        }
        if (normUsersOf(port).size() + withUsersOf(port).size() != 1) {
            continue;
        }
        const auto withIns = withInputsOf(next[j]);
        if (withIns.size() != 1) {
            continue;
        }
        Type *element = nodeTypeOf(nodeDraftOf(next[j])->ref(withIns.front()));
        if (element == nullptr) {
            continue;
        }
        collected.push_back(element);
        values.push_back(nodeDraftOf(next[j])->ref(withIns.front()));
    }
    if (collected.empty()) {
        return {};
    }
    // The body's next state values, in state order, then one appended element per collection.
    // They are the values the body computed this round; reading them back through an ACCS of
    // the loop's own value would both lose the append (nothing would use it) and hand the
    // driver the state it started with.
    if (stateCount > 1) {
        for (size_t j = 0; j < stateCount; ++j) {
            if (next[j] == nullptr) {
                return {}; // a constant component the body does not give us as a value
            }
        }
    }
    std::vector<node_handle_t> components(next.begin(), next.end());
    components.insert(components.end(), values.begin(), values.end());
    std::vector<Type *> resultTypes = stateTypes;
    for (Type *element : collected) {
        resultTypes.push_back(element);
    }
    Type *resultType = TupleType::create(resultTypes);
    std::vector<size_t> slots(resultTypes.size());
    std::iota(slots.begin(), slots.end(), size_t{0});
    // The tuple is a node of the body.
    const uint32_t outerRegion = draft.insertRegion();
    draft.setInsertRegion(body->region);
    node_handle_t result = currGraph_->addFillNode(
        resultType,
        runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
    const slot_t empty = camel::core::rtdata::toSlot(
        ::Tuple::create(resultTypes.size(), camel::core::mm::autoSpace()));
    node_handle_t source = draft.ref(draft.materializeStaticValue(empty, resultType));
    const std::array<runtime::gc_node_ref_t, 1> sourceInput{nodeIdOf(source)};
    draft.setNormInputs(nodeIdOf(result), sourceInput);
    std::vector<runtime::gc_node_ref_t> componentIds;
    for (node_handle_t component : components) {
        componentIds.push_back(nodeIdOf(component));
    }
    draft.setWithInputs(nodeIdOf(result), componentIds);
    draft.setInsertRegion(outerRegion);
    body->output = result;
    body->value  = result;
    body->type   = resultType;
    registerNodeOrigin(context_, result, loop->loopBody(), "gir.loop.collect");
    return collected;
}

void Builder::markLoopStateInPlace(
    const arm_ptr_t &body, const GCT::node_ptr_t &loop, std::vector<node_handle_t> &initial,
    const std::vector<bool> &privateInitial) {
    // The decision is per state, and only a state of these four properties is rewritten: the
    // state is an array, its first value is a literal (a Data or FILL node), its next value is
    // `state->append<v>`, and the body uses that state's port nowhere else. Anything else -- a
    // struct or tuple state, a first value that is a parameter or an array element, a body that
    // also reads the state or ends in `return` -- keeps the copying `append`; the values are the
    // same either way, and only the allocation differs (10.5's in-place guarantee is about the
    // state's data block, not about a promise on the source).
    // A state that was split out of a struct is a component of the states the loop's parts
    // declare, so the count is the initial values', not the LOOP node's names; its first value
    // is a projection of the struct (a value the loop does not own) and is copied once.
    const size_t stateCount = initial.size();
    if (stateCount == 0 || body->returned) {
        return;
    }
    node_handle_t returned = loopStateValueOf(body->value);
    if (returned == nullptr) {
        return;
    }
    // The next value of each state, in state order. One state hands the append call back
    // directly. Two or more return a tuple, a FILL whose With inputs are the computed
    // components and whose payload says which component each one fills; a component written as
    // a constant there (`(xs->append<7>, 0)`) has no value node and no rewrite of its own.
    if (stateCount == 1) {
        markLoopStateInPlaceOne(
            body,
            loop,
            initial,
            0,
            returned,
            !privateInitial.empty() && privateInitial[0]);
        return;
    }
    if (!nodeIsKind(returned, runtime::GCNodeKind::Fill)) {
        return;
    }
    const Type *returnType = valueTypeOf(nodeTypeOf(returned));
    if (returnType == nullptr || returnType->code() != TypeCode::Tuple) {
        return;
    }
    const auto withIns = withInputsOf(returned);
    const auto payload = nodeDraftOf(returned)->payloadOf(nodeIdOf(returned));
    if (payload.size() < sizeof(runtime::GCFillBody)) {
        return;
    }
    const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
    if (fill->fillKind != runtime::GCFillKind::Tuple) {
        return;
    }
    const auto slots = fill->slots();
    if (slots.size() != withIns.size()) {
        return;
    }
    std::vector<node_handle_t> next(stateCount, nullptr);
    for (size_t i = 0; i < slots.size(); ++i) {
        if (static_cast<size_t>(slots[i]) >= stateCount) {
            return;
        }
        next[slots[i]] = nodeDraftOf(returned)->ref(withIns[i]);
    }
    for (size_t j = 0; j < stateCount; ++j) {
        markLoopStateInPlaceOne(
            body,
            loop,
            initial,
            j,
            next[j],
            !privateInitial.empty() && j < privateInitial.size() && privateInitial[j]);
    }
}

void Builder::markLoopStateInPlaceOne(
    const arm_ptr_t &body, const GCT::node_ptr_t &loop, std::vector<node_handle_t> &initial,
    size_t index, node_handle_t next, bool initialIsPrivate) {
    if (next == nullptr || !nodeIsKind(next, runtime::GCNodeKind::Oper)) {
        return;
    }
    // The first value has to be a literal: a name, a call or a projection can name an array
    // that something else also reads, and the loop is about to make its first value private.
    // A component split out of a struct state is such a projection, and it is copied
    // below because the struct it comes from is the program's, not the loop's.
    if (!initialIsPrivate && !nodeIsKind(initial[index], runtime::GCNodeKind::Data) &&
        !nodeIsKind(initial[index], runtime::GCNodeKind::Fill)) {
        return;
    }
    const Type *stateType = valueTypeOf(nodeTypeOf(initial[index]));
    if (stateType == nullptr || stateType->code() != TypeCode::Array) {
        return;
    }
    const oper_idx_ptr_t op = nodeGraphOf(next)->operTarget(next);
    if (op == nullptr || op->uri() != ":struct/append_arr") {
        return;
    }
    // The array the call appends to is this state's parameter, and the body uses that
    // parameter nowhere else: once the round returns, the value it was bound to is gone (the
    // driver binds the round's own state), so nothing can see it as it was.
    const auto normIns = normInputsOf(next);
    if (normIns.size() != 1) {
        return;
    }
    node_handle_t port  = nodeDraftOf(next)->ref(normIns.front());
    const size_t portAt = index + (camel::core::loopHasIterable(loop->loopKind()) ? 1 : 0);
    if (port == nullptr || portAt >= body->ports.size() || body->ports[portAt] != port) {
        return;
    }
    if (normUsersOf(port).size() + withUsersOf(port).size() != 1) {
        return;
    }
    // The appended value is the call's only With input; the owned form takes the same.
    const auto withIns = withInputsOf(next);
    if (withIns.size() != 1) {
        return;
    }
    Type *valueType = nodeTypeOf(nodeDraftOf(next)->ref(withIns.front()));
    if (valueType == nullptr) {
        return;
    }
    const oper_group_ptr_t group =
        context_->importModule("")->nativeOperatorGroup(camel::core::oper::AppendOwned);
    if (group == nullptr) {
        return;
    }
    const std::vector<StaticArg> statics(1);
    const auto owned = group->resolve({valueType}, {nodeTypeOf(port)}, statics, Modifier::None);
    if (!owned.has_value()) {
        return;
    }
    // The loop starts from a private copy of its first value. A literal is a constant of the
    // graph: the same array object on every evaluation of the loop's site, so appending to it
    // in place would edit the program's own literal. One copy per loop entry buys every round.
    const oper_group_ptr_t copyGroup =
        context_->importModule("")->nativeOperatorGroup(camel::core::oper::CopyArray);
    if (copyGroup == nullptr) {
        return;
    }
    const auto copyOp =
        copyGroup->resolve({}, {nodeTypeOf(initial[index])}, statics, Modifier::None);
    if (!copyOp.has_value()) {
        return;
    }
    node_handle_t copy = currGraph_->addOperNode(*copyOp, 0);
    stampPermissions(copy);
    const size_t initialAt = index < loop->loopStateCount() ? index : 0;
    registerNodeOrigin(context_, copy, loop->loopInitial(initialAt), "gir.loop.copy");
    linkNodes(LinkType::Norm, initial[index], copy);
    initial[index] = copy;
    nodeGraphOf(next)->retargetOperNode(next, *owned);
}

void Builder::setBodyResult(const compile_graph_ptr_t &graph, node_handle_t result) {
    if (graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    if (result == nullptr) {
        result = graph->addStaticDataNode(Data::null());
        setGraphReturnFromResult(graph, nodeIdOf(result), nodeIdOf(result), Type::Void());
        return;
    }
    // A sync block that ends in a value hands it back behind a GATE (see visitExecNode): the
    // GATE is the output anchor, the value it forwards is what the graph returns.
    node_handle_t value = result;
    if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
        value = nodeDraftOf(result)->ref(normInputsOf(result).front());
    }
    checkDeclaredReturn(graph, nodeTypeOf(value), 0);
    setGraphReturnFromResult(graph, nodeIdOf(result), nodeIdOf(value), nodeTypeOf(value));
}

} // namespace camel::compile::gir