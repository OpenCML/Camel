// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `jvp(f)` answered while the caller's graph is being built: the compile-time half of the transform.
 *
 * A call of `jvp(f)` whose `f` is a function written where the call is -- its graph is in hand, it
 * captures nothing, and its body calls nothing -- has a lift that is decided by the source and not by
 * the run: the graph `buildJvpGraph` builds is the same graph every time the program runs. So it is
 * built here, while the call's own graph is a draft, and the call site holds the function value the
 * graph stands for instead of a call that builds one (`camel/compile/gir/compile_time.h`).
 *
 * The build is the run-time kernel's, not a second implementation: the same `buildJvpGraph`, reading
 * the caller's draft through the face a lift reads (`DraftGraphRead` over the encoder's id table), and
 * writing into a program of its own the way the kernel writes into one of its own. The readings are
 * therefore the readings the kernel prints, and the graph is the graph it hands back.
 *
 * What is *not* moved here is named where each shape is refused below: a function that is a
 * parameter or a returned value (its graph is not known where the call is written), a function that
 * captures (its closure is not a constant of the program), a body that calls another function (the
 * callee lift reads the program's own graphs, and a lift of the lift's own has none), and a value
 * handed to another transform rather than called (`split`, `transpose`: those read the graph in the
 * program they run in). Each of them keeps the run-time lift it has today.
 */

#include "jvp.h"
#include "jvp_operator.h"
#include "split.h"

#include "camel/compile/gir/compile_time.h"
#include "camel/compile/gir/static_function.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/rtdata/func.h"

#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace camel::transforms {

