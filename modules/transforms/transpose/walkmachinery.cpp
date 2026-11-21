// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The one line a refusal is written on, and the one place a parameter's shape is recorded: the two
 * pieces of the walk that every node family reaches, moved out of `buildTransposeMember` so the
 * families can reach them without a second copy each. What stays behind is the walk
 * (`transpose/walk.cpp`); what a family is handed is `WalkContext` (`transpose/walk.h`).
 */

#include "transpose/walk.h"

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"

#include <optional>
#include <string>

namespace camel::transforms {

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

} // namespace camel::transforms
