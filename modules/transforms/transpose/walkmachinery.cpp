// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pieces of the walk that are reached from more than one place and are not the walk itself: the
 * one line a refusal is written on, the one place a parameter's shape is recorded, the four questions
 * about a **type and its zero** (the cotangent space of a type, the cotangent space of a value, the
 * zero a type states, and the zero of a value), and **the sum of two cotangents in a space** with the
 * aggregate it is assembled into.
 *
 * They moved out of `buildTransposeMember` so the node families can reach them without a second copy
 * each. What stays behind is the walk (`transpose/walk.cpp`); what these are handed is `WalkContext`
 * (`transpose/walk.h`), which is why it carries the draft, the forward table and the fact that some
 * cotangent could not be summed.
 */

#include "transpose/walk.h"

#include "transpose/cotangent.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/tree.h"
#include "camel/utils/type.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace camel::transforms {

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::Type;

void refuseMember(WalkContext &ctx, TransposeRefusalCause cause, std::optional<size_t> param) {
    ctx.state.refusal       = TransposeRefusal{cause, param};
    ctx.state.refusal.graph = ctx.function->name();
    ctx.os << "TRANSPOSE function=" << ctx.function->name() << " skip="
           << transposeRefusalWord(cause);
}

void noteUnstatedParameterOf(WalkContext &ctx, size_t param) {
    Type *const paramType  = ctx.function->funcType()->normTypeAt(param);
    const std::string type = paramType != nullptr ? paramType->toString() : std::string();
    std::string name;
    const FunctionType *const signatureType = ctx.function->funcType();
    if (signatureType->hasArgNames()) {
        // One-based place in the written signature, past the `with` types: the same arithmetic
        // the refusal record uses, so the name is the parameter's and not its neighbour's.
        const size_t named = signatureType->withTypesCount() + param;
        if (named < signatureType->argNamesCount()) {
            name = std::string(signatureType->argNameAt(named));
        }
    }
    if (!ctx.state.shapeWanted) {
        ctx.state.shapeWanted = true;
        ctx.state.shapeParam  = param;
        ctx.state.shapeType   = type;
        ctx.state.shapeName   = name;
    }
    ctx.state.refusal.paramName = name;
    ctx.state.refusal.paramType = type;
}

// The cotangent space of a type and of a value, and the two zeros. All four are asked by name from
// more than one place in the walk, and all four answer a question about a type rather than about a
// node, which is why they are here and not a family's.

Type *cotangentSpaceOfType(WalkContext &, const Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(type);
    return space.hasSpace() ? space.space : nullptr;
}

Type *cotangentTypeOfValue(WalkContext &ctx, gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = ctx.function->node(ref);
    return cotangentSpaceOfType(ctx, node != nullptr ? node->dataType() : nullptr);
}

gc_node_ref_t zeroOfCotangentType(WalkContext &ctx, const Type *primal, Type *tangentType) {
    if (primal == nullptr || primal->code() != camel::core::type::TypeCode::Array ||
        tangentType == nullptr || tangentType != primal) {
        return rt::kInvalidNodeRef;
    }
    const auto *const arrayType = tt::as_ptr<ArrayType>(const_cast<Type *>(primal));
    if (arrayType->elemType() == nullptr ||
        !camel::core::type::isFloatingScalar(arrayType->elemType()->code())) {
        return rt::kInvalidNodeRef;
    }
    const std::span<const camel::core::type::ExtentForm> stated = arrayType->extents();
    if (stated.empty()) {
        return rt::kInvalidNodeRef;
    }
    std::vector<int64_t> extents;
    extents.reserve(stated.size());
    for (const camel::core::type::ExtentForm &form : stated) {
        const std::optional<int64_t> size = form.evaluate({});
        if (!size.has_value()) {
            // An extent the type does not state: the zero is the forward argument's, and the
            // witness arrives as one of the cut's residuals. This must not answer an empty array
            // instead -- an unwitnessed zero is wrong at every length but 0 -- so a missing
            // witness is a loud refusal, never a quiet wrong width.
            return rt::kInvalidNodeRef;
        }
        extents.push_back(*size);
    }
    ::Array *const zeros = ::Array::create(
        camel::core::mm::autoSpace(),
        arrayType,
        std::span<const int64_t>(extents),
        /*zeroInit=*/true);
    if (zeros == nullptr) {
        return rt::kInvalidNodeRef;
    }
    return ctx.draft->materializeStaticValue(
        camel::core::rtdata::toSlot<::Array *>(zeros), const_cast<Type *>(primal),
        rt::kGCNodeFlagConstant);
}

