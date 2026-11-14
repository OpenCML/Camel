// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The bridge the effect-masking transition needs (docs/design/effects/plan.cn.md, step 5).
 *
 * Masking -- a function's own variable is not one of its caller's resources, and a closure that
 * stays inside the call is not either (design principles 3.3, "效应遮蔽") -- is implemented twice
 * today: in the builder's walk (builder_resources.cpp#placeCapturedUse, #placeCalleeCapture) and in
 * the runtime's walk (runtime/resources.cpp#FootprintWalk, which drops an access whose origin is
 * `Fresh`, and #call). R3 replaces both with one analysis, and the author's review of 2026-11-14
 * (f3be47bf3) asks for a case that asks both sides meanwhile, so the two cannot drift apart unseen:
 * "with a case that asks both sides meanwhile".
 *
 * Such a case needs one thing neither side publishes today. The runtime's answer is a fact about a
 * frozen graph (runtime::footprintOf), which does not exist while the bodies are built, and the
 * builder's answer for a body is not the signature its type carries: a measured set that names a
 * class the clause language has no word for (`anyOf`) is deliberately not written onto the type
 * (builder_resources.cpp#measureInferredResources), and a body that wrote a clause keeps the clause
 * there rather than its measurement. So the builder hands the settled reach of every body it
 * measured over here, keyed by the GraphId the frozen graph keeps, and the test-only pass
 * `test::resource_transition` reads it back beside the runtime's footprint of the same graph.
 *
 * TEST-ONLY: nothing in a run reads this table. It is deleted with the builder's walk when R3
 * lands, together with the pass that asks it.
 */

#pragma once

#include "camel/core/resource_semantics.h"
#include "camel/runtime/graph_id.h"

namespace camel::compile::gir {

namespace sem = camel::core::sem;

/// TEST-ONLY (see the file comment): what the builder's walk measured the body of `graph` to
/// reach. An earlier entry for the same id is replaced.
void publishMeasuredReach(camel::runtime::GraphId graph, const sem::ResourceSignature &reach);

/// TEST-ONLY: the reach published for `graph`, or nullptr when this compile measured no body with
/// that id (the graph is not a body the front end built, or another compile in this process built
/// it).
const sem::ResourceSignature *measuredReachOf(camel::runtime::GraphId graph);

/// TEST-ONLY: forget every published reach. A compile calls this before it measures, because a
/// GraphId belongs to one program (runtime/graph_id.h) and a later compile in the same process may
/// give the same id to another body.
void clearMeasuredReaches();

} // namespace camel::compile::gir