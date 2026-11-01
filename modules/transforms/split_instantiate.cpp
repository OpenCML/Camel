// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `split(jvp(f))` while the caller's graph is built (compile/gir/compile_time.h).
 *
 * The run-time kernel (split_operator.cpp) reads the lifted function's graph and builds the two
 * halves when the call runs. For a value the front end built a third of a line earlier -- `jvp(f)`
 * with `f` written where the call is, answered by jvp_instantiate.cpp -- nothing about the cut
 * needs to wait for the run: the graph it cuts exists, so the signature check, the residual count
 * and both halves' graphs can exist beside it, and the call site holds the pair as a constant.
 *
 * What the front end is given is the operator's OPER node and the graph it was written in. The
 * value it was applied to is one of that graph's static slots (`StaticProgramFunction`: a graph of
 * a program the transform built), so the whole of the cut is `buildSplitGraph` on that graph, the
 * same function the kernel calls. The halves are then two more such values, in one tuple.
 *
 * The shapes it declines keep the kernel, and they decline silently: a refusal is the run-time
 * path's to report, and the run-time path reports it where the primitive was written.
 */

#include "split.h"
#include "split_operator.h"

#include "camel/compile/gir/compile_time.h"
#include "camel/compile/gir/static_function.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"

#include <array>
#include <optional>
#include <sstream>
#include <string>

namespace camel::transforms {

namespace {

namespace gir = camel::compile::gir;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::toSlot;
using camel::runtime::GCNodeKind;

/// The node a value is read through with the copies that only hand it on stripped: a `let` binding,
/// an argument passed on. A cut of a value two copies away is the same cut.
gir::draft_node_ref_t throughCopies(const gir::CompileTimeCall &call, gir::draft_node_ref_t ref) {
    const auto &draft = call.caller->view();
    for (int hop = 0; hop < 4; ++hop) {
        const auto *header = draft.header(ref);
        if (header == nullptr || header->kind != GCNodeKind::Copy) {
            return ref;
        }
        const auto inputs = draft.withInputsOf(ref);
        if (inputs.size() != 1) {
            return ref;
        }
        ref = inputs.front();
    }
    return ref;
}

/// The lifted graph the value names, and the program that holds it, or nullptr when it names none:
/// `split` of a value the front end did not build here -- a parameter, a closure, a value a call
/// returned -- is not a cut this table can make, because there is no graph in hand to read.
const camel::runtime::GCGraph *liftedGraphOf(
    const gir::CompileTimeCall &call, gir::draft_node_ref_t value,
    const camel::runtime::Program *&program) {
    program            = nullptr;
    const auto &draft  = call.caller->view();
    const auto *header = draft.header(value);
    if (header == nullptr || header->kind != GCNodeKind::Data || header->dataIndex >= 0) {
        return nullptr;
    }
    const auto slots  = draft.staticSlots();
    const size_t slot = static_cast<size_t>(-static_cast<int64_t>(header->dataIndex));
    if (slot >= slots.size() || slots[slot] == NullSlot) {
        return nullptr;
    }
    auto *object = fromSlot<camel::core::rtdata::Object *>(slots[slot]);
    if (!gir::isStaticProgramFunction(object)) {
        return nullptr;
    }
    const auto *carrier = static_cast<gir::StaticProgramFunction *>(object);
    if (carrier->program() == nullptr) {
        return nullptr;
    }
    program = carrier->program();
    return carrier->program()->graph(carrier->graphId());
}

std::optional<gir::node_handle_t> instantiateSplit(gir::CompileTimeCall &call) {
    const auto &draft = call.caller->view();
    const auto inputs = draft.normInputsOf(call.node);
    if (inputs.size() != 1 || !draft.withInputsOf(call.node).empty()) {
        return std::nullopt;
    }
    const gir::draft_node_ref_t value         = throughCopies(call, inputs.front());
    const camel::runtime::Program *cutProgram = nullptr;
    const camel::runtime::GCGraph *lifted     = liftedGraphOf(call, value, cutProgram);
    if (lifted == nullptr) {
        // Not this shape: the value the cut reads was not lifted by the build, so there is no graph
        // here to cut -- `split` of a function that only exists at run time waits for the kernel.
        return std::nullopt;
    }
    const auto *header = draft.header(call.node);
    if (header == nullptr || header->dataType == nullptr ||
        header->dataType->code() != camel::core::type::TypeCode::Tuple) {
        return std::nullopt;
    }
    auto *pairType = static_cast<camel::core::type::TupleType *>(header->dataType);
    if (pairType->size() != 2) {
        return std::nullopt;
    }

    // The readings are kept until the cut answers: a refusal is the run-time path's to report, in
    // the words it always used, so a refused cut here says nothing at all.
    std::ostringstream readings;
    const SplitGraph built = buildSplitGraph(
        call.context, cutProgram, const_cast<camel::runtime::GCGraph *>(lifted), readings);
    if (built.forward == nullptr || built.backward == nullptr) {
        return std::nullopt;
    }
    call.os << readings.str();
    // Which half built the two graphs, in a word: a build-time cut has no run-time graph id to name
    // for the function it cut, and a number there would read as one.
    printSplitCallReading(call.os, lifted->name(), "compile");

    auto *pair = ::Tuple::create(pairType->size(), camel::core::mm::autoSpace());
    const std::array<const camel::runtime::GCGraph *, 2> halves{built.forward, built.backward};
    for (size_t i = 0; i < halves.size() && i < pairType->size(); ++i) {
        auto *carrier = gir::StaticProgramFunction::create(
            built.program, halves[i]->id(), halves[i]->funcType(), camel::core::mm::autoSpace());
        pair->set<slot_t>(i, toSlot<gir::StaticProgramFunction *>(carrier), pairType);
    }
    const auto slot = call.caller->addStaticSlot(toSlot<::Tuple *>(pair), pairType);
    const auto data = call.caller->edit().addDataNode(
        pairType, slot, static_cast<uint8_t>(camel::runtime::kGCNodeFlagConstant));
    // The call becomes the pair it stands for: every reader of its value reads the value, the call's
    // place in a statement chain is the value's, and the call itself is gone -- a call that is gone
    // is a kernel that does not run.
    gir::replaceCallWithValue(call.caller, call.node, data);
    return call.caller->view().ref(data);
}

} // namespace

void registerSplitCompileTimeTransform() {
    gir::registerCompileTimeTransform("transforms:split", &instantiateSplit);
}

} // namespace camel::transforms
