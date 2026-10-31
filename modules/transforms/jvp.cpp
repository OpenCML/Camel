// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "jvp.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

FunctionType *jvpSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    Type *result = function->exitType();
    if (result != Type::Float64()) {
        return nullptr;
    }
    auto *lifted = TupleType::create(std::vector<Type *>{Type::Float64(), Type::Float64()});
    camel::core::type::param_vec_t params;
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        if (function->normTypeAt(i) != Type::Float64()) {
            return nullptr;
        }
        params.emplace_back(lifted, false);
    }
    if (params.empty()) {
        return nullptr;
    }
    return FunctionType::create(camel::core::type::param_vec_t{}, params, lifted);
}

JvpGraph buildJvpGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function) {
    // The skeleton, and nothing else yet: the five steps are fixed, none of them is written.
    //
    // 1. A new member of a group of its own: `GraphDraftGroup group(context, program)`, one reserved
    //    member, its signature `jvpSignature(function->funcType())`.
    // 2. Each parameter of `function` becomes one port of the lifted pair's type; the pair's first
    //    element is the value the primitive reads, its second is the tangent the caller passed.
    // 3. Every node is copied in order: a `Port` reads the first element of its lifted parameter, a
    //    constant or a captured value is lifted with a zero tangent, and a primitive keeps its value
    //    while its tangent becomes a call of the closure its rule instance carries
    //    (`GenericRuleInstance::rhsBuilder`, typed by the tuple it captures -- the one thing
    //    measured for certain, f9d352ce4), fed the lifted tangents of its inputs.
    // 4. The exit is the lifted result: `Tuple{value, tangent}`.
    // 5. The group is encoded and its program answered with, together with the member graph -- the
    //    new function value whose caller decides which direction to ask about.
    (void)context;
    (void)program;
    (void)function;
    return JvpGraph{};
}

} // namespace camel::transforms