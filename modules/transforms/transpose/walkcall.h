// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The shape one node family
'
s transposition takes: the walk
'
s state, the draft the member is
 * being built in, the source graph and the node. One struct for all four families, because the
 * families differ in what they do and not in what they are handed -- and because a second struct
 * per family would be four copies of the same five fields.
 *
 * This file declares; it defines nothing, and it is not the walk. What each family does is in
 * `transpose/calls.cpp`, `transpose/regions.cpp`, `transpose/arrays.cpp` and
 * `transpose/closures.cpp`; the state, the pair call and the dispatch that reaches them stay in
 * `transpose/walk.cpp`.
 */

#pragma once

#include "transpose/walk.h"

#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <iosfwd>

namespace camel::transforms {

/// One node handed to its family, with everything the family needs to answer: the shared state,
/// the draft this member is being built in, the source graph (the place types and names are read
/// from), the node itself, and the stream a refusal is written to.
struct WalkCall {
    TransposeState &state;
    rt::GraphDraft &draft;
    rt::GCGraph *function = nullptr;
    gc_node_ref_t ref = rt::kInvalidNodeRef;
    std::ostream &os;
};

/// `Call` and `Func`: a call through a value, or a call through a graph known at build time. The
/// family that owns `transposePairCall`.
bool buildCallFamily(WalkCall &call);

/// `Loop`, `Join` and `Gate`: the region a body hands its value on through.
bool buildRegionFamily(WalkCall &call);

/// `Oper`: one operator instance, its law, and the arguments that law is handed.
bool buildArrayFamily(WalkCall &call);

/// `Fill` and `Accs`: a closure
'
s construction and the member its answer is read from.
bool buildClosureFamily(WalkCall &call);

} // namespace camel::transforms
