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
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return JvpGraph{};
    }
    FunctionType *signature = jvpSignature(function->funcType());
    if (signature == nullptr) {
        os << "REINTERP jvp function=" << function->name() << " skip=signature\n";
        return JvpGraph{};
    }
    // One parameter, for now: with two the exit would need a way to combine two lifted pairs into
    // the one lifted result, and that is a node in the body -- step 3's business, not this one's.
    if (function->funcType()->normTypesCount() != 1) {
        os << "REINTERP jvp function=" << function->name()
           << " skip=params " << function->funcType()->normTypesCount() << "\n";
        return JvpGraph{};
    }

    // Steps 1 and 2: a new member with the lifted signature, and one port per parameter, the port
    // itself being the lifted pair `(value, tangent)` the caller passes.
    auto *lifted = TupleType::create(std::vector<Type *>{Type::Float64(), Type::Float64()});
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "jvp_of_" + std::string(function->name()), "jvp_of_" + std::string(function->name()));
    {
        auto &draft = group.draft(member);
        draft.setFuncType(signature);
        const gc_node_ref_t param = draft.addPortNode(lifted, draft.allocateRuntimeSlot(lifted));
        // Placeholder, and said so: the graph answers with the parameter it was given, so `value`
        // is the caller's value and the `tangent` half is whatever the caller passed -- no
        // primitive has been rewritten yet, and nothing has been differentiated. What this layer
        // establishes is that the new graph exists, with the lifted signature, and is addressable.
        draft.setOutputNode(param);
        draft.setReturnNode(param, rt::GCReturnKind::Self);
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    const auto *madeType = made != nullptr ? made->funcType() : nullptr;
    os << "REINTERP jvp function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << (madeType != nullptr ? madeType->toString() : std::string("-"))
       << " exit=param tangent=placeholder(from caller, not computed)\n";
    return JvpGraph{group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms