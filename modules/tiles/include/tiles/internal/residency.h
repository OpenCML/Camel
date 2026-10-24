// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The residency conclusion (docs/design/gpu/plan.cn.md 3.2): where each value's block lives, and
// what crosses a placement boundary. The conclusion is a query over the graph, printed as
// `TILES_RESIDENCY` by the pass that reads the placement fact; the block a value lives in is
// decided by the node that produces it, a call's parameters follow their arguments' blocks, and a
// loop's state stays where it was made. The pass that inserts `tiles:upload`/`tiles:download`
// nodes reads this conclusion instead of deciding transfers per consumer.

#pragma once

#include "camel/runtime/graph.h"

#include <string>

namespace camel::runtime {
class Program;
}

namespace camel::tiles {

/// `TILES_RESIDENCY` lines, one JSON object per graph (docs/design/gpu/plan.cn.md 3.2). Empty
/// when the program writes no placement fact, so an untagged program's output is unchanged.
std::string residencyReport(const runtime::Program &program);

} // namespace camel::tiles