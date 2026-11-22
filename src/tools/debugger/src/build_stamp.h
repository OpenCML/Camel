// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

namespace debugger {

/// The footprint this binary was configured with. The value reaches the binary through one small
/// translation unit (`build_stamp.cpp`), so a stamp change does not recompile the units that print
/// it; they call this function instead of reading the generated header themselves.
const char *buildFootprint();

} // namespace debugger