gc_node_ref_t zeroOfValue(WalkContext &ctx, gc_node_ref_t valueRef) {
    const camel::runtime::GCNode *node = ctx.function->node(valueRef);
    const Type *type                   = node != nullptr ? node->dataType() : nullptr;
    const camel::core::TangentSpaceAnswer space = type == nullptr
                                                      ? camel::core::TangentSpaceAnswer{}
                                                      : camel::core::tangentSpaceOf(type);
    const gc_node_ref_t forwarded =
        ctx.forwardOf != nullptr && valueRef < ctx.forwardOf->size() ? (*ctx.forwardOf)[valueRef]
                                                                    : rt::kInvalidNodeRef;
    const gc_node_ref_t built =
        zeroOfSpaceOf(*ctx.draft, space.hasSpace() ? space.space : nullptr, forwarded);
    if (built != rt::kInvalidNodeRef) {
        return built;
    }
    // A shape the walk has no value for, on a type that states none: the zero the type itself
    // states (see `zeroOfCotangentType`).
    return space.hasSpace() ? zeroOfCotangentType(ctx, type, space.space) : rt::kInvalidNodeRef;
}

// The sum of two cotangents, and the two node builders it is written with. Everything the sum needs
// that is not a type is in the context, so it reads the same whether the walk or a family asks.
namespace {

/// One operator node of the new graph, with its operands.
gc_node_ref_t primitiveOf(
    WalkContext &ctx, Type *type, const char *uri, std::vector<gc_node_ref_t> inputs) {
    const gc_node_ref_t made = ctx.draft->addOperNode(type, nullptr, uri);
    ctx.draft->setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
    return made;
}

/// One member of an aggregate cotangent, as a node of the new graph: the place `place` of a value
/// whose type is `aggregateType`. A struct's place is named (an `Accs` of `GCAccsKind::StructKey`), a
/// tuple's is numbered (`TupleIndex`) -- the two forms the builder writes for `q.0` and `qq.a`, so a
/// member read here is read exactly as a program reads it.
gc_node_ref_t memberOf(WalkContext &ctx, gc_node_ref_t value, Type *aggregateType, size_t place) {
    Type *const memberType = camel::core::type::tree::childType(aggregateType, place);
    if (value == rt::kInvalidNodeRef || memberType == nullptr) {
        return rt::kInvalidNodeRef;
    }
    gc_node_ref_t member = rt::kInvalidNodeRef;
    if (aggregateType->code() == camel::core::type::TypeCode::Struct) {
        const auto *structure = tt::as_ptr<camel::core::type::StructType>(aggregateType);
        if (structure == nullptr || place >= structure->size()) {
            return rt::kInvalidNodeRef;
        }
        member = ctx.draft->addAccsNode(memberType, structure->fieldName(place));
    } else {
        member = ctx.draft->addAccsNode(memberType, static_cast<uint32_t>(place));
    }
    ctx.draft->setNormInputs(member, std::span<const gc_node_ref_t>(&value, 1));
    return member;
}

} // namespace

