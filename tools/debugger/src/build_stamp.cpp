// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The debugger's build stamp in its own translation unit, for the same reason as the CLI's
 * (`tools/camel-cli/build_stamp.cpp`): `build_config.h` is rewritten by every configure whose
 * pinned footprint moves, and repl.cpp is a heavy unit that only prints the string in `--version`.
 */
#include "build_config.h"

#include "build_stamp.h"

namespace debugger {

const char *buildFootprint() { return BUILD_FOOTPRINT; }

} // namespace debugger