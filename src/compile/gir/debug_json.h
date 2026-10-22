// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <utility>

namespace debugger {

std::pair<std::string, std::string>
getGirJsonFromCompileGraph(const void *rootGraph, const std::string &graphId);

} // namespace debugger