namespace {

namespace gir = camel::compile::gir;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::toSlot;
using camel::runtime::GCNodeKind;

/// The compile graph the call's argument is the value of, or nullptr when the argument is not a
/// function value of this program (`jvp(g)` where `g` is a parameter, a closure, or a value a call
/// returned: none of them is a graph the front end holds here).
gir::graph_ptr_t directCalleeOf(const gir::CompileTimeCall &call) {
    const auto &draft = call.caller->view();
    const auto inputs = draft.normInputsOf(call.node);
    if (inputs.size() != 1) {
        return nullptr;
    }
    const auto *header = draft.header(inputs.front());
    if (header == nullptr || header->kind != GCNodeKind::Data || header->dataIndex >= 0) {
        return nullptr;
    }
    const auto slots  = draft.staticSlots();
    const size_t slot = static_cast<size_t>(-static_cast<int64_t>(header->dataIndex));
    if (slot >= slots.size() || slots[slot] == NullSlot) {
        return nullptr;
    }
    auto *object = fromSlot<camel::core::rtdata::Object *>(slots[slot]);
    if (!gir::isStaticFunction(object)) {
        return nullptr;
    }
    return static_cast<gir::StaticFunction *>(object)->graph();
}

/// Whether the body calls another function. A body that does is a body whose lift reads the graphs
/// its calls name -- the program's own -- and this build reads a draft of the caller's, so the
/// callee lift would find none. Those bodies keep the run-time lift.
bool bodyCalls(const gir::graph_ptr_t &graph) {
    const auto &draft = graph->view();
    for (gir::draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        const auto *header = draft.header(id);
        if (header != nullptr && header->kind == GCNodeKind::Func) {
            return true;
        }
    }
    return false;
}

/// The URI of the operator that cuts a lifted function in two: `split(jvp(f))` is the shape `vjp` is
/// built from, and the cut reads the graph the lift made. The table knows how to take that value
/// (split_instantiate.cpp), so the lift may answer here too.
constexpr std::string_view kSplitUri = "transforms:split";

/// Whether the call's value is read where it is made: one CALL node takes it as its callee (a copy
/// in between is the same value, once), or the `split` operator takes it as the function to cut, and
/// nothing else reads it. A value passed on to `transpose`, or handed to anything else, keeps the
/// run-time lift: that reader would look for the value in the program it runs in.
bool calledWhereItIsMade(const gir::CompileTimeCall &call) {
    const auto &draft = call.caller->view();
    if (!draft.ctrlUsersOf(call.node).empty()) {
        return false;
    }
    const auto normUsers = draft.normUsersOf(call.node);
    if (!normUsers.empty()) {
        if (normUsers.size() != 1) {
            return false;
        }
        const auto *reader = draft.header(normUsers.front());
        if (reader == nullptr || reader->kind != GCNodeKind::Oper) {
            return false;
        }
        const auto op = call.caller->operTarget(normUsers.front());
        return op != nullptr && op->uri() == kSplitUri;
    }
    gir::draft_node_ref_t user = call.node;
    for (int hop = 0; hop < 4; ++hop) {
        const auto users = draft.withUsersOf(user);
        if (users.size() != 1) {
            return false;
        }
        user             = users.front();
        const auto *next = draft.header(user);
        if (next == nullptr) {
            return false;
        }
        if (next->kind == GCNodeKind::Call) {
            return true;
        }
        if (next->kind != GCNodeKind::Copy) {
            return false;
        }
    }
    return false;
}

/// Answers the call, or says no (nullptr) and leaves it exactly as it was built.
std::optional<gir::node_handle_t> instantiateJvp(gir::CompileTimeCall &call) {
    const gir::graph_ptr_t callee = directCalleeOf(call);
    if (callee == nullptr) {
        // The argument is not a function of this program: a parameter, a closure, a returned value.
        return std::nullopt;
    }
    if (!callee->view().closureNodes().empty() || !callee->captureNames().empty()) {
        // It captures: the value it is called with carries the captures, which are not a constant of
        // the program.
        return std::nullopt;
    }
    if (bodyCalls(callee)) {
        // The lifted body calls another function: the lift would have to read the callee's graph here,
        // and this reads a draft.
        return std::nullopt;
    }
    if (!calledWhereItIsMade(call)) {
        // The value is handed to something else: that reader looks for it in the program it runs in.
        return std::nullopt;
    }

    // The readings are kept until the lift answers: a refusal is the run-time path's to report, and
    // it reports it where the primitive was written, so a refused lift here says nothing at all.
    std::ostringstream readings;
    // The id table the encoder numbers the graph by, so what the lift reads here is the graph the
    // program will hold: the same node ids, the same exit, the same count.
    const std::vector<gir::draft_node_ref_t> kept = gir::liveDraftNodes(callee->transaction());
    const gir::DraftGraphRead face(callee->transaction(), kept);
    // The lift's own graphs go into a program of their own, as the run-time kernel's do; the graph
    // it was lifted from is named by no id here: a graph is numbered when the program is encoded,
    // which is after this runs (the run-time lift names it, and the name is the same graph's).
    const JvpGraph built = buildJvpGraphOfItsOwn(
        call.context, face, callee->name(), camel::runtime::kInvalidGraphId, readings);
    if (built.graph == nullptr) {
        return std::nullopt;
    }
    call.os << readings.str();
    // The reading of the call, in the same words the run-time kernel prints it in: the value the call
    // stands for is the one it would have answered with, so the two halves read the same line.
    printJvpCallReading(call.os, callee->funcType(), built.graph->funcType(), "compile");

    auto *carrier = gir::StaticProgramFunction::create(
        built.program, built.graph->id(), built.graph->funcType(), camel::core::mm::autoSpace());
    const auto slot = call.caller->addStaticSlot(
        toSlot<gir::StaticProgramFunction *>(carrier), built.graph->funcType());
    const auto data = call.caller->edit().addDataNode(
        built.graph->funcType(), slot, static_cast<uint8_t>(camel::runtime::kGCNodeFlagConstant));
    // The call becomes what it stands for: every reader of its value reads the value, the call's place
    // in a statement chain is the value's, and the call itself is gone -- a call that is gone is a
    // kernel that does not run.
    gir::replaceCallWithValue(call.caller, call.node, data);
    return call.caller->view().ref(data);
}

} // namespace

void registerCompileTimeTransforms() {
    gir::registerCompileTimeTransform("transforms:jvp", &instantiateJvp);
    // The cut reads the value the lift answers, so the two halves are registered together: the pair
    // of a lift and a cut written in one line is answered by the two of them or by neither.
    registerSplitCompileTimeTransform();
}

} // namespace camel::transforms