gc_node_ref_t aggregateOfMembers(
    WalkContext &ctx, Type *aggregateType, const std::vector<gc_node_ref_t> &parts) {
    if (aggregateType == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const bool structure = aggregateType->code() == camel::core::type::TypeCode::Struct;
    camel::core::rtdata::Object *empty =
        structure ? static_cast<camel::core::rtdata::Object *>(
                        ::Struct::create(parts.size(), camel::core::mm::autoSpace()))
                  : static_cast<camel::core::rtdata::Object *>(
                        ::Tuple::create(parts.size(), camel::core::mm::autoSpace()));
    if (empty == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const gc_node_ref_t source = ctx.draft->materializeStaticValue(
        camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(empty), aggregateType);
    std::vector<size_t> slots;
    for (size_t i = 0; i < parts.size(); ++i) {
        slots.push_back(i);
    }
    const gc_node_ref_t filled = ctx.draft->addFillNode(
        aggregateType,
        rt::makeFillPayload(
            structure ? rt::GCFillKind::Struct : rt::GCFillKind::Tuple, slots));
    ctx.draft->setNormInputs(filled, std::span<const gc_node_ref_t>(&source, 1));
    ctx.draft->setWithInputs(filled, std::span<const gc_node_ref_t>(parts));
    return filled;
}

gc_node_ref_t sumOfCotangents(
    WalkContext &ctx, Type *space, gc_node_ref_t into, gc_node_ref_t node) {
    if (into == rt::kInvalidNodeRef) {
        return node;
    }
    const std::string addUri = addUriOfSpace(space);
    if (space != nullptr && !addUri.empty() &&
        space->code() != camel::core::type::TypeCode::Tuple &&
        space->code() != camel::core::type::TypeCode::Struct) {
        return into == node ? primitiveOf(ctx, space, addUri.c_str(), {node, node})
                            : primitiveOf(ctx, space, addUri.c_str(), {into, node});
    }
    if (space != nullptr && (space->code() == camel::core::type::TypeCode::Tuple ||
                             space->code() == camel::core::type::TypeCode::Struct)) {
        const auto count = camel::core::type::tree::arity(space);
        if (!count) {
            // Not a type with members: the aggregate branch of this sum cannot run, and a count read
            // out of another type's bytes would have made it run on nothing.
            return rt::kInvalidNodeRef;
        }
        std::vector<gc_node_ref_t> parts;
        parts.reserve(*count);
        for (size_t i = 0; i < *count; ++i) {
            Type *const memberType = camel::core::type::tree::childType(space, i);
            const camel::core::TangentSpaceAnswer memberSpace = memberType != nullptr
                                                                   ? camel::core::tangentSpaceOf(
                                                                         memberType)
                                                                   : camel::core::TangentSpaceAnswer{};
            if (!memberSpace.hasSpace() || memberSpace.space == nullptr) {
                // No tangent space of its own: the place is the unit, and the unit's sum is the one
                // value there is. Handing either side on says exactly that.
                const gc_node_ref_t kept = memberOf(ctx, into, space, i);
                parts.push_back(kept != rt::kInvalidNodeRef ? kept : memberOf(ctx, node, space, i));
                continue;
            }
            const gc_node_ref_t left  = memberOf(ctx, into, space, i);
            const gc_node_ref_t right = into == node ? left : memberOf(ctx, node, space, i);
            if (left == rt::kInvalidNodeRef || right == rt::kInvalidNodeRef) {
                parts.clear();
                break;
            }
            const gc_node_ref_t summed = sumOfCotangents(ctx, memberSpace.space, left, right);
            if (summed == rt::kInvalidNodeRef) {
                // The member's own refusal has already been recorded with the member's name.
                parts.clear();
                break;
            }
            parts.push_back(summed);
        }
        if (!parts.empty() && parts.size() == count) {
            const gc_node_ref_t assembled = aggregateOfMembers(ctx, space, parts);
            if (assembled != rt::kInvalidNodeRef) {
                return assembled;
            }
        }
        if (ctx.cotangentArithmeticMissing) {
            return rt::kInvalidNodeRef;
        }
    }
    if (addUri.empty() || space == nullptr) {
        ctx.cotangentArithmeticMissing = true;
        ctx.os << "TRANSPOSE function=" << ctx.function->name() << " cotangent-no-arithmetic"
               << '\n';
        return rt::kInvalidNodeRef;
    }
    return into == node ? primitiveOf(ctx, space, addUri.c_str(), {node, node})
                        : primitiveOf(ctx, space, addUri.c_str(), {into, node});
}

} // namespace camel::transforms
