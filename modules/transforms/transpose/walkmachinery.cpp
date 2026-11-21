// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pieces of the walk that are reached from more than one place and are not the walk itself: the
 * one line a refusal is written on, the one place a parameter's shape is recorded, and the four
 * questions about a **type and its zero** -- the cotangent space of a type, the cotangent space of a
 * value, the zero a type states, and the zero of a value.
 *
 * They moved out of `buildTransposeMember` so the node families can reach them without a second
 * copy each. What stays behind is the walk (`transpose/walk.cpp`); what these are handed is
 * `WalkContext` (`transpose/walk.h`), which is why it carries the draft and the forward table.
 */

#include "transpose/walk.h"

#include "transpose/cotangent.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
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

} // namespace camel::transforms
