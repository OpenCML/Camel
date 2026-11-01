// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tangent-space reading of tangent_space.h: one line per type, `TANGENT type=<t> space=<a>`,
 * with `<a>` the answer of `camel::core::tangentSpaceOf` (`none`, `self`, `other=<t>`).
 *
 * The list is the three tiers the query distinguishes, one type standing for each, plus the two
 * that a transform meets first: a plain float and a plain integer.
 */

#include "camel/passes/test/tangent_space.h"

#include "camel/core/tangent_space.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"

#include <vector>

namespace {

using camel::core::TangentSpaceAnswer;
using camel::core::tangentSpaceOf;
using camel::core::type::ArrayType;
using camel::core::type::TupleType;
using camel::core::type::Type;

/// The types the reading asks about, in the order the answer is printed.
std::vector<Type *> typesToRead() {
    return {
        Type::Float64(),
        Type::Float32(),
        Type::Float16(),
        Type::BFloat16(),
        Type::Int32(),
        Type::String(),
        static_cast<Type *>(ArrayType::create(Type::Float64())),
        static_cast<Type *>(ArrayType::create(Type::Int32())),
        static_cast<Type *>(
            TupleType::create(std::vector<Type *>{Type::Float64(), Type::Int32()})),
        static_cast<Type *>(TupleType::create(std::vector<Type *>{Type::Float64(), Type::Float64()})),
    };
}

} // namespace

const camel::runtime::Program *
TangentSpaceTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    for (Type *type : typesToRead()) {
        const TangentSpaceAnswer answer = tangentSpaceOf(type);
        os << "TANGENT type=" << type->toString() << " space=" << answer.describe() << "\n";
    }
    return program;
}