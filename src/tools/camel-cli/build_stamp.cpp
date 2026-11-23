// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The build stamp's only translation unit.
 *
 * `build_config.h` is written by `configure_file` at configure time, so its content moves whenever
 * the footprint moves: a source change, a different option set, a new commit in the name. The
 * footprint is a function of the tree (`scripts/common.js`, `pinnedFootprint`), so rebuilding
 * unchanged sources does not move it and a changed byte does, which is what makes two readings'
 * stamps comparable. Every translation unit that includes it is recompiled for that one
 * string, so the include lives here and everything else asks `camelBuildFootprint()`. A stamp
 * change now costs this file and the link, not config.cpp (17 s) and the debugger's repl.cpp.
 */
#include "build_config.h"

#include "build_stamp.h"

const char *camelBuildFootprint() { return BUILD_FOOTPRINT; }