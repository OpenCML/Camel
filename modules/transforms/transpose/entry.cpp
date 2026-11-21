// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The transposition's entry point: one `TransposeGraph` for a whole function, built out of the
 * member the walk builds for it.
 *
 * The walk itself -- the state, the pair call and the member builder -- is `transpose.cpp`, and what
 * it hands this file is declared in `transpose/walk.h`.
 */

#include "transpose.h"

#include "transpose/walk.h"

#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <ostream>
#include <string>
#include <vector>

namespace camel::transforms {

namespace rt = camel::runtime;

using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;

TransposeGraph buildTransposeGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os, bool promisedToTheOperator) {
    if (program == nullptr || function == nullptr) {
        return TransposeGraph{};
    }
    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "transpose_of_" + std::string(function->name()),
        "transpose_of_" + std::string(function->name()));
    TransposeState state{group, program, {}, context};
    // What the member takes and what it answers is the signature's (`transposeSignature`, which
    // reads the lift law), and this entrance adds nothing to it: the ports are the value places the
    // reading names and the answer is the cotangents of the places a cotangent is answered for. A
    // graph whose signature is itself a `Lift` therefore comes out with the value half as ports and
    // the tangent half as answer -- the same reading a cut makes of the same shape -- instead of one
    // test here deciding that such a graph has no values at all; and a lift of a function that eats
    // only tangents comes out with no value port and one answer per place, because that lift is
    // linear in both halves (`transposedPlacesOf`).
    if (!buildTransposeMember(state, function, member, os, {}, nullptr)) {
        // The cause the walk recorded, not a reading of the log: a null graph and the reason it is
        // null are one answer, handed back together.
        return TransposeGraph{state.refusal};
    }
    group.encode();
    const camel::runtime::GCGraph *made = group.encoded(member);
    // The graph must take exactly what the operator's own type says it takes. `transpose(f)` is
    // resolved by `transposeOperatorSignature`, which reads `f`'s type and no body, so the call
    // site is told `(ct) => (cotangents)` -- one port, the cotangent. A member that needs a value
    // the body reads and no type of the program can supply declares one port *more*, and the call
    // site then hands the graph one argument where it has two ports: the run dies on `nargs.size()
    // == normPorts.size()`, which is a signal and says nothing. Rather than let the shape reach the
    // run, the build compares the two counts here -- the only place that has both -- and refuses by
    // name. The count is not the rule: it is the one reading that can tell "this member is the
    // function the call site was promised" from "this member is a different function".
    //
    // Only the operator's own call site is compared: `promisedToTheOperator` is what the kernel
    // says, and it is false for a member the walk builds for a *call inside a body*, whose caller
    // writes its type with the captures in hand (`calleeNeeds`) and therefore fills them. Comparing
    // there would refuse the honest `index_into` over an array whose extent only the caller knows
    // -- the value is carried, the adjoint is built from `ct` and a known value, and the rule
    // allows exactly that.
    if (promisedToTheOperator) {
        FunctionType *const promised =
            function->funcType() != nullptr ? transposeSignature(function->funcType()) : nullptr;
        const size_t promisedPorts = promised != nullptr ? promised->normTypesCount() : 0;
        const size_t builtPorts    = made != nullptr ? made->normPorts().size() : 0;
        if (promisedPorts != 0 && builtPorts != promisedPorts) {
            // Which repair the reader is owed depends on *what* the extra port carries: a value a
            // law reads that no type states (a capture the operator's type cannot name) or the
            // shape of a parameter whose declaration leaves it open. The second has a declaration
            // to edit and a rule behind it, so it is refused with the
            // diagnostic that says so; the first keeps the sentence about the closure.
            TransposeRefusal refusal;
            refusal.graph = function->name();
            if (state.shapeWanted) {
                refusal.cause     = TransposeRefusalCause::ParameterShape;
                refusal.param     = state.shapeParam;
                refusal.paramName = state.shapeName;
                refusal.paramType = state.shapeType;
            } else {
                refusal.cause = TransposeRefusalCause::NeedsAValueOutsideTheClosure;
            }
            // The line names the cause by the table's word rather than the log's `skip=` prefix:
            // this record is written after the walk, and `skip=` belongs to the one place that
            // renders a refusal as the walk meets it. What a reader greps for is here all the same.
            const char *const word = transposeRefusalWord(refusal.cause);
            os << "TRANSPOSE function=" << function->name() << " ports=" << builtPorts
               << " promised=" << promisedPorts << " cause=" << (word != nullptr ? word : "-");
            if (state.shapeWanted) {
                os << " param=" << state.shapeParam << " param-type=" << state.shapeType;
            }
            os << '\n';
            return TransposeGraph{refusal};
        }
    }
    return TransposeGraph{*context, group.program(), const_cast<camel::runtime::GCGraph *>(made)};
}

} // namespace camel::transforms
