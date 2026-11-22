// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The build stamp's only translation unit.
 *
 * `build_config.h` is written by `configure_file` at configure time, so its content moves whenever
 * the pinned footprint in `build/footprint.json` moves: a source change, a different option set, a
 * new commit in the name. Every translation unit that includes it is recompiled for that one
 * string, so the include lives here and everything else asks `camelBuildFootprint()`. A stamp
 * change now costs this file and the link, not config.cpp (17 s) and the debugger's repl.cpp.
 */
#include "build_config.h"

#include "build_stamp.h"

const char *camelBuildFootprint() { return BUILD_FOOTPRINT